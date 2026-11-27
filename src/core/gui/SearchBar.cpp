#include "SearchBar.h"

#include <string>  // for allocator, string

#include <gdk/gdk.h>         // for GdkEventKey, GDK_SHIFT_MASK
#include <gdk/gdkkeysyms.h>  // for GDK_KEY_Return
#include <glib-object.h>     // for G_CALLBACK, g_signal_connect
#include <glib.h>            // for g_free, g_strdup_printf

#include "control/Control.h"            // for Control
#include "control/NavigationHistory.h"  // for NavigationHistory
#include "control/ScrollHandler.h"      // for ScrollHandler
#include "control/zoom/ZoomControl.h"  // for ZoomControl
#include "gui/MainWindow.h"            // for MainWindow
#include "model/Document.h"            // for Document
#include "util/PlaceholderString.h"    // for PlaceholderString
#include "util/i18n.h"                 // for _, FC, _F

SearchBar::SearchBar(Control* control): control(control) {
    MainWindow* win = control->getWindow();

    GtkWidget* close = win->get("buttonCloseSearch");
    g_signal_connect(close, "clicked", G_CALLBACK(buttonCloseSearchClicked), this);

    GtkWidget* next = win->get("btSearchForward");
    GtkWidget* previous = win->get("btSearchBack");
    g_signal_connect(next, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer d) { static_cast<SearchBar*>(d)->searchNext(); }), this);
    g_signal_connect(previous, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer d) { static_cast<SearchBar*>(d)->searchPrevious(); }), this);

    GtkWidget* searchTextField = win->get("searchTextField");
    g_signal_connect(searchTextField, "search-changed", G_CALLBACK(searchTextChangedCallback), this);
    g_signal_connect(searchTextField, "stop-search",
                     G_CALLBACK(+[](GtkSearchEntry*, gpointer d) { static_cast<SearchBar*>(d)->showSearchBar(false); }),
                     this);
    g_signal_connect(searchTextField, "next-match", G_CALLBACK(+[](GtkSearchEntry* e, gpointer d) {
                         static_cast<SearchBar*>(d)->searchNext();
                         // Grab focus again since searching will take away focus
                         gtk_widget_grab_focus(GTK_WIDGET(e));
                     }),
                     this);
    g_signal_connect(searchTextField, "previous-match", G_CALLBACK(+[](GtkSearchEntry* e, gpointer d) {
                         static_cast<SearchBar*>(d)->searchPrevious();
                         // Grab focus again since searching will take away focus
                         gtk_widget_grab_focus(GTK_WIDGET(e));
                     }),
                     this);

    GtkEventController* ctrl = gtk_shortcut_controller_new();
    for (guint key: {GDK_KEY_Return, GDK_KEY_KP_Enter, GDK_KEY_ISO_Enter}) {
        gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(ctrl),
                                             gtk_shortcut_new(gtk_keyval_trigger_new(key, GdkModifierType(0)),
                                                              gtk_signal_action_new("next-match")));
        gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(ctrl),
                                             gtk_shortcut_new(gtk_keyval_trigger_new(key, GDK_SHIFT_MASK),
                                                              gtk_signal_action_new("previous-match")));
    }
    // The entry's inner text field consumes Return (as "activate"), so catch it before it gets there
    gtk_event_controller_set_propagation_phase(ctrl, GTK_PHASE_CAPTURE);
    gtk_widget_add_controller(searchTextField, ctrl);

    this->searchTextField = searchTextField;
}

SearchBar::~SearchBar() { this->control = nullptr; }

void SearchBar::setSearchState(bool found) {
    // The theme's "error" style shows the text in red
    if (found) {
        gtk_widget_remove_css_class(this->searchTextField, "error");
    } else {
        gtk_widget_add_css_class(this->searchTextField, "error");
    }
}

void SearchBar::searchFromAnchor(const char* text) {
    GtkWidget* lbSearchState = control->getWindow()->get("lbSearchState");
    const size_t pageCount = control->getDocument()->getPageCount();
    if (pageCount == 0) {
        return;
    }
    if (!anchorValid || anchorPage >= pageCount) {
        anchorPage = control->getCurrentPageNo();
        anchorValid = true;
    }

    // Remove the highlights of the previous text
    if (page < pageCount) {
        control->searchTextOnPage("", page, 0, nullptr, nullptr);
    }
    page = anchorPage;
    indexInPage = 0;
    occurrences = 0;

    if (*text == 0) {
        gtk_label_set_text(GTK_LABEL(lbSearchState), "");
        setSearchState(true);
        return;
    }

    for (size_t i = 0; i < pageCount; i++) {
        const size_t p = (anchorPage + i) % pageCount;
        XojPdfRectangle matchRect = XojPdfRectangle();
        size_t found = 0;
        if (control->searchTextOnPage(text, p, 1, &found, &matchRect) && found > 0) {
            page = p;
            indexInPage = 1;
            occurrences = found;
            if (!searchActive) {
                control->getNavigationHistory()->recordNavPoint();
                searchActive = true;
            }
            control->getScrollHandler()->scrollToPage(page, matchRect);
            gtk_label_set_text(GTK_LABEL(lbSearchState),
                               (occurrences == 1 ? FC(_F("Text found once on page {1}") % (page + 1)) :
                                                   FC(_F("Text found {1} times on page {2} ({3} of {1})") %
                                                      occurrences % (page + 1) % indexInPage)));
            setSearchState(true);
            return;
        }
    }

    gtk_label_set_text(GTK_LABEL(lbSearchState), _("Text not found"));
    setSearchState(false);
}

void SearchBar::searchTextChangedCallback(GtkSearchEntry* entry, SearchBar* searchBar) {
    searchBar->searchFromAnchor(gtk_editable_get_text(GTK_EDITABLE(entry)));
    // Grab focus again since scrolling to another page may take it away
    gtk_widget_grab_focus(GTK_WIDGET(entry));
}

void SearchBar::buttonCloseSearchClicked(GtkButton* button, SearchBar* searchBar) { searchBar->showSearchBar(false); }

template <class Fun>
void SearchBar::search(Fun next) {

    MainWindow* win = control->getWindow();
    GtkWidget* searchTextField = win->get("searchTextField");
    const char* text = gtk_editable_get_text(GTK_EDITABLE(searchTextField));
    GtkWidget* lbSearchState = win->get("lbSearchState");
    if (*text == 0) {
        return;
    }
    const size_t originalPage = page;

    XojPdfRectangle matchRect = XojPdfRectangle();
    // Search backwards through the pages, wrapping around if needed.
    for (;;) {
        next(text);
        const bool found = control->searchTextOnPage(text, page, indexInPage, &occurrences, &matchRect);

        if (found) {
            if (!searchActive) {
                control->getNavigationHistory()->recordNavPoint();
                searchActive = true;
            }
            control->getScrollHandler()->scrollToPage(page, matchRect);
            setSearchState(true);
            // Refining the text from here on continues from this match
            anchorPage = page;
            anchorValid = true;
            gtk_label_set_text(GTK_LABEL(lbSearchState),
                               (occurrences == 1 ? FC(_F("Text found once on page {1}") % (page + 1)) :
                                                   FC(_F("Text found {1} times on page {2} ({3} of {1})") %
                                                      occurrences % (page + 1) % indexInPage)));
            return;
        }
        if (page == originalPage) {
            gtk_label_set_text(GTK_LABEL(lbSearchState), _("Text not found, searched on all pages"));
            setSearchState(false);
            return;
        }
    }
}

void SearchBar::searchNext() {
    size_t pageCount = control->getDocument()->getPageCount();
    search([&](const char* text) {
        indexInPage++;
        if (indexInPage > occurrences) {
            control->searchTextOnPage(text, page, 1, &occurrences, nullptr);  // clear the active marker
            page++;
            if (page >= pageCount) {
                page = 0;
            }
            indexInPage = 1;
        }
    });
}

void SearchBar::searchPrevious() {
    size_t pageCount = control->getDocument()->getPageCount();
    search([&](const char* text) {
        indexInPage--;
        if (indexInPage == 0 || indexInPage >= occurrences) {
            control->searchTextOnPage(text, page, 1, &occurrences, nullptr);  // clear the active marker
            page--;
            if (page > pageCount) {
                page = pageCount - 1;
            }
            control->searchTextOnPage(text, page, 1, &occurrences, nullptr);
            indexInPage = occurrences;
        }
    });
}

void SearchBar::showSearchBar(bool show) {
    MainWindow* win = control->getWindow();
    GtkWidget* searchBar = win->get("searchBar");

    if (show) {
        GtkWidget* searchTextField = win->get("searchTextField");
        gtk_widget_set_visible(searchBar, true);
        gtk_widget_grab_focus(searchTextField);
        this->indexInPage = 0;
        this->anchorValid = false;  // Start searching from the current page
        this->searchActive = false;
    } else {
        searchActive = false;
        gtk_widget_set_visible(searchBar, false);
        const size_t pageCount = control->getDocument()->getPageCount();
        for (size_t i = pageCount - 1; i < pageCount; i--) {
            control->searchTextOnPage("", i, 0, nullptr, nullptr);
        }
    }
}
