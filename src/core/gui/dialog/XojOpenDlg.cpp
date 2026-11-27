#include "XojOpenDlg.h"

#include <memory>   // for unique_ptr
#include <utility>  // for move

#include "control/settings/Settings.h"  // for Settings
#include "util/PathUtil.h"              // for fromGFile, toGFile
#include "util/i18n.h"                  // for _
#include "util/raii/GObjectSPtr.h"      // for GObjectSPtr

#include "FileChooserFiltersHelper.h"

namespace {

/// What a GtkFileDialog should pick
enum class Pick { FILE, FOLDER };

/**
 * Shows a GtkFileDialog and calls `callback` with the chosen path. Nothing is called if the user cancels.
 * @param filters A GListStore of GtkFileFilter (may be null); the first one is the default filter
 * @param initialFolder Folder to start in (may be empty)
 */
void showFileDialog(GtkWindow* parent, Pick pick, const char* title, GListStore* filters, const fs::path& initialFolder,
                    std::function<void(fs::path)> callback) {
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, title);
    gtk_file_dialog_set_modal(dialog, true);
    gtk_file_dialog_set_accept_label(dialog, pick == Pick::FILE ? _("_Open") : _("Select folder"));
    if (filters && g_list_model_get_n_items(G_LIST_MODEL(filters)) > 0) {
        gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
        xoj::util::GObjectSPtr<GtkFileFilter> first(GTK_FILE_FILTER(g_list_model_get_item(G_LIST_MODEL(filters), 0)),
                                                    xoj::util::adopt);
        gtk_file_dialog_set_default_filter(dialog, first.get());
    }
    if (!initialFolder.empty()) {
        gtk_file_dialog_set_initial_folder(dialog, Util::toGFile(initialFolder).get());
    }

    // The callback travels through the async call on the heap and is freed when the dialog finishes
    auto* data = new std::function<void(fs::path)>(std::move(callback));
    auto onFinish = +[](GObject* source, GAsyncResult* res, gpointer d) {
        std::unique_ptr<std::function<void(fs::path)>> cb(static_cast<std::function<void(fs::path)>*>(d));
        auto* dlg = GTK_FILE_DIALOG(source);
        GError* err = nullptr;
        // Both finish functions return a full reference (or null on cancel/error)
        GFile* file = g_object_get_data(source, "xopp-pick-folder") ?
                              gtk_file_dialog_select_folder_finish(dlg, res, &err) :
                              gtk_file_dialog_open_finish(dlg, res, &err);
        if (file) {
            xoj::util::GObjectSPtr<GFile> owned(file, xoj::util::adopt);
            (*cb)(Util::fromGFile(owned.get()));
        } else if (err && !g_error_matches(err, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
            g_warning("File dialog failed: %s", err->message);
        }
        g_clear_error(&err);
        g_object_unref(dlg);  // Drop the reference held since showFileDialog()
    };
    if (pick == Pick::FOLDER) {
        g_object_set_data(G_OBJECT(dialog), "xopp-pick-folder", GINT_TO_POINTER(1));
        gtk_file_dialog_select_folder(dialog, parent, nullptr, onFinish, data);
    } else {
        gtk_file_dialog_open(dialog, parent, nullptr, onFinish, data);
    }
}

/// The folder an "open" dialog starts in: the last folder a file was opened from, or the home folder
fs::path lastOpenFolder(Settings* settings) {
    if (settings && !settings->getLastOpenPath().empty()) {
        return settings->getLastOpenPath();
    }
    return Util::fromGFilename(g_get_home_dir());
}

/// Wraps `callback` so the folder of the opened file is remembered as the last open folder
template <class... Args>
std::function<void(fs::path, Args...)> rememberLastOpenFolder(std::function<void(fs::path, Args...)> callback,
                                                              Settings* settings) {
    return [cb = std::move(callback), settings](fs::path path, Args... args) {
        if (settings && !path.empty()) {
            settings->setLastOpenPath(path.parent_path());
        }
        cb(std::move(path), std::forward<Args>(args)...);
    };
}

/// Owning handle for a new filter list
using FilterList = xoj::util::GObjectSPtr<GListStore>;
FilterList makeFilterList() { return FilterList(xoj::newFilterList(), xoj::util::adopt); }

}  // namespace

void xoj::OpenDlg::showOpenTemplateDialog(GtkWindow* parent, Settings* settings,
                                          std::function<void(fs::path)> callback) {
    auto filters = makeFilterList();
    xoj::addFilterXopt(filters.get());
    xoj::addFilterAllFiles(filters.get());
    showFileDialog(parent, Pick::FILE, _("Open template file"), filters.get(), lastOpenFolder(settings),
                   rememberLastOpenFolder(std::move(callback), settings));
}

void xoj::OpenDlg::showOpenFileDialog(GtkWindow* parent, Settings* settings, std::function<void(fs::path)> callback) {
    auto filters = makeFilterList();
    xoj::addFilterSupported(filters.get());
    xoj::addFilterXoj(filters.get());
    xoj::addFilterXopt(filters.get());
    xoj::addFilterXopp(filters.get());
    xoj::addFilterPdf(filters.get());
    xoj::addFilterSba(filters.get());
    xoj::addFilterAllFiles(filters.get());
    showFileDialog(parent, Pick::FILE, _("Open file"), filters.get(), lastOpenFolder(settings),
                   rememberLastOpenFolder(std::move(callback), settings));
}

void xoj::OpenDlg::showAnnotatePdfDialog(GtkWindow* parent, Settings* settings,
                                         std::function<void(fs::path, bool)> callback) {
    auto filters = makeFilterList();
    xoj::addFilterPdf(filters.get());
    xoj::addFilterAllFiles(filters.get());
    // GtkFileDialog can't show an "attach" checkbox: the choice is a setting (Preferences > Load / Save)
    auto withAttach = [cb = rememberLastOpenFolder(std::move(callback), settings), settings](fs::path path) {
        cb(std::move(path), settings->isAttachBackgroundFiles());
    };
    showFileDialog(parent, Pick::FILE, _("Annotate Pdf file"), filters.get(), lastOpenFolder(settings),
                   std::move(withAttach));
}

void xoj::OpenDlg::showOpenImageDialog(GtkWindow* parent, Settings* settings, bool addAttachQuestion,
                                       std::function<void(fs::path, bool)> callback) {
    auto filters = makeFilterList();
    xoj::addFilterImages(filters.get());
    xoj::addFilterAllFiles(filters.get());
    // GtkFileDialog can't show an "attach" checkbox: for backgrounds, the choice is a setting
    auto withAttach = [cb = std::move(callback), settings, addAttachQuestion](fs::path path) {
        if (auto folder = path.parent_path(); !folder.empty()) {
            settings->setLastImagePath(folder);
        }
        cb(std::move(path), addAttachQuestion && settings->isAttachBackgroundFiles());
    };
    showFileDialog(parent, Pick::FILE, _("Choose image file"), filters.get(), settings->getLastImagePath(),
                   std::move(withAttach));
}

void xoj::OpenDlg::showMultiFormatDialog(GtkWindow* parent, std::vector<std::string> formats,
                                         std::function<void(fs::path)> callback) {
    auto filters = makeFilterList();
    if (!formats.empty()) {
        GtkFileFilter* filterSupported = gtk_file_filter_new();
        gtk_file_filter_set_name(filterSupported, _("Supported files"));
        for (const std::string& format: formats) {
            gtk_file_filter_add_pattern(filterSupported, format.c_str());
        }
        g_list_store_append(filters.get(), filterSupported);
        g_object_unref(filterSupported);
    }
    showFileDialog(parent, Pick::FILE, _("Open file"), filters.get(), fs::path(), std::move(callback));
}

void xoj::OpenDlg::showOpenTexDialog(GtkWindow* parent, const fs::path& preset,
                                     std::function<void(fs::path)> callback) {
    auto filters = makeFilterList();
    xoj::addFilterTex(filters.get());
    xoj::addFilterAllFiles(filters.get());
    showFileDialog(parent, Pick::FILE, _("Choose Latex template file"), filters.get(), preset.parent_path(),
                   std::move(callback));
}

void xoj::OpenDlg::showSelectFolderDialog(GtkWindow* parent, const char* title, const fs::path& preset,
                                          std::function<void(fs::path)> callback) {
    showFileDialog(parent, Pick::FOLDER, title, nullptr, preset, std::move(callback));
}
