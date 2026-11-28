#include "MainWindow.h"


#include <adwaita.h>  // for AdwStyleManager
#include <gtk/gtk.h>

#include "control/AudioController.h"                    // for AudioController
#include "control/Control.h"                            // for Control
#include "control/DeviceListHelper.h"                   // for getSourceMapping
#include "control/ScrollHandler.h"                      // for ScrollHandler
#include "control/actions/ActionDatabase.h"             // for ActionDatabase
#include "control/jobs/XournalScheduler.h"              // for XournalScheduler
#include "control/layer/LayerController.h"              // for LayerController
#include "control/settings/Settings.h"                  // for Settings
#include "control/settings/SettingsEnums.h"             // for SCROLLBAR_HIDE_HO...
#include "control/zoom/ZoomControl.h"                   // for ZoomControl
#include "gui/FloatingToolbox.h"                        // for FloatingToolbox
#include "gui/GladeGui.h"                               // for GladeGui
#include "gui/PdfFloatingToolbox.h"                     // for PdfFloatingToolbox
#include "gui/SearchBar.h"                              // for SearchBar
#include "gui/inputdevices/InputEvents.h"               // for INPUT_DEVICE_TOUC...
#include "gui/menus/menubar/Menubar.h"                  // for Menubar
#include "gui/menus/menubar/ToolbarSelectionSubmenu.h"  // for ToolbarSelectionSubmenu
#include "gui/scroll/ScrollHandling.h"                  // for ScrollHandling
#include "gui/sidebar/Sidebar.h"                        // for Sidebar
#include "gui/toolbarMenubar/ToolMenuHandler.h"         // for ToolMenuHandler
#include "gui/toolbarMenubar/model/ToolbarData.h"       // for ToolbarData
#include "gui/toolbarMenubar/model/ToolbarModel.h"      // for ToolbarModel
#include "gui/widgets/SpinPageAdapter.h"                // for SpinPageAdapter
#include "gui/widgets/ToolbarBox.h"                     // for ToolbarBox
#include "gui/widgets/XournalWidget.h"                  // for gtk_xournal_get_l...
#include "util/AppName.h"  // for isRebranded
#include "util/GListView.h"                             // for GListView, GListV...
#include "util/GtkUtil.h"                               // for getWidgetDPI
#include "util/PathUtil.h"                              // for getConfigFile
#include "util/StringUtils.h"                           // for char_cast
#include "util/Util.h"                                  // for execInUiThread, npos
#include "util/XojMsgBox.h"                             // for XojMsgBox
#include "util/glib_casts.h"                            // for wrap_for_once_v
#include "util/i18n.h"                                  // for FS, _F
#include "util/raii/CStringWrapper.h"                   // for OwnedCString

#include "GladeSearchpath.h"     // for GladeSearchpath
#include "ToolbarDefinitions.h"  // for TOOLBAR_DEFINITIO...
#include "XournalView.h"         // for XournalView
#include "config-dev.h"          // for TOOLBAR_CONFIG
#include "config.h"       // for XOPP_APP_ID
#include "filesystem.h"          // for path, exists

using std::string;

static void themeCallback(GObject*, GParamSpec*, gpointer data) { static_cast<MainWindow*>(data)->updateColorscheme(); }

/**
 * Load Overall CSS file with custom icons, other styling and potentially, user changes
 */
static void loadCSS(GdkDisplay* display, GladeSearchpath* gladeSearchPath, const gchar* cssFilename) {
    auto filepath = gladeSearchPath->findFile("", cssFilename);
    xoj::util::GObjectSPtr<GtkCssProvider> provider(gtk_css_provider_new(), xoj::util::adopt);
    gtk_css_provider_load_from_path(provider.get(), Util::toGFilename(filepath).c_str());
    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(provider.get()),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static constexpr auto CSS_FILENAME = "xournalpp.css";
static constexpr auto UI_FILENAME = "main.ui";

MainWindow::MainWindow(GladeSearchpath* gladeSearchPath, Control* control, GtkApplication* parent):
        GladeGui(gladeSearchPath, UI_FILENAME, "mainWindow"),
        control(control),
        toolbar(std::make_unique<ToolMenuHandler>(control, this)),
        menubar(std::make_unique<Menubar>()),
        panedContainerWidget(get("panedMainContents"), xoj::util::ref),
        sidebarWidget(get("sidebar"), xoj::util::ref) {
    gtk_window_set_application(GTK_WINDOW(getWindow()), parent);
    if (xoj::util::isRebranded()) {
        // Own app icon (installed under the application ID) for this window and all dialogs
        gtk_window_set_default_icon_name(XOPP_APP_ID);
        gtk_window_set_icon_name(GTK_WINDOW(getWindow()), XOPP_APP_ID);
    }

    loadCSS(gtk_widget_get_display(this->window), gladeSearchPath, CSS_FILENAME);

    GtkOverlay* overlay = GTK_OVERLAY(get("mainOverlay"));
    this->pdfFloatingToolBox = std::make_unique<PdfFloatingToolbox>(this, overlay);
    this->floatingToolbox = std::make_unique<FloatingToolbox>(this, overlay);

    initXournalWidget();

    setSidebarVisible(control->getSettings()->isSidebarVisible());

    // Window handler
    g_signal_connect(this->window, "close-request", xoj::util::wrap_for_g_callback_v<closeRequestCallback>,
                     this->control);
    g_signal_connect(this->window, "notify::maximized", xoj::util::wrap_for_g_callback_v<windowMaximizedCallback>,
                     this);
    // The title bar is part of the window content (AdwHeaderBar): hide it in fullscreen, like a regular title bar
    g_signal_connect(this->window, "notify::fullscreened", G_CALLBACK(+[](GtkWindow* w, GParamSpec*, gpointer d) {
                         auto* self = static_cast<MainWindow*>(d);
                         gtk_widget_set_visible(self->get("mainHeaderBar"), !gtk_window_is_fullscreen(w));
                     }),
                     this);

    updateScrollbarSidebarPosition();

    gtk_window_set_default_size(GTK_WINDOW(this->window), control->getSettings()->getMainWndWidth(),
                                control->getSettings()->getMainWndHeight());

    if (control->getSettings()->isMainWndMaximized()) {
        gtk_window_maximize(GTK_WINDOW(this->window));
    } else {
        gtk_window_unmaximize(GTK_WINDOW(this->window));
    }

    Util::execInUiThread([=]() {
        // Execute after the window is visible, else the check won't work
        control->setShowMenubar(control->getSettings()->isMenubarVisible());
    });

    // Follow the system's light/dark switch (or the forced variant from the settings)
    g_signal_connect(adw_style_manager_get_default(), "notify::dark", G_CALLBACK(themeCallback), this);
    // ... and the system accent colour (page border and selections)
    g_signal_connect(adw_style_manager_get_default(), "notify::accent-color", G_CALLBACK(themeCallback), this);

    updateColorscheme();
}

void MainWindow::populate(GladeSearchpath* gladeSearchPath) {
    toolbar->populate(gladeSearchPath);
    menubar->populate(gladeSearchPath, this);

    // need to create tool buttons registered in plugins, so they can be added to toolbars
    control->registerPluginToolButtons(this->toolbar.get());

    createToolbar();

    setToolbarVisible(control->getSettings()->isToolbarVisible());
}

GMenuModel* MainWindow::getMenuModel() const { return menubar->getModel(); }

MainWindow::~MainWindow() {
    g_signal_handlers_disconnect_by_func(adw_style_manager_get_default(), reinterpret_cast<gpointer>(themeCallback),
                                         this);
}

void MainWindow::updateColorscheme() {
    AdwStyleManager* styleManager = adw_style_manager_get_default();
    g_signal_handlers_block_by_func(styleManager, reinterpret_cast<gpointer>(themeCallback), this);

    // libadwaita takes care of the Adwaita style, the dark variant and the system accent colour
    switch (control->getSettings()->getThemeVariant()) {
        case THEME_VARIANT_FORCE_LIGHT:
            adw_style_manager_set_color_scheme(styleManager, ADW_COLOR_SCHEME_FORCE_LIGHT);
            break;
        case THEME_VARIANT_FORCE_DARK:
            adw_style_manager_set_color_scheme(styleManager, ADW_COLOR_SCHEME_FORCE_DARK);
            break;
        case THEME_VARIANT_USE_SYSTEM:
        default:
            adw_style_manager_set_color_scheme(styleManager, ADW_COLOR_SCHEME_DEFAULT);
            break;
    }
    this->darkMode = adw_style_manager_get_dark(styleManager);

    // Colours we draw ourselves follow the style too: the page border and selections use the accent colour (if
    // enabled in the settings), the area between the pages the window colour (only while it is the default colour)
    {
        Settings* settings = control->getSettings();
        const Color borderBefore = settings->getBorderColor();
        const Color backgroundBefore = settings->getBackgroundColor();

        GdkRGBA* accent = adw_style_manager_get_accent_color_rgba(styleManager);
        settings->setSystemAccentColor(Util::GdkRGBA_to_rgb(*accent));
        gdk_rgba_free(accent);
        settings->setThemeBackgroundColor(darkMode ? Color(0x22, 0x22, 0x26) : Color(0xde, 0xdd, 0xda));

        if ((borderBefore != settings->getBorderColor() || backgroundBefore != settings->getBackgroundColor()) &&
            this->xournal) {
            this->xournal->forceUpdatePagenumbers();
            gtk_widget_queue_draw(this->winXournal);
        }
    }

    // Set up icons
    {
        const auto uiPath = this->getGladeSearchPath()->getFirstSearchPath();
        const auto lightColorIcons = (uiPath / "iconsColor-light");
        const auto darkColorIcons = (uiPath / "iconsColor-dark");
        const auto lightLucideIcons = (uiPath / "iconsLucide-light");
        const auto darkLucideIcons = (uiPath / "iconsLucide-dark");

        // icon load order from lowest priority to highest priority
        std::vector<fs::path> iconLoadOrder = {};
        const auto chosenTheme = control->getSettings()->getIconTheme();
        switch (chosenTheme) {
            case ICON_THEME_COLOR:
                iconLoadOrder = {darkLucideIcons, lightLucideIcons, darkColorIcons, lightColorIcons};
                break;
            case ICON_THEME_LUCIDE:
                iconLoadOrder = {darkColorIcons, lightColorIcons, darkLucideIcons, lightLucideIcons};
                break;
            case ICON_THEME_SYMBOLIC:
                // The symbolic icons are recoloured by GTK, so one set serves light and dark mode.
                // Lucide stays as a fallback for icons that have no symbolic version.
                iconLoadOrder = {darkColorIcons, lightColorIcons, darkLucideIcons, lightLucideIcons,
                                 uiPath / "iconsSymbolic"};
                break;
            default:
                g_message("Unknown icon theme!");
        }

        if (this->darkMode) {
            for (size_t i = 0; 2 * i + 1 < iconLoadOrder.size(); ++i) {
                std::swap(iconLoadOrder[2 * i], iconLoadOrder[2 * i + 1]);
            }
        }

        for (auto& p: iconLoadOrder) {
            gtk_icon_theme_add_search_path(gtk_icon_theme_get_for_display(gtk_widget_get_display(this->window)),
                                           Util::toGFilename(p).c_str());
        }
    }

    if (this->darkMode) {
        gtk_widget_add_css_class(this->window, "darkMode");
    } else {
        gtk_widget_remove_css_class(this->window, "darkMode");
    }
    // Makes GTK look up "<name>-symbolic" first for every icon in the window (see xournalpp.css)
    if (control->getSettings()->getIconTheme() == ICON_THEME_SYMBOLIC) {
        gtk_widget_add_css_class(this->window, "symbolicIcons");
    } else {
        gtk_widget_remove_css_class(this->window, "symbolicIcons");
    }
    g_debug("Theme variant: %s, icon theme: %s", this->darkMode ? "dark" : "light",
            iconThemeToString(control->getSettings()->getIconTheme()));

    g_signal_handlers_unblock_by_func(styleManager, reinterpret_cast<gpointer>(themeCallback), this);
}

void MainWindow::initXournalWidget() {
    winXournal = gtk_scrolled_window_new();

    setGtkTouchscreenScrollingForDeviceMapping();

    // At the top of the document column (above its search bar and bottom toolbars), next to the sidebar
    gtk_widget_set_vexpand(this->winXournal, true);
    gtk_box_prepend(GTK_BOX(get("documentColumn")), this->winXournal);

    scrollHandling = std::make_unique<ScrollHandling>(GTK_SCROLLED_WINDOW(winXournal));

    this->xournal = std::make_unique<XournalView>(GTK_SCROLLED_WINDOW(winXournal), control, scrollHandling.get());

    control->getZoomControl()->initZoomHandler(GTK_SCROLLED_WINDOW(winXournal), xournal.get(), control);

    // Automated DPI detection
    g_signal_connect(winXournal, "realize", G_CALLBACK(+[](GtkWidget* w, gpointer d) {
                         GdkSurface* surf = gtk_native_get_surface(gtk_widget_get_native(w));
                         xoj_assert(surf);
                         g_signal_connect(surf, "enter-monitor",
                                          G_CALLBACK(+[](GdkSurface*, GdkMonitor* monitor, gpointer d) {
                                              auto* self = static_cast<MainWindow*>(d);
                                              if (monitor && monitor != self->lastMonitor) {
                                                  self->lastMonitor = monitor;
                                                  const char* monitorName = gdk_monitor_get_model(monitor);
                                                  g_debug("Window moved to monitor \"%s\"", monitorName);
                                                  self->setDPI();
                                              }
                                          }),
                                          d);
                     }),
                     this);
}

void MainWindow::setGtkTouchscreenScrollingForDeviceMapping() {
    InputDeviceClass touchscreenClass =
            DeviceListHelper::getSourceMapping(GDK_SOURCE_TOUCHSCREEN, this->getControl()->getSettings());

    setGtkTouchscreenScrollingEnabled(touchscreenClass == INPUT_DEVICE_TOUCHSCREEN &&
                                      !control->getSettings()->getTouchDrawingEnabled());
}

void MainWindow::setGtkTouchscreenScrollingEnabled(bool enabled) {
    if (!control->getSettings()->getGtkTouchInertialScrollingEnabled()) {
        enabled = false;
    }
    gtk_scrolled_window_set_kinetic_scrolling(GTK_SCROLLED_WINDOW(winXournal), enabled);
}

auto MainWindow::getLayout() const -> Layout* { return this->xournal->getLayout(); }

auto MainWindow::getControl() const -> Control* { return control; }

void MainWindow::updateScrollbarSidebarPosition() {
    // Part 1: update scrollbar position
    if (winXournal != nullptr) {
        GtkScrolledWindow* scrolledWindow = GTK_SCROLLED_WINDOW(winXournal);

        ScrollbarHideType type = this->getControl()->getSettings()->getScrollbarHideType();

        bool scrollbarOnLeft = control->getSettings()->isScrollbarOnLeft();
        if (scrollbarOnLeft) {
            gtk_scrolled_window_set_placement(scrolledWindow, GTK_CORNER_TOP_RIGHT);
        } else {
            gtk_scrolled_window_set_placement(scrolledWindow, GTK_CORNER_TOP_LEFT);
        }

        gtk_widget_set_visible(gtk_scrolled_window_get_hscrollbar(scrolledWindow), !(type & SCROLLBAR_HIDE_HORIZONTAL));
        gtk_widget_set_visible(gtk_scrolled_window_get_vscrollbar(scrolledWindow), !(type & SCROLLBAR_HIDE_VERTICAL));

        gtk_scrolled_window_set_overlay_scrolling(scrolledWindow,
                                                  !control->getSettings()->isScrollbarFadeoutDisabled());
    }

    bool sidebarRight = control->getSettings()->isSidebarOnRight();
    auto dir = sidebarRight ? GTK_TEXT_DIR_RTL : GTK_TEXT_DIR_LTR;
    gtk_widget_set_direction(this->panedContainerWidget.get(), dir);

    if (this->sidebarVisible) {
        updatePanedPosition();
    } else {
        gtk_widget_set_visible(sidebarWidget.get(), false);
    }
}

auto MainWindow::closeRequestCallback(GtkWidget* widget, Control* control) -> bool {
    control->quit();

    return true;
}

void MainWindow::setSidebarVisible(bool visible) {
    if (!visible && (this->control->getSidebar() != nullptr)) {
        this->control->getSidebar()->saveSize();
    }

    if (visible != this->sidebarVisible) {
        this->sidebarVisible = visible;
        updatePanedPosition();
        gtk_widget_set_visible(sidebarWidget.get(), visible);
    }
}

void MainWindow::updatePanedPosition() {
    gtk_paned_set_position(GTK_PANED(panedContainerWidget.get()), control->getSettings()->getSidebarWidth());
}

void MainWindow::setToolbarVisible(bool visible) {
    Settings* settings = control->getSettings();

    settings->setToolbarVisible(visible);
    for (auto& tb: this->toolbars) {
        if (!visible || !tb->empty()) {
            gtk_widget_set_visible(tb->getWidget(), visible);
        }
    }
}

void MainWindow::setMenubarVisible(bool visible) {
    // The menu bar is part of the window content (see Menubar::populate)
    gtk_widget_set_visible(get("menubarBox"), visible);
}

void MainWindow::setMaximized(bool maximized) { this->maximized = maximized; }

auto MainWindow::isMaximized() const -> bool { return this->maximized; }

auto MainWindow::setFullscreen(bool enabled) const -> void {
    if (enabled) {
        gtk_window_fullscreen(GTK_WINDOW(this->getWindow()));
    } else {
        gtk_window_unfullscreen(GTK_WINDOW(this->getWindow()));
    }
}

auto MainWindow::isDarkTheme() const -> bool { return this->darkMode; }

auto MainWindow::getXournal() const -> XournalView* { return xournal.get(); }

auto MainWindow::windowMaximizedCallback(GObject* window, GParamSpec*, MainWindow* win) -> void {
    win->setMaximized(gtk_window_is_maximized(GTK_WINDOW(window)));
}

void MainWindow::toolbarSelected(const std::string& id) {
    const auto& toolbars = toolbar->getModel()->getToolbars();
    auto it = std::find_if(toolbars.begin(), toolbars.end(), [&](const auto& d) { return d->getId() == id; });
    toolbarSelected(it == toolbars.end() ? nullptr : it->get());
}

void MainWindow::toolbarSelected(ToolbarData* d) {
    if (!d || this->selectedToolbar == d) {
        return;
    }

    Settings* settings = control->getSettings();
    settings->setSelectedToolbar(d->getId());

    this->clearToolbar();
    this->loadToolbar(d);
}

auto MainWindow::clearToolbar() -> const ToolbarData* {
    if (this->selectedToolbar != nullptr) {
        for (auto& tb: this->toolbars) {
            ToolMenuHandler::unloadToolbar(tb.get());
        }

        this->toolbar->freeDynamicToolbarItems();
    }
    return std::exchange(this->selectedToolbar, nullptr);
}

void MainWindow::loadToolbar(ToolbarData* d) {
    this->selectedToolbar = d;

    for (auto& tb: this->toolbars) {
        this->toolbar->load(d, *tb);
    }

    this->floatingToolbox->flagRecalculateSizeRequired();
}

void MainWindow::reloadToolbars() {
    ToolbarData* d = getSelectedToolbar();
    this->clearToolbar();
    this->toolbarSelected(d);
}

auto MainWindow::getSelectedToolbar() const -> ToolbarData* { return this->selectedToolbar; }

void MainWindow::setDynamicallyGeneratedSubmenuDisabled(bool disabled) { menubar->setDisabled(disabled); }

void MainWindow::updateToolbarMenu() {
    menubar->getToolbarSelectionSubmenu().update(toolbar.get(), this->selectedToolbar);
}

void MainWindow::createToolbar() {
    size_t i = 0;
    for (auto&& def: TOOLBAR_DEFINITIONS) {
        this->toolbars[i++] = std::make_unique<ToolbarBox>(def.propName, get(def.guiName));
    }

    toolbarSelected(control->getSettings()->getSelectedToolbar());

    this->control->getScheduler()->unblockRerenderZoom();
}

void MainWindow::updatePageNumbers(size_t page, size_t pagecount, size_t pdfpage) {
    toolbar->setPageInfo(page, pagecount, pdfpage);
}

auto MainWindow::getMenubar() const -> Menubar* { return menubar.get(); }

void MainWindow::show(GtkWindow* parent) { gtk_window_present(GTK_WINDOW(this->window)); }

void MainWindow::setUndoDescription(const string& description) { menubar->setUndoDescription(description); }

void MainWindow::setRedoDescription(const string& description) { menubar->setRedoDescription(description); }

auto MainWindow::getToolbarModel() const -> ToolbarModel* { return this->toolbar->getModel(); }

auto MainWindow::getToolMenuHandler() const -> ToolMenuHandler* { return this->toolbar.get(); }

PdfFloatingToolbox* MainWindow::getPdfToolbox() const { return this->pdfFloatingToolBox.get(); }

FloatingToolbox* MainWindow::getFloatingToolbox() const { return this->floatingToolbox.get(); }

void MainWindow::setDPI() const {
    if (auto dpi = this->getControl()->getSettings()->getDisplayDpi(); dpi == -1) {
        auto res = xoj::util::gtk::getWidgetDPI(this->window);
        this->getControl()->getZoomControl()->setZoom100Value(res.value_or(Util::DPI_NORMALIZATION_FACTOR) /
                                                              Util::DPI_NORMALIZATION_FACTOR);
    } else {
        this->getControl()->getZoomControl()->setZoom100Value(dpi / Util::DPI_NORMALIZATION_FACTOR);
    }
}
