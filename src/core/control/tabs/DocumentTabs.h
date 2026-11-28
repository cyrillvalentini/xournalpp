/*
 * Xournal++
 *
 * Several open documents in one window, shown as tabs
 *
 * The window has a single document view (Control's document, page view, sidebar...). The tabs keep the other open
 * documents in memory: their pages, undo/redo history and view position. Switching tabs swaps that state into the
 * single view; the pages are shared, not copied, so this is quick even for large documents.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>  // for function
#include <memory>      // for unique_ptr
#include <set>         // for set
#include <string>      // for string
#include <vector>      // for vector

#include <adwaita.h>

#include "model/DocumentHandler.h"  // for DocumentHandler
#include "undo/UndoRedoHandler.h"   // for UndoRedoHandler::Snapshot

#include "filesystem.h"  // for path

class Control;
class Document;

class DocumentTabs {
public:
    explicit DocumentTabs(Control* control);
    ~DocumentTabs();

    DocumentTabs(const DocumentTabs&) = delete;
    DocumentTabs& operator=(const DocumentTabs&) = delete;

    /// Connects the tab bar of the main window. The document currently in the view becomes the first tab.
    void attach(AdwTabBar* bar, AdwTabView* view);

    /// Number of open documents
    size_t count() const;

    /**
     * Whether the active tab shows a new document nobody has worked on yet. Opening a file then replaces it instead of
     * adding a tab.
     */
    bool isActiveTabUnused() const;

    /// Switches to the tab that has this file open. Returns false if no tab has it.
    bool selectTabWithFile(const fs::path& file);

    /**
     * Keeps the active document in its tab and adds a new, active tab with an empty view. The caller then loads a
     * document into the view (e.g. with Control::replaceDocument). If that fails, call cancelNewTab().
     */
    void addTab();
    /// Removes the tab added by addTab() and shows the previous one again
    void cancelNewTab();

    /// Updates the title, unsaved indicator and tooltip of the active tab (call when the document or its state changes)
    void updateActiveTab();

    /**
     * The file the active document was opened from. For imported files (PDF, Saber notes, images) it names the tab and
     * window until the document is saved.
     */
    void setActiveSource(fs::path source);
    /// The file the active document is shown as: its own file, else the file it was opened from, else its PDF
    fs::path getActiveDisplayPath() const;

    /// Asks to close the active tab (saving it if needed), like the close button of the tab
    void closeActiveTab();
    void selectNextTab();
    void selectPreviousTab();

    /**
     * Asks about every document with unsaved changes, one after the other (switching to its tab), before the window
     * closes. Calls done(true) if all were saved or discarded, done(false) if the user cancelled.
     */
    void confirmCloseAll(bool allowCancel, std::function<void(bool)> done);

    /// The documents open when the window was closed last time (see saveSession)
    struct Session {
        struct Entry {
            fs::path file;
            size_t page = 0;
            bool draft = false;  ///< file is the draft of a document that was never saved (after a crash)
            fs::path source;     ///< for a draft: the file the document was opened from, if any
            fs::path original;   ///< for a draft: the document's own file, if it had one
        };
        std::vector<Entry> entries;
        size_t active = 0;  ///< index in entries
    };
    /**
     * Remembers the open documents that exist as files (saved documents, and PDFs or other files opened directly), so
     * that they open again at the next start. New documents that were never saved are left out.
     */
    void saveSession();
    /**
     * Keeps the saved session as it is from now on. Called when the window closes, after the last saveSession(): the
     * documents are cleared afterwards, which would otherwise look like unsaved documents.
     */
    void freezeSession();

    /// Deletes the drafts of all tabs (when the window closes, after every document was saved or discarded)
    void discardAllDrafts();

    /// Drafts on or off, and how often (in seconds) the active document's draft is written: the autosave setting
    void configureDrafts(bool enabled, int seconds);
    /// The session saved last time, without the files that do not exist anymore
    static Session loadSession();
    /**
     * Opens the documents of a session, one after the other (each at its page), selects the active one and calls done.
     * The session is not saved meanwhile, so that a crash while restoring does not lose it.
     */
    void restoreSession(Session session, std::function<void()> done);

private:
    struct Tab;

    Tab* findTab(AdwTabPage* page) const;
    Tab* activeTab() const;
    void appendTab(std::unique_ptr<Tab> tab, AdwTabPage* after);

    /// Moves the state of the view (document, undo history, position) into the active tab
    void stashActive();
    /// Shows the given (stashed) tab in the view
    void load(Tab* tab);
    /// Scrolls the view to the given position (see Tab), once the pages are laid out
    void restorePosition(size_t pageNo, double offsetX, double offsetY);
    /// Removes a tab whose closing was confirmed
    void removeTab(Tab* tab);

    void updateTab(Tab* tab, const Document& doc, bool changed);

    /**
     * Drafts (the autosave): while a document has unsaved changes, they are written to a draft in the data folder, so
     * that they survive a crash; after a crash, the documents open again with their changes. The draft is deleted
     * when the document is saved, or closed.
     */
    void saveActiveDraft();
    void deleteDraft(Tab* tab);
    /// After a crash: turns the opened draft back into the unsaved document it was
    void adoptRestoredDraft(const fs::path& draft, const fs::path& source, const fs::path& original);
    void updateBarVisibility();

    static void onSelectedPageChanged(AdwTabView* view, GParamSpec*, DocumentTabs* self);
    static gboolean onClosePage(AdwTabView* view, AdwTabPage* page, DocumentTabs* self);

    void confirmNext(bool allowCancel, std::shared_ptr<std::set<Tab*>> handled, std::function<void(bool)> done);

    Control* control;
    /// Receives the change events of the stashed documents (nobody listens to them)
    DocumentHandler stashHandler;

    AdwTabBar* bar = nullptr;
    AdwTabView* view = nullptr;

    std::vector<std::unique_ptr<Tab>> tabs;
    Tab* active = nullptr;
    /// Set while the tabs are changed from the code, so that the selection signal is ignored
    bool updating = false;
    /// Set while a session is restored: saveSession() does nothing
    bool restoring = false;
    /// See freezeSession(); also set while a closing tab's document is cleared
    bool sessionFrozen = false;
    guint draftTimer = 0;
    bool draftsEnabled = true;
};
