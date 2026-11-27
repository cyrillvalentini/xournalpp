#include "SidebarIndexPage.h"

#include <cstring>  // for strlen

#include <glib-object.h>  // for g_object_unref
#include <pango/pango.h>  // for PangoLogAttr

#include "control/Control.h"        // for Control
#include "control/ScrollHandler.h"  // for ScrollHandler
#include "model/Document.h"         // for Document
#include "model/LinkDestination.h"  // for XojLinkDest
#include "util/Assert.h"            // for xoj_assert
#include "util/glib_casts.h"        // for wrap_v
#include "util/i18n.h"              // for _
#include "util/safe_casts.h"        // for as_unsigned

/// Key under which each row's page label stores its GBinding to the bookmark's "page-label" property
constexpr auto BINDING_KEY = "page-label-binding";

SidebarIndexPage::SidebarIndexPage(Control* control):
        AbstractSidebarPage(control), iconNameHelper(control->getSettings()) {
    this->selection.reset(gtk_single_selection_new(nullptr), xoj::util::adopt);
    gtk_single_selection_set_autoselect(selection.get(), false);
    gtk_single_selection_set_can_unselect(selection.get(), true);
    this->selectHandler = g_signal_connect(selection.get(), "notify::selected", G_CALLBACK(selectionChanged), this);
    xoj_assert(this->selectHandler != 0);

    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(setupRow), this);
    g_signal_connect(factory, "bind", G_CALLBACK(bindRow), this);
    g_signal_connect(factory, "unbind", G_CALLBACK(unbindRow), this);

    // The list view takes ownership of the factory, but not of the selection
    this->listView = gtk_list_view_new(GTK_SELECTION_MODEL(g_object_ref(selection.get())), factory);
    gtk_widget_add_css_class(listView, "navigation-sidebar");

    GtkEventController* keys = this->keyController = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(keyPressed), this);
    gtk_widget_add_controller(listView, keys);

    this->scrollBookmarks = gtk_scrolled_window_new();
    g_object_ref(this->scrollBookmarks);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollBookmarks), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrollBookmarks), listView);

    registerListener(control);
}

SidebarIndexPage::~SidebarIndexPage() {
    if (this->searchTimeout) {
        g_source_remove(this->searchTimeout);
        this->searchTimeout = 0;
    }
    g_signal_handler_disconnect(selection.get(), selectHandler);
    // The list view may outlive this page (it is part of the window's widget tree)
    g_signal_handlers_disconnect_by_data(keyController, this);

    g_object_unref(this->scrollBookmarks);
}

void SidebarIndexPage::setupRow(GtkSignalListItemFactory*, GtkListItem* item, SidebarIndexPage*) {
    GtkWidget* title = gtk_label_new(nullptr);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_widget_set_hexpand(title, true);

    GtkWidget* page = gtk_label_new(nullptr);
    gtk_widget_add_css_class(page, "dim-label");
    PangoAttrList* attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_style_new(PANGO_STYLE_ITALIC));
    gtk_label_set_attributes(GTK_LABEL(page), attrs);
    pango_attr_list_unref(attrs);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(box), title);
    gtk_box_append(GTK_BOX(box), page);

    GtkWidget* expander = gtk_tree_expander_new();
    gtk_tree_expander_set_child(GTK_TREE_EXPANDER(expander), box);
    gtk_list_item_set_child(item, expander);
}

void SidebarIndexPage::bindRow(GtkSignalListItemFactory*, GtkListItem* item, SidebarIndexPage*) {
    auto* row = GTK_TREE_LIST_ROW(gtk_list_item_get_item(item));
    auto* expander = GTK_TREE_EXPANDER(gtk_list_item_get_child(item));
    gtk_tree_expander_set_list_row(expander, row);

    GtkWidget* box = gtk_tree_expander_get_child(expander);
    GtkWidget* title = gtk_widget_get_first_child(box);
    GtkWidget* page = gtk_widget_get_last_child(box);

    auto* link = LINK_DEST(gtk_tree_list_row_get_item(row));
    gtk_label_set_text(GTK_LABEL(title), link->dest->getName().c_str());
    gtk_widget_set_tooltip_text(box, link->dest->getName().c_str());
    GBinding* binding = g_object_bind_property(link, "page-label", page, "label", G_BINDING_SYNC_CREATE);
    g_object_set_data(G_OBJECT(page), BINDING_KEY, binding);
    g_object_unref(link);
}

void SidebarIndexPage::unbindRow(GtkSignalListItemFactory*, GtkListItem* item, SidebarIndexPage*) {
    auto* expander = GTK_TREE_EXPANDER(gtk_list_item_get_child(item));
    GtkWidget* page = gtk_widget_get_last_child(gtk_tree_expander_get_child(expander));
    if (auto* binding = static_cast<GBinding*>(g_object_get_data(G_OBJECT(page), BINDING_KEY))) {
        g_binding_unbind(binding);
        g_object_set_data(G_OBJECT(page), BINDING_KEY, nullptr);
    }
    gtk_tree_expander_set_list_row(expander, nullptr);
}

auto SidebarIndexPage::getLink(guint position) const -> XojLinkDest* {
    GListModel* model = gtk_single_selection_get_model(selection.get());
    if (model == nullptr || position >= g_list_model_get_n_items(model)) {
        return nullptr;
    }
    auto* row = GTK_TREE_LIST_ROW(g_list_model_get_item(model, position));
    auto* link = LINK_DEST(gtk_tree_list_row_get_item(row));
    g_object_unref(row);
    // The tree list model holds a reference to the item
    g_object_unref(link);
    return link;
}

void SidebarIndexPage::selectRow(guint position) {
    g_signal_handler_block(selection.get(), selectHandler);
    gtk_single_selection_set_selected(selection.get(), position);
    g_signal_handler_unblock(selection.get(), selectHandler);
}

void SidebarIndexPage::gotoSelected() {
    XojLinkDest* link = getLink(gtk_single_selection_get_selected(selection.get()));
    if (link && link->dest) {
        control->getScrollHandler()->scrollToLinkDest(*link->dest);
    }
}

void SidebarIndexPage::selectionChanged(GtkSingleSelection*, GParamSpec*, SidebarIndexPage* sidebar) {
    if (sidebar->searchTimeout) {
        return;
    }
    sidebar->gotoSelected();
}

void SidebarIndexPage::enableSidebar() {}

void SidebarIndexPage::disableSidebar() {
    // Nothing to do at the moment
}

auto SidebarIndexPage::searchTimeoutFunc(SidebarIndexPage* sidebar) -> bool {
    sidebar->searchTimeout = 0;
    sidebar->searchText.clear();
    sidebar->gotoSelected();
    return false;
}

auto SidebarIndexPage::keyPressed(GtkEventControllerKey*, guint keyval, guint, GdkModifierType state,
                                  SidebarIndexPage* sidebar) -> gboolean {
    if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) {
        return false;
    }

    if (keyval == GDK_KEY_BackSpace && !sidebar->searchText.empty()) {
        const char* start = sidebar->searchText.c_str();
        const char* last = g_utf8_find_prev_char(start, start + sidebar->searchText.size());
        sidebar->searchText.resize(static_cast<size_t>(last ? last - start : 0));
    } else if (keyval == GDK_KEY_Escape && !sidebar->searchText.empty()) {
        sidebar->searchText.clear();
    } else {
        gunichar c = gdk_keyval_to_unicode(keyval);
        if (c == 0 || !g_unichar_isprint(c) || (c == ' ' && sidebar->searchText.empty())) {
            return false;
        }
        char buf[8] = {};
        g_unichar_to_utf8(c, buf);
        sidebar->searchText += buf;
    }

    if (sidebar->searchTimeout) {
        g_source_remove(sidebar->searchTimeout);
        sidebar->searchTimeout = 0;
    }
    if (sidebar->searchText.empty()) {
        return true;
    }
    sidebar->searchTimeout = g_timeout_add_seconds_full(G_PRIORITY_DEFAULT_IDLE, 2,
                                                        xoj::util::wrap_v<searchTimeoutFunc>, sidebar, nullptr);

    GListModel* model = gtk_single_selection_get_model(sidebar->selection.get());
    guint n = model ? g_list_model_get_n_items(model) : 0;
    for (guint i = 0; i < n; i++) {
        XojLinkDest* link = sidebar->getLink(i);
        if (link && matchesSearch(link->dest->getName().c_str(), sidebar->searchText.c_str())) {
            sidebar->selectRow(i);
            gtk_list_view_scroll_to(GTK_LIST_VIEW(sidebar->listView), i, GTK_LIST_SCROLL_FOCUS, nullptr);
            break;
        }
    }
    return true;
}

auto SidebarIndexPage::matchesSearch(const char* text, const char* key) -> bool {
    // Source: Pidgin
    gchar* tmp = g_utf8_normalize(key, -1, G_NORMALIZE_DEFAULT);
    gchar* enteredstring = g_utf8_casefold(tmp, -1);
    g_free(tmp);

    tmp = g_utf8_normalize(text, -1, G_NORMALIZE_DEFAULT);
    gchar* normalized = g_utf8_casefold(tmp, -1);
    g_free(tmp);

    bool result = false;
    if (g_str_has_prefix(normalized, enteredstring)) {
        result = true;
    } else {
        /* Use Pango to separate by words. */
        size_t len = as_unsigned(g_utf8_strlen(normalized, -1));
        PangoLogAttr* log_attrs = g_new(PangoLogAttr, len + 1);

        pango_get_log_attrs(normalized, static_cast<int>(strlen(normalized)), -1, nullptr, log_attrs,
                            static_cast<int>(len + 1));

        gchar* word = normalized;
        for (size_t i = 0; i + 1 < len; i++) {
            if (log_attrs[i].is_word_start && g_str_has_prefix(word, enteredstring)) {
                result = true;
                break;
            }
            word = g_utf8_next_char(word);
        }
        g_free(log_attrs);
    }

    g_free(enteredstring);
    g_free(normalized);
    return result;
}

auto SidebarIndexPage::getName() -> std::string { return _("Contents"); }

auto SidebarIndexPage::getIconName() -> std::string { return this->iconNameHelper.iconName("sidebar-index"); }

auto SidebarIndexPage::hasData() -> bool { return this->hasContents; }

auto SidebarIndexPage::getWidget() -> GtkWidget* { return this->scrollBookmarks; }

static int countEntries(GListModel* model) {
    int count = 0;
    guint n = g_list_model_get_n_items(model);
    for (guint i = 0; i < n; i++) {
        auto* link = LINK_DEST(g_list_model_get_item(model, i));
        count++;
        if (link->children) {
            count += countEntries(G_LIST_MODEL(link->children));
        }
        g_object_unref(link);
    }
    return count;
}

auto SidebarIndexPage::expandOpenLinks() -> int {
    auto* tree = GTK_TREE_LIST_MODEL(gtk_single_selection_get_model(selection.get()));
    if (tree == nullptr) {
        return 0;
    }
    // Expanding a row inserts its children right after it, so they are visited as well
    for (guint i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(tree)); i++) {
        GtkTreeListRow* row = gtk_tree_list_model_get_row(tree, i);
        auto* link = LINK_DEST(gtk_tree_list_row_get_item(row));
        if (link->dest->getExpand() && gtk_tree_list_row_is_expandable(row)) {
            gtk_tree_list_row_set_expanded(row, true);
        }
        g_object_unref(link);
        g_object_unref(row);
    }
    return countEntries(gtk_tree_list_model_get_model(tree));
}

void SidebarIndexPage::selectPageNr(size_t page, size_t pdfPage) {
    GListModel* model = gtk_single_selection_get_model(selection.get());
    if (model == nullptr) {
        return;
    }

    // check if there is already a bookmark from this page selected
    XojLinkDest* selected = getLink(gtk_single_selection_get_selected(selection.get()));
    if (selected && selected->dest && selected->dest->getPdfPage() == pdfPage) {
        return;
    }

    // Select the first visible bookmark pointing to this page (rows are in depth-first order)
    guint n = g_list_model_get_n_items(model);
    for (guint i = 0; i < n; i++) {
        XojLinkDest* link = getLink(i);
        if (link && link->dest && link->dest->getPdfPage() == pdfPage) {
            selectRow(i);
            return;
        }
    }
}

static GListModel* createChildModel(gpointer item, gpointer) {
    auto* link = LINK_DEST(item);
    return link->children ? G_LIST_MODEL(g_object_ref(link->children)) : nullptr;
}

void SidebarIndexPage::documentChanged(DocumentChangeType type) {
    if (type == DOCUMENT_CHANGE_CLEARED) {
        gtk_single_selection_set_model(selection.get(), nullptr);
    } else if (type == DOCUMENT_CHANGE_PDF_BOOKMARKS || type == DOCUMENT_CHANGE_COMPLETE) {
        Document* doc = this->control->getDocument();

        //  Block the selection signal when the document changes, otherwise
        //  there will be a deadlock: both the selectHandler and this code will
        //  lock the document.
        g_signal_handler_block(selection.get(), this->selectHandler);
        doc->lock_shared();
        GListModel* contents = doc->getContentsModel();
        GtkTreeListModel* tree = nullptr;
        if (contents) {
            tree = gtk_tree_list_model_new(G_LIST_MODEL(g_object_ref(contents)), false, false, createChildModel,
                                           nullptr, nullptr);
        }
        gtk_single_selection_set_model(selection.get(), G_LIST_MODEL(tree));
        if (tree) {
            g_object_unref(tree);
        }
        int count = expandOpenLinks();
        doc->unlock_shared();
        g_signal_handler_unblock(selection.get(), this->selectHandler);

        hasContents = (count != 0);
    }
}

void SidebarIndexPage::layout() {}
