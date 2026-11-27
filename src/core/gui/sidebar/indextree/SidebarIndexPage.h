/*
 * Xournal++
 *
 * Index Sidebar Page
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>  // for size_t
#include <string>   // for string

#include <glib.h>     // for gboolean, gchar, gint
#include <gtk/gtk.h>  // for GtkWidget, GtkListView

#include "gui/IconNameHelper.h"               // for IconNameHelper
#include "gui/sidebar/AbstractSidebarPage.h"  // for AbstractSidebarPage
#include "model/DocumentChangeType.h"         // for DocumentChangeType
#include "model/LinkDestination.h"            // for XojLinkDest
#include "util/raii/GObjectSPtr.h"            // for GObjectSPtr

class Control;
class SidebarToolbar;

class SidebarIndexPage: public AbstractSidebarPage {
public:
    SidebarIndexPage(Control* control);
    ~SidebarIndexPage() override;

public:
    void enableSidebar() override;
    void disableSidebar() override;

    void layout() override;

    /**
     * @overwrite
     */
    std::string getName() override;

    /**
     * @overwrite
     */
    std::string getIconName() override;

    /**
     * @overwrite
     */
    bool hasData() override;

    /**
     * @overwrite
     */
    GtkWidget* getWidget() override;

    /**
     * @overwrite
     */
    void selectPageNr(size_t page, size_t pdfPage) override;

    /**
     * @overwrite
     */
    void documentChanged(DocumentChangeType type) override;

private:
    static void setupRow(GtkSignalListItemFactory*, GtkListItem* item, SidebarIndexPage*);
    static void bindRow(GtkSignalListItemFactory*, GtkListItem* item, SidebarIndexPage*);
    static void unbindRow(GtkSignalListItemFactory*, GtkListItem* item, SidebarIndexPage*);

    /**
     * The selected bookmark changed: scroll to it (unless the selection was set programmatically or by typing)
     */
    static void selectionChanged(GtkSingleSelection* selection, GParamSpec*, SidebarIndexPage* sidebar);

    /// Scroll the document to the selected bookmark
    void gotoSelected();

    /// Select the visible row at position (without scrolling the document)
    void selectRow(guint position);

    /// @return the bookmark at a visible row, or nullptr
    XojLinkDest* getLink(guint position) const;

    /**
     * Typeahead search: typing selects the first visible entry with a word starting with the typed text.
     * After a timeout we scroll to the selected page.
     */
    static gboolean keyPressed(GtkEventControllerKey*, guint keyval, guint, GdkModifierType state,
                               SidebarIndexPage* sidebar);
    static auto searchTimeoutFunc(SidebarIndexPage* sidebar) -> bool;
    static bool matchesSearch(const char* text, const char* key);

    /// Expand the entries the PDF marks as open. @return the total number of entries
    int expandOpenLinks();

private:
    GtkWidget* listView = nullptr;
    GtkWidget* scrollBookmarks = nullptr;
    GtkEventController* keyController = nullptr;

    /// Selection over the flattened tree (GtkTreeListModel of the document's outline)
    xoj::util::GObjectSPtr<GtkSingleSelection> selection;
    gulong selectHandler = 0;

    std::string searchText;
    guint searchTimeout = 0;

    /**
     * If there is something to display in the tree
     */
    bool hasContents = false;

    IconNameHelper iconNameHelper;
};
