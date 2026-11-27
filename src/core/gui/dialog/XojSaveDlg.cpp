#include "XojSaveDlg.h"

#include <memory>   // for unique_ptr
#include <utility>  // for move

#include "control/settings/Settings.h"
#include "util/PathUtil.h"          // for fromGFile, toGFile
#include "util/i18n.h"              // for _
#include "util/raii/GObjectSPtr.h"  // for GObjectSPtr

#include "FileChooserFiltersHelper.h"

using Callback = xoj::util::move_only_function<void(std::optional<fs::path>)>;

void xoj::SaveExportDialog::show(GtkWindow* parent, Settings* settings, fs::path suggestedPath, const char* windowTitle,
                                 const char* buttonLabel, const FileType* filetype, Callback callback) {
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, windowTitle);
    gtk_file_dialog_set_accept_label(dialog, buttonLabel);
    gtk_file_dialog_set_modal(dialog, true);

    if (filetype) {
        suggestedPath.replace_extension(filetype->extension);
        xoj::util::GObjectSPtr<GListStore> filters(xoj::newFilterList(), xoj::util::adopt);
        xoj::addFilterForFile(filters.get(), *filetype);
        gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters.get()));
    }

    if (!suggestedPath.empty()) {
        if (suggestedPath.has_parent_path()) {
            gtk_file_dialog_set_initial_folder(dialog, Util::toGFile(suggestedPath.parent_path()).get());
        }
        gtk_file_dialog_set_initial_name(dialog, Util::toGFilename(suggestedPath.filename()).c_str());
    } else if (settings && !settings->getLastSavePath().empty()) {
        gtk_file_dialog_set_initial_folder(dialog, Util::toGFile(settings->getLastSavePath()).get());
    }

    // The callback travels through the async call on the heap and is freed when the dialog finishes
    auto* data = new Callback(std::move(callback));
    gtk_file_dialog_save(
            dialog, parent, nullptr,
            +[](GObject* source, GAsyncResult* res, gpointer d) {
                std::unique_ptr<Callback> cb(static_cast<Callback*>(d));
                auto* dlg = GTK_FILE_DIALOG(source);
                GError* err = nullptr;
                GFile* file = gtk_file_dialog_save_finish(dlg, res, &err);  // full reference or null
                if (file) {
                    xoj::util::GObjectSPtr<GFile> owned(file, xoj::util::adopt);
                    (*cb)(Util::fromGFile(owned.get()));
                } else {
                    if (err && !g_error_matches(err, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
                        g_warning("Save dialog failed: %s", err->message);
                    }
                    (*cb)(std::nullopt);
                }
                g_clear_error(&err);
                g_object_unref(dlg);  // Drop the reference held since show()
            },
            data);
}

void xoj::SaveExportDialog::showSaveFileDialog(GtkWindow* parent, Settings* settings, fs::path suggestedPath,
                                               Callback callback) {
    show(parent, settings, std::move(suggestedPath), _("Save File"), _("Save"), &xoj::FileTypes::XOPP,
         std::move(callback));
}

void xoj::SaveExportDialog::showExportFileDialog(GtkWindow* parent, Settings* settings, fs::path suggestedPath,
                                                 const FileType& filetype, Callback callback) {
    show(parent, settings, std::move(suggestedPath), _("Export File"), _("Export"), &filetype, std::move(callback));
}
