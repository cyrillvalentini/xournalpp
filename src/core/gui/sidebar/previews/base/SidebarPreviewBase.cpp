#include "SidebarPreviewBase.h"

#include <cstdlib>  // for abs, size_t

#include <glib-object.h>  // for g_object_ref, G_CALLBACK, g_sig...
#include <glib.h>         // for g_idle_add

#include "control/Control.h"   // for Control
#include "control/PdfCache.h"  // for PdfCache
#include "gui/Builder.h"       // for Builder
#include "gui/MainWindow.h"    // for MainWindow
#include "model/Document.h"    // for Document
#include "util/GtkUtil.h"
#include "util/Util.h"         // for npos
#include "util/glib_casts.h"   // for wrap_for_once_v

#include "SidebarLayout.h"            // for SidebarLayout
#include "SidebarPreviewBaseEntry.h"  // for SidebarPreviewBaseEntry

constexpr auto XML_FILE = "sidebar.ui";

SidebarPreviewBase::SidebarPreviewBase(Control* control, const char* menuId, const char* toolbarId):
        AbstractSidebarPage(control),
        scrollableBox(gtk_scrolled_window_new(), xoj::util::adopt),
        mainBox(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0), xoj::util::adopt),
        miniaturesContainer(GTK_FIXED(gtk_fixed_new()), xoj::util::adopt) {
    gtk_box_append(GTK_BOX(mainBox.get()), scrollableBox.get());
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollableBox.get()), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrollableBox.get()), GTK_WIDGET(miniaturesContainer.get()));
    // Centre the miniatures in the sidebar (like GNOME's document viewer)
    gtk_widget_set_halign(GTK_WIDGET(miniaturesContainer.get()), GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(scrollableBox.get(), true);

    Document* doc = this->control->getDocument();
    doc->lock_shared();
    if (doc->getPdfPageCount() != 0) {
        this->cache = std::make_unique<PdfCache>(doc->getPdfDocument(), control->getSettings());
    }
    doc->unlock_shared();

    registerListener(this->control);
    this->control->addChangedDocumentListener(this);

    auto* adj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scrollableBox.get()));
    g_signal_connect(
            adj, "notify::page-size", G_CALLBACK(+[](GObject* adj, GParamSpec*, gpointer d) {
                static_cast<SidebarPreviewBase*>(d)->newWidth(gtk_adjustment_get_page_size(GTK_ADJUSTMENT(adj)));
            }),
            this);

    // Ensure visible miniatures have been generated at least once -- they will be kept up to date.
    auto verticalChangeCallback =
            +[](GtkAdjustment*, gpointer d) { static_cast<SidebarPreviewBase*>(d)->ensureVisibleAreRendered(); };
    g_signal_connect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrollableBox.get())), "changed",
                     G_CALLBACK(verticalChangeCallback), this);
    g_signal_connect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrollableBox.get())), "value-changed",
                     G_CALLBACK(verticalChangeCallback), this);

    Builder builder(control->getGladeSearchPath(), XML_FILE);
    GMenuModel* menu = G_MENU_MODEL(builder.getObject(menuId));
    contextMenu.reset(GTK_POPOVER(gtk_popover_menu_new_from_model(menu)), xoj::util::adopt);
    gtk_widget_set_parent(GTK_WIDGET(contextMenu.get()), GTK_WIDGET(miniaturesContainer.get()));

    gtk_box_append(GTK_BOX(mainBox.get()), builder.get(toolbarId));
}

SidebarPreviewBase::~SidebarPreviewBase() { this->control->removeChangedDocumentListener(this); }

void SidebarPreviewBase::enableSidebar() {
    if (!this->enabled) {
        enabled = true;
        layout();
    }
}

void SidebarPreviewBase::ensureVisibleAreRendered() {
    auto* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrollableBox.get()));
    Interval<int> previewInterval = {floor_cast<int>(gtk_adjustment_get_value(adj)),
                                     ceil_cast<int>(gtk_adjustment_get_value(adj) + gtk_adjustment_get_page_size(adj))};
    for (auto&& p: this->previews) {
        if (p->getVerticalPosition().intersect(previewInterval)) {
            p->ensureRendered();
        }
    }
}

void SidebarPreviewBase::disableSidebar() { enabled = false; }

void SidebarPreviewBase::newWidth(double width) {
    static constexpr double TRIGGER = 20.;

    if (std::abs(lastWidth - width) > TRIGGER) {
        this->layout();
        lastWidth = width;
    }
}

auto SidebarPreviewBase::getZoom() const -> double { return this->zoom; }

auto SidebarPreviewBase::getCache() -> PdfCache* { return this->cache.get(); }

void SidebarPreviewBase::layout() {
    if (enabled) {
        SidebarLayout::layout(this);
        // Request the miniatures now visible. Waiting for the scroll range to change is not enough: a new document may
        // have the same range as the previous one (e.g. a single page), and its miniatures would never be requested.
        ensureVisibleAreRendered();
    }
}

auto SidebarPreviewBase::hasData() -> bool { return true; }

auto SidebarPreviewBase::getWidget() -> GtkWidget* { return this->mainBox.get(); }

void SidebarPreviewBase::documentChanged(DocumentChangeType type) {
    if (type == DOCUMENT_CHANGE_COMPLETE || type == DOCUMENT_CHANGE_CLEARED) {
        // The previews first: deleting them removes their jobs and waits for a running one, which may use the cache
        this->previews.clear();
        this->cache.reset();

        Document* doc = control->getDocument();
        doc->lock_shared();
        if (doc->getPdfPageCount() != 0) {
            this->cache = std::make_unique<PdfCache>(doc->getPdfDocument(), control->getSettings());
        }
        doc->unlock_shared();
        updatePreviews();
    }
}

auto SidebarPreviewBase::scrollToPreview(SidebarPreviewBase* sidebar) -> bool {
    if (!sidebar->enabled) {
        return false;
    }

    MainWindow* win = sidebar->control->getWindow();
    if (win == nullptr) {
        return false;
    }

    GtkWidget* w = win->get("sidebar");
    if (!gtk_widget_get_visible(w)) {
        return false;
    }

    if (sidebar->selectedEntry != npos && sidebar->selectedEntry < sidebar->previews.size()) {
        auto& p = sidebar->previews[sidebar->selectedEntry];

        // scroll to preview
        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sidebar->scrollableBox.get()));
        GtkWidget* widget = p->getWidget();

        // Position within the miniatures container; not available until the widget has been allocated
        graphene_rect_t bounds;
        if (!gtk_widget_compute_bounds(widget, gtk_widget_get_parent(widget), &bounds)) {
            g_idle_add(xoj::util::wrap_for_once_v<scrollToPreview>, sidebar);
            return false;
        }

        gtk_adjustment_clamp_page(vadj, bounds.origin.y, bounds.origin.y + bounds.size.height);
    }
    return false;
}

void SidebarPreviewBase::pageDeleted(size_t page) {}

void SidebarPreviewBase::pageInserted(size_t page) {}

void SidebarPreviewBase::openPreviewContextMenu(double x, double y, GtkWidget* entry) {
    double newX, newY;
    xoj::util::gtk::translateCoordinates(entry, GTK_WIDGET(miniaturesContainer.get()), x, y, &newX, &newY);
    GdkRectangle r = {round_cast<int>(newX), round_cast<int>(newY), 0, 0};
    gtk_popover_set_pointing_to(this->contextMenu.get(), &r);
    gtk_popover_popup(this->contextMenu.get());
}
