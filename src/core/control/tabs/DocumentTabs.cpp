#include "DocumentTabs.h"

#include <algorithm>  // for find_if
#include <utility>    // for move

#include "control/Control.h"
#include "control/ScrollHandler.h"
#include "control/NavigationHistory.h"
#include "control/settings/MetadataManager.h"
#include "control/RecentManager.h"
#include "control/xojfile/SaveHandler.h"
#include "control/zoom/ZoomControl.h"
#include "gui/Layout.h"
#include "gui/MainWindow.h"
#include "gui/XournalView.h"
#include "model/Document.h"
#include "undo/EmergencySaveRestore.h"
#include "util/PathUtil.h"
#include "util/StringUtils.h"
#include "util/i18n.h"

struct DocumentTabs::Tab {
    AdwTabPage* page = nullptr;

    /// While the tab is not active: its document, undo history and position in the view
    std::unique_ptr<Document> document;
    UndoRedoHandler::SnapshotPtr undo;
    /// The view position: the top left corner of the visible area, relative to page pageNo (in page coordinates),
    /// so that it stays right if the window size changed in the meantime
    size_t pageNo = 0;
    double offsetX = 0;
    double offsetY = 0;
    double zoom = 1.0;
    bool stashed = false;

    /// Kept up to date for the tab bar and for checks while the tab is not active
    fs::path filepath;
    /// The file the document was opened from (see setActiveSource)
    fs::path source;
    bool changed = false;

    /// While the document has unsaved changes: the draft that keeps them safe in a crash, and the undo revision it was
    /// last written at (see saveActiveDraft)
    fs::path draft;
    uint64_t draftRevision = 0;
};

static fs::path draftFolder() { return Util::getDataSubfolder("drafts"); }

DocumentTabs::DocumentTabs(Control* control): control(control) {}

DocumentTabs::~DocumentTabs() {
    if (draftTimer) {
        g_source_remove(draftTimer);
    }
    if (view) {
        g_signal_handlers_disconnect_by_data(view, this);
        g_object_unref(view);
    }
}

void DocumentTabs::attach(AdwTabBar* bar, AdwTabView* view) {
    this->bar = bar;
    this->view = view;
    g_object_ref(view);  // kept alive until the signal handlers are disconnected in the destructor

    // The shortcuts are actions of the window (close-tab, next-tab, previous-tab), so that they also work when the
    // document view has the focus
    adw_tab_view_set_shortcuts(view, ADW_TAB_VIEW_SHORTCUT_NONE);

    // "+" at the end of the tab bar, like in GNOME Text Editor and web browsers
    GtkWidget* newButton = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_add_css_class(newButton, "flat");
    gtk_widget_set_tooltip_text(newButton, _("New Document"));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(newButton), "win.new-file");
    adw_tab_bar_set_end_action_widget(bar, newButton);
    g_signal_connect(view, "notify::selected-page", G_CALLBACK(onSelectedPageChanged), this);
    g_signal_connect(view, "close-page", G_CALLBACK(onClosePage), this);

    auto tab = std::make_unique<Tab>();
    appendTab(std::move(tab), nullptr);
    active = tabs.front().get();
    updateActiveTab();
}

void DocumentTabs::configureDrafts(bool enabled, int seconds) {
    if (draftTimer) {
        g_source_remove(draftTimer);
        draftTimer = 0;
    }
    draftsEnabled = enabled;
    if (enabled) {
        draftTimer = g_timeout_add_seconds(
                static_cast<guint>(std::max(1, seconds)),
                +[](gpointer self) -> gboolean {
                    static_cast<DocumentTabs*>(self)->saveActiveDraft();
                    return G_SOURCE_CONTINUE;
                },
                this);
    }
}

void DocumentTabs::deleteDraft(Tab* tab) {
    if (tab->draft.empty()) {
        return;
    }
    std::error_code ec;
    fs::remove(tab->draft, ec);
    // An attached PDF background is written next to it
    fs::path bg = tab->draft;
    bg += ".bg.pdf";
    fs::remove(bg, ec);
    tab->draft.clear();
    tab->draftRevision = 0;
}

void DocumentTabs::saveActiveDraft() {
    Tab* tab = active;
    if (!tab || tab->stashed || sessionFrozen || restoring || !draftsEnabled) {
        return;
    }
    Document* doc = control->doc;
    UndoRedoHandler* undo = control->undoRedo;

    if (!undo->isChanged()) {
        // Saved, or back to the opened state: no draft needed
        if (!tab->draft.empty()) {
            deleteDraft(tab);
            saveSession();
        }
        return;
    }
    if (!tab->draft.empty() && tab->draftRevision == undo->getRevision()) {
        return;  // up to date
    }

    const bool newDraft = tab->draft.empty();
    if (newDraft) {
        std::error_code ec;
        fs::create_directories(draftFolder(), ec);
        GDateTime* now = g_date_time_new_now_local();
        gchar* stamp = g_date_time_format(now, "%Y-%m-%d_%H-%M-%S");
        g_date_time_unref(now);
        fs::path name = std::string("draft-") + stamp + "-" + std::to_string(g_random_int() % 100000) + ".xopp";
        g_free(stamp);
        tab->draft = draftFolder() / name;
    }

    // Written from a copy that has the draft as its file (the pages are shared, not copied): the document itself stays
    // unsaved, and an attached PDF is written next to the draft
    Document copy(&stashHandler);
    copy.lock();
    doc->lock_shared();
    copy = *doc;
    doc->unlock_shared();
    copy.setFilepath(tab->draft);
    SaveHandler handler;
    handler.prepareSave(&copy, tab->draft);
    handler.saveTo(tab->draft);
    copy.unlock();
    if (!handler.getErrorMessage().empty()) {
        g_warning("Could not save the draft %s: %s", char_cast(tab->draft.u8string().c_str()),
                  handler.getErrorMessage().c_str());
        return;
    }
    tab->draftRevision = undo->getRevision();
    if (newDraft) {
        saveSession();  // so that the draft is found after a crash
    }
}

void DocumentTabs::discardAllDrafts() {
    for (auto& t: tabs) {
        deleteDraft(t.get());
    }
}

size_t DocumentTabs::count() const { return tabs.size(); }

auto DocumentTabs::findTab(AdwTabPage* page) const -> Tab* {
    auto it = std::find_if(tabs.begin(), tabs.end(), [page](const auto& t) { return t->page == page; });
    return it == tabs.end() ? nullptr : it->get();
}

auto DocumentTabs::activeTab() const -> Tab* { return active; }

void DocumentTabs::appendTab(std::unique_ptr<Tab> tab, AdwTabPage* after) {
    // AdwTabView needs a child widget for each page. The document view itself is shared by all tabs, so the pages only
    // hold an empty placeholder.
    GtkWidget* placeholder = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    updating = true;
    if (after) {
        tab->page = adw_tab_view_add_page(view, placeholder, after);
    } else {
        tab->page = adw_tab_view_append(view, placeholder);
    }
    adw_tab_view_set_selected_page(view, tab->page);
    updating = false;
    tabs.push_back(std::move(tab));
    updateBarVisibility();
}

void DocumentTabs::updateBarVisibility() {
    // Always shown: the bar also holds the button for a new document
    gtk_widget_set_visible(GTK_WIDGET(bar), true);
}

bool DocumentTabs::isActiveTabUnused() const {
    if (!active || !active->source.empty()) {
        // A document opened from a file (e.g. an imported Saber note, which has no file of its own yet) is in use
        return false;
    }
    Document* doc = control->doc;
    doc->lock_shared();
    const bool noFile = doc->getFilepath().empty() && doc->getPdfFilepath().empty();
    doc->unlock_shared();
    return noFile && !control->undoRedo->isChanged() && !control->undoRedo->canUndo();
}

bool DocumentTabs::selectTabWithFile(const fs::path& file) {
    std::error_code ec;
    updateActiveTab();
    for (const auto& t: tabs) {
        // A document's own file, or the file it was opened from (e.g. a PDF or Saber note not saved yet)
        const fs::path& own = t->filepath.empty() ? t->source : t->filepath;
        if ((!own.empty() && fs::equivalent(own, file, ec)) ||
            (!t->draft.empty() && fs::equivalent(t->draft, file, ec))) {
            if (t.get() != active) {
                adw_tab_view_set_selected_page(view, t->page);  // switches via onSelectedPageChanged
            }
            return true;
        }
    }
    return false;
}

void DocumentTabs::stashActive() {
    Tab* tab = active;
    if (!tab || tab->stashed) {
        return;
    }
    control->clearSelectionEndText();
    control->resetGeometryTool();
    // Changes in a tab that is not shown cannot happen: one last draft now keeps it safe while it waits
    saveActiveDraft();

    Document* doc = control->doc;
    tab->document = std::make_unique<Document>(&stashHandler);
    tab->document->lock();
    doc->lock_shared();
    *tab->document = *doc;  // shares the pages, does not copy them
    doc->unlock_shared();
    tab->document->unlock();

    tab->undo = control->undoRedo->takeSnapshot();
    XournalView* xournal = control->getWindow()->getXournal();
    Layout* layout = xournal->getLayout();
    tab->pageNo = control->getCurrentPageNo();
    tab->zoom = control->zoom->getZoom();
    const auto visible = layout->getVisibleRect();
    const auto pos = layout->getPixelCoordinatesOfEntry(tab->pageNo);
    tab->offsetX = (visible.x - pos.x) / tab->zoom;
    tab->offsetY = (visible.y - pos.y) / tab->zoom;
    tab->stashed = true;
}

void DocumentTabs::load(Tab* tab) {
    xoj_assert(tab->stashed);
    Document* doc = control->doc;

    // The zoom first, so that the pages are laid out with it when the document is shown
    ZoomControl* zoom = control->zoom;
    if (!zoom->isZoomFitMode() && !zoom->isZoomPresentationMode()) {
        zoom->setZoom(tab->zoom);
    }

    doc->lock();
    *doc = *tab->document;  // fires DOCUMENT_CHANGE_COMPLETE: the view, sidebar... show the document
    doc->unlock();
    tab->document.reset();

    control->undoRedo->restoreSnapshot(std::move(tab->undo));
    tab->stashed = false;
    active = tab;

    if (control->navHistory) {
        control->navHistory->reset();
    }
    control->undoRedoChanged();  // also updates the window title and this tab
    control->getWindow()->getXournal()->forceUpdatePagenumbers();
    control->updatePageActions();

    restorePosition(tab->pageNo, tab->offsetX, tab->offsetY);
    saveSession();
}

void DocumentTabs::restorePosition(size_t pageNo, double offsetX, double offsetY) {
    struct Data {
        Control* control;
        size_t pageNo;
        double offsetX;
        double offsetY;
    };
    // After the view has laid out the pages of the document (like Control::loadMetadata)
    g_idle_add_full(
            G_PRIORITY_DEFAULT_IDLE,
            +[](gpointer d) -> gboolean {
                auto* data = static_cast<Data*>(d);
                Control* control = data->control;
                ZoomControl* zoom = control->getZoomControl();
                if (zoom->isZoomFitMode()) {
                    zoom->updateZoomFitValue();
                    zoom->setZoomFitMode(true);
                }
                XournalView* xournal = control->getWindow()->getXournal();
                if (data->pageNo < xournal->getViewPages().size()) {
                    // Select the page first: that may scroll the view to it
                    control->firePageSelected(data->pageNo);
                    Layout* layout = xournal->getLayout();
                    const auto pos = layout->getPixelCoordinatesOfEntry(data->pageNo);
                    const double z = xournal->getZoom();
                    layout->scrollAbs(pos.x + data->offsetX * z, pos.y + data->offsetY * z);
                }
                return G_SOURCE_REMOVE;
            },
            new Data{control, pageNo, offsetX, offsetY}, +[](gpointer d) { delete static_cast<Data*>(d); });
}

void DocumentTabs::addTab() {
    stashActive();
    // The view is empty until the caller loads a document (the stashed document keeps its own copy)
    control->undoRedo->clearContents();

    auto tab = std::make_unique<Tab>();
    Tab* raw = tab.get();
    appendTab(std::move(tab), active ? active->page : nullptr);
    active = raw;
}

void DocumentTabs::cancelNewTab() {
    Tab* failed = active;
    auto it = std::find_if(tabs.begin(), tabs.end(), [failed](const auto& t) { return t.get() == failed; });
    if (it == tabs.end() || tabs.size() < 2) {
        return;
    }
    updating = true;
    adw_tab_view_close_page(view, failed->page);  // no confirmation: onClosePage confirms while updating
    updating = false;
    tabs.erase(it);
    active = nullptr;
    updateBarVisibility();

    if (Tab* selected = findTab(adw_tab_view_get_selected_page(view)); selected && selected->stashed) {
        load(selected);
    }
}

static fs::path displayPath(const Document& doc, const fs::path& source) {
    if (!doc.getFilepath().empty()) {
        return doc.getFilepath();
    }
    return source.empty() ? doc.getPdfFilepath() : source;
}

void DocumentTabs::setActiveSource(fs::path source) {
    if (active) {
        active->source = std::move(source);
    }
}

fs::path DocumentTabs::getActiveDisplayPath() const {
    Document* doc = control->doc;
    doc->lock_shared();
    fs::path p = displayPath(*doc, active ? active->source : fs::path());
    doc->unlock_shared();
    return p;
}

void DocumentTabs::updateTab(Tab* tab, const Document& doc, bool changed) {
    const fs::path ref = displayPath(doc, tab->source);
    const bool fileChanged = tab->filepath != doc.getFilepath();
    tab->filepath = doc.getFilepath();
    tab->changed = changed;

    std::string title = ref.empty() ? std::string(_("Unsaved Document")) : std::string(char_cast(ref.filename().u8string()));
    if (changed) {
        // The usual "unsaved changes" marker of GNOME apps
        title = "• " + title;
    }
    adw_tab_page_set_title(tab->page, title.c_str());
    std::string tooltip = ref.empty() ? std::string(_("Unsaved Document")) : std::string(char_cast(ref.u8string()));
    adw_tab_page_set_tooltip(tab->page, tooltip.c_str());

    if (fileChanged) {
        // Saved under a new name: the draft is not needed anymore (a restored draft that gets its own file back keeps
        // its draft, as it still has unsaved changes)
        if (!tab->filepath.empty() && !changed) {
            deleteDraft(tab);
        }
        saveSession();
    }
}

void DocumentTabs::updateActiveTab() {
    if (!active || active->stashed || !view) {
        return;
    }
    Document* doc = control->doc;
    doc->lock_shared();
    updateTab(active, *doc, control->undoRedo->isChanged());
    doc->unlock_shared();
}

void DocumentTabs::onSelectedPageChanged(AdwTabView* view, GParamSpec*, DocumentTabs* self) {
    if (self->updating) {
        return;
    }
    Tab* tab = self->findTab(adw_tab_view_get_selected_page(view));
    if (!tab || tab == self->active) {
        return;
    }
    self->stashActive();
    self->load(tab);
}

gboolean DocumentTabs::onClosePage(AdwTabView* view, AdwTabPage* page, DocumentTabs* self) {
    if (self->updating) {
        adw_tab_view_close_page_finish(view, page, true);
        return GDK_EVENT_STOP;
    }
    Tab* tab = self->findTab(page);
    if (!tab) {
        adw_tab_view_close_page_finish(view, page, true);
        return GDK_EVENT_STOP;
    }
    // Ask about unsaved changes with the document shown, as the save dialog works on the active document
    if (tab != self->active) {
        adw_tab_view_set_selected_page(view, page);
    }
    self->control->close(
            [self, page](bool closed) {
                Tab* tab = self->findTab(page);
                if (!tab) {
                    return;
                }
                if (!closed) {
                    adw_tab_view_close_page_finish(self->view, page, false);
                    return;
                }
                self->removeTab(tab);
            },
            false);
    return GDK_EVENT_STOP;
}

void DocumentTabs::removeTab(Tab* tab) {
    xoj_assert(tab == active);
    // Closing was confirmed: the document was saved, or its changes discarded
    deleteDraft(tab);
    if (tabs.size() == 1) {
        // The last tab: like closing the document, keep an empty new document
        updating = true;
        adw_tab_view_close_page_finish(view, tab->page, false);
        updating = false;
        sessionFrozen = true;
        control->closeDocument();
        sessionFrozen = false;
        tab->source.clear();
        control->loadNewDocument();
        saveSession();
        return;
    }

    {
        // Clearing the view makes the tab look like an unsaved document: that must not end up in the session
        sessionFrozen = true;
        control->closeDocument();
        sessionFrozen = false;
    }
    active = nullptr;
    AdwTabPage* page = tab->page;
    tabs.erase(std::find_if(tabs.begin(), tabs.end(), [tab](const auto& t) { return t.get() == tab; }));
    updating = true;
    adw_tab_view_close_page_finish(view, page, true);
    updating = false;
    updateBarVisibility();

    if (Tab* selected = findTab(adw_tab_view_get_selected_page(view)); selected && selected->stashed) {
        load(selected);
    }
    saveSession();
}

void DocumentTabs::closeActiveTab() {
    if (active) {
        adw_tab_view_close_page(view, active->page);
    }
}

void DocumentTabs::selectNextTab() {
    if (!adw_tab_view_select_next_page(view) && !tabs.empty()) {
        // wrap around
        adw_tab_view_set_selected_page(view, adw_tab_view_get_nth_page(view, 0));
    }
}

void DocumentTabs::selectPreviousTab() {
    if (!adw_tab_view_select_previous_page(view) && !tabs.empty()) {
        adw_tab_view_set_selected_page(view, adw_tab_view_get_nth_page(view, adw_tab_view_get_n_pages(view) - 1));
    }
}

void DocumentTabs::confirmCloseAll(bool allowCancel, std::function<void(bool)> done) {
    confirmNext(allowCancel, std::make_shared<std::set<Tab*>>(), std::move(done));
}

void DocumentTabs::confirmNext(bool allowCancel, std::shared_ptr<std::set<Tab*>> handled,
                               std::function<void(bool)> done) {
    updateActiveTab();
    // The active document first, then the others in tab order
    Tab* next = nullptr;
    if (active && !handled->count(active) && control->undoRedo->isChanged()) {
        next = active;
    } else {
        const int n = view ? adw_tab_view_get_n_pages(view) : 0;
        for (int i = 0; i < n && !next; i++) {
            Tab* t = findTab(adw_tab_view_get_nth_page(view, i));
            if (t && t != active && !handled->count(t) && UndoRedoHandler::isChanged(t->undo.get())) {
                next = t;
            }
        }
    }
    if (!next) {
        done(true);
        return;
    }
    if (next != active) {
        adw_tab_view_set_selected_page(view, next->page);
    }
    handled->insert(next);
    control->close(
            [this, allowCancel, handled, done = std::move(done)](bool ok) mutable {
                if (!ok) {
                    done(false);
                    return;
                }
                confirmNext(allowCancel, std::move(handled), std::move(done));
            },
            false, allowCancel);
}

static fs::path sessionFile() { return Util::getConfigFile("session.ini"); }

constexpr auto SESSION_GROUP = "Session";

void DocumentTabs::saveSession() {
    if (!view || restoring || sessionFrozen) {
        return;
    }
    updateActiveTab();
    std::vector<std::string> files;
    std::vector<std::string> sources;    ///< for drafts: the file they were opened from (names the tab)
    std::vector<std::string> originals;  ///< for drafts: the document's own file, if it has one
    std::vector<gboolean> drafts;
    std::vector<gint> pages;
    gint activeIndex = 0;
    const int n = adw_tab_view_get_n_pages(view);
    for (int i = 0; i < n; i++) {
        Tab* t = findTab(adw_tab_view_get_nth_page(view, i));
        if (!t) {
            continue;
        }
        // Its draft (unsaved changes); else its own file; else the file it was opened from (a PDF, a Saber note...)
        const bool isDraft = !t->draft.empty();
        const fs::path& file = isDraft ? t->draft : !t->filepath.empty() ? t->filepath : t->source;
        if (file.empty()) {
            continue;  // a new document, never changed
        }
        if (t == active) {
            activeIndex = static_cast<gint>(files.size());
        }
        files.emplace_back(char_cast(file.u8string()));
        sources.emplace_back(isDraft ? char_cast(t->source.u8string()) : "");
        originals.emplace_back(isDraft ? char_cast(t->filepath.u8string()) : "");
        drafts.push_back(isDraft);
        pages.push_back(static_cast<gint>(t == active ? control->getCurrentPageNo() : t->pageNo));
    }

    GKeyFile* kf = g_key_file_new();
    std::vector<const gchar*> list;
    for (const auto& f: files) {
        list.push_back(f.c_str());
    }
    g_key_file_set_string_list(kf, SESSION_GROUP, "files", list.data(), list.size());
    std::vector<const gchar*> sourceList;
    for (const auto& s: sources) {
        sourceList.push_back(s.c_str());
    }
    g_key_file_set_string_list(kf, SESSION_GROUP, "sources", sourceList.data(), sourceList.size());
    std::vector<const gchar*> originalList;
    for (const auto& s: originals) {
        originalList.push_back(s.c_str());
    }
    g_key_file_set_string_list(kf, SESSION_GROUP, "originals", originalList.data(), originalList.size());
    g_key_file_set_boolean_list(kf, SESSION_GROUP, "drafts", drafts.data(), drafts.size());
    g_key_file_set_integer_list(kf, SESSION_GROUP, "pages", pages.data(), pages.size());
    g_key_file_set_integer(kf, SESSION_GROUP, "active", activeIndex);
    GError* err = nullptr;
    if (!g_key_file_save_to_file(kf, char_cast(sessionFile().u8string().c_str()), &err)) {
        g_warning("Could not save the open documents: %s", err->message);
        g_error_free(err);
    }
    g_key_file_free(kf);
}

void DocumentTabs::restoreSession(Session session, std::function<void()> done) {
    restoring = true;
    struct State {
        DocumentTabs* self;
        Session session;
        size_t next = 0;
        std::function<void()> done;
    };
    auto state = std::make_shared<State>(State{this, std::move(session), 0, std::move(done)});

    // Opens entry `next`; once it is loaded and has scrolled to its page (an idle callback queued by the loading),
    // continues with the next one
    auto step = std::make_shared<std::function<void()>>();
    *step = [state, step]() {
        DocumentTabs* self = state->self;
        if (state->next >= state->session.entries.size()) {
            if (!state->session.entries.empty()) {
                self->selectTabWithFile(state->session.entries[state->session.active].file);
            }
            self->restoring = false;
            self->saveSession();
            auto done = std::move(state->done);
            *step = nullptr;  // breaks the reference cycle
            done();
            return;
        }
        const auto& entry = state->session.entries[state->next++];
        self->control->openFile(
                entry.file,
                [step, self, entry](bool ok) {
                    if (ok && entry.draft) {
                        self->adoptRestoredDraft(entry.file, entry.source, entry.original);
                    }
                    // After the idle callbacks of the loading (e.g. scrolling to the page)
                    Util::execWhenIdle([step]() {
                        if (*step) {
                            (*step)();
                        }
                    });
                },
                static_cast<int>(entry.page));
    };
    (*step)();
}

void DocumentTabs::freezeSession() { sessionFrozen = true; }

void DocumentTabs::adoptRestoredDraft(const fs::path& draft, const fs::path& source, const fs::path& original) {
    // The draft was opened like a normal file: make it the document with unsaved changes it was before the crash
    // again (with its own file, if it had one, so that saving writes there)
    Document* doc = control->doc;
    doc->lock();
    doc->setFilepath(original);
    doc->unlock();
    RecentManager::removeRecentFileFilename(draft);
    active->source = source;
    active->draft = draft;
    // Marks the document as changed, so that closing asks to save it (like the emergency save restore)
    control->undoRedo->addUndoAction(std::make_unique<EmergencySaveRestore>());
    active->draftRevision = control->undoRedo->getRevision();
    control->updateWindowTitle();
}

auto DocumentTabs::loadSession() -> Session {
    Session session;
    GKeyFile* kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, char_cast(sessionFile().u8string().c_str()), G_KEY_FILE_NONE, nullptr)) {
        g_key_file_free(kf);
        return session;
    }
    gsize nFiles = 0;
    gchar** files = g_key_file_get_string_list(kf, SESSION_GROUP, "files", &nFiles, nullptr);
    gsize nPages = 0;
    gint* pages = g_key_file_get_integer_list(kf, SESSION_GROUP, "pages", &nPages, nullptr);
    const gint active = g_key_file_get_integer(kf, SESSION_GROUP, "active", nullptr);
    gsize nSources = 0;
    gchar** sources = g_key_file_get_string_list(kf, SESSION_GROUP, "sources", &nSources, nullptr);
    gsize nOriginals = 0;
    gchar** originals = g_key_file_get_string_list(kf, SESSION_GROUP, "originals", &nOriginals, nullptr);
    gsize nDrafts = 0;
    gboolean* drafts = g_key_file_get_boolean_list(kf, SESSION_GROUP, "drafts", &nDrafts, nullptr);

    for (gsize i = 0; i < nFiles; i++) {
        fs::path file(std::u8string(reinterpret_cast<const char8_t*>(files[i])));
        std::error_code ec;
        if (static_cast<gint>(i) == active) {
            session.active = session.entries.size();
        }
        if (!fs::is_regular_file(file, ec)) {
            continue;  // moved or deleted in the meantime
        }
        size_t page = (pages && i < nPages && pages[i] > 0) ? static_cast<size_t>(pages[i]) : 0;
        Session::Entry e{std::move(file), page};
        e.draft = drafts && i < nDrafts && drafts[i];
        if (e.draft && sources && i < nSources && sources[i][0]) {
            e.source = fs::path(std::u8string(reinterpret_cast<const char8_t*>(sources[i])));
        }
        if (e.draft && originals && i < nOriginals && originals[i][0]) {
            e.original = fs::path(std::u8string(reinterpret_cast<const char8_t*>(originals[i])));
        }
        session.entries.push_back(std::move(e));
    }
    g_strfreev(sources);
    g_strfreev(originals);
    g_free(drafts);
    if (session.active >= session.entries.size()) {
        session.active = session.entries.empty() ? 0 : session.entries.size() - 1;
    }
    g_strfreev(files);
    g_free(pages);
    g_key_file_free(kf);
    return session;
}
