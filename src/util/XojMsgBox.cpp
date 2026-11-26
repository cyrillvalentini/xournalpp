#include "util/XojMsgBox.h"

#include <utility>  // for move

#include <glib-object.h>  // for g_object_set_property, g_value_init, g_valu...
#include <glib.h>         // for g_free, g_markup_escape_text, g_error_free

#include <memory>  // for unique_ptr
#include "util/Util.h"
#include "util/i18n.h"  // for _, FS, _F
#include "util/raii/CStringWrapper.h"

#ifdef _WIN32
// Needed for help dialog workaround on Windows; see XojMsgBox::showHelp
#include <windows.h>
// <windows.h> must be included first
#include <shellapi.h>  // for ShellExecute
#endif

GtkWindow* defaultWindow = nullptr;

namespace {
/// GtkAlertDialog shows plain text: drop Pango markup (e.g. "<b>Error in </b>...")
std::string stripMarkup(std::string_view markup) {
    char* text = nullptr;
    if (pango_parse_markup(markup.data(), static_cast<int>(markup.size()), 0, nullptr, &text, nullptr, nullptr)) {
        std::string res(text);
        g_free(text);
        return res;
    }
    return std::string(markup);
}

/**
 * Shows a modal GtkAlertDialog and calls callback(response) with the chosen button's response id, or
 * GTK_RESPONSE_DELETE_EVENT if the dialog was dismissed without a button. A button with GTK_RESPONSE_CANCEL (if any)
 * is used for Escape; the first button is the default one.
 */
void showAlert(GtkWindow* parent, const std::string& message, const std::string& detail,
               const std::vector<XojMsgBox::Button>& buttons, xoj::util::move_only_function<void(int)> callback) {
    if (parent == nullptr) {
        parent = defaultWindow;
    }
    GtkAlertDialog* dialog = gtk_alert_dialog_new("%s", message.c_str());
    if (!detail.empty()) {
        gtk_alert_dialog_set_detail(dialog, detail.c_str());
    }
    gtk_alert_dialog_set_modal(dialog, true);

    std::vector<const char*> labels;
    std::vector<int> responses;
    for (size_t i = 0; i < buttons.size(); i++) {
        labels.push_back(buttons[i].label.c_str());
        responses.push_back(buttons[i].response);
        if (buttons[i].response == GTK_RESPONSE_CANCEL) {
            gtk_alert_dialog_set_cancel_button(dialog, static_cast<int>(i));
        }
    }
    labels.push_back(nullptr);
    gtk_alert_dialog_set_buttons(dialog, labels.data());
    if (!buttons.empty()) {
        gtk_alert_dialog_set_default_button(dialog, 0);
    }

    struct Data {
        std::vector<int> responses;
        xoj::util::move_only_function<void(int)> callback;
    };
    auto* data = new Data{std::move(responses), std::move(callback)};
    gtk_alert_dialog_choose(
            dialog, parent, nullptr,
            +[](GObject* src, GAsyncResult* res, gpointer d) {
                std::unique_ptr<Data> data(static_cast<Data*>(d));
                int index = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(src), res, nullptr);
                int response = index >= 0 && static_cast<size_t>(index) < data->responses.size() ?
                                       data->responses[static_cast<size_t>(index)] :
                                       GTK_RESPONSE_DELETE_EVENT;
                g_object_unref(src);
                // Called after the dialog has closed, so the callback may open another dialog
                data->callback(response);
            },
            data);
}
}  // namespace

/**
 * Set window for messages without window
 */
void XojMsgBox::setDefaultWindow(GtkWindow* win) { defaultWindow = win; }

void XojMsgBox::showMarkupMessageToUser(GtkWindow* win, const std::string_view& markupTitle, const std::string& msg,
                                        GtkMessageType) {
    showAlert(win, stripMarkup(markupTitle), msg, {Button(_("OK"), GTK_RESPONSE_OK)}, [](int) {});
}

void XojMsgBox::showMessageToUser(GtkWindow* win, const std::string& title, const std::string& msg,
                                  GtkMessageType type) {
    auto escapedTitle = xoj::util::OwnedCString::assumeOwnership(g_markup_escape_text(title.c_str(), -1));
    showMarkupMessageToUser(win, escapedTitle.get(), msg, type);
}

void XojMsgBox::showMessageToUser(GtkWindow* win, const std::string& msg, GtkMessageType type) {
    showMessageToUser(win, msg, std::string(), type);
}

void XojMsgBox::showErrorToUser(GtkWindow* win, const std::string& msg) {
    showMessageToUser(win, msg, GTK_MESSAGE_ERROR);
    g_warning("%s", msg.c_str());
}

void XojMsgBox::askQuestion(GtkWindow* win, const std::string& maintext, const std::string& secondarytext,
                            const std::vector<Button>& buttons, xoj::util::move_only_function<void(int)> callback) {
    showAlert(win, maintext, secondarytext, buttons, std::move(callback));
}

void XojMsgBox::askQuestionWithMarkup(GtkWindow* win, std::string_view maintext, const std::string& secondarytext,
                                      const std::vector<Button>& buttons,
                                      xoj::util::move_only_function<void(int)> callback) {
    showAlert(win, stripMarkup(maintext), secondarytext, buttons, std::move(callback));
}

void XojMsgBox::showErrorAndQuit(std::string& msg, int exitCode) {
    // We use a hack to ensure the app does not exit until the user has read the error message and closed the popup
    bool done = false;
    showAlert(defaultWindow, stripMarkup(msg), std::string(), {Button(_("OK"), GTK_RESPONSE_OK)},
              [&done](int) { done = true; });

    while (!done) {
        // Let the main loop run so the popup pops up and can be interacted with
        g_main_context_iteration(g_main_context_default(), true);
    }

    exit(exitCode);
}

void XojMsgBox::showPluginMessage(const std::string& pluginName, const std::string& msg, bool error) {
    auto header = std::string("Xournal++ Plugin «") + pluginName + "»";
    auto escapedHeader = xoj::util::OwnedCString::assumeOwnership(g_markup_escape_text(header.c_str(), -1));
    header = (error ? std::string("<b>Error in </b>") : "") + escapedHeader.get();

    showMarkupMessageToUser(nullptr, header, msg, error ? GTK_MESSAGE_ERROR : GTK_MESSAGE_INFO);
}

void XojMsgBox::replaceFileQuestion(GtkWindow* win, fs::path file,
                                    xoj::util::move_only_function<void(const fs::path&)> writeTofile) {
    if (!fs::exists(file)) {
        writeTofile(file);
        return;
    }

    showAlert(win,
              FS(_F("The file {1} already exists! Do you want to replace it?") % file.filename().u8string()),
              std::string(),
              {Button(_("Select another name"), GTK_RESPONSE_CANCEL), Button(_("Replace"), GTK_RESPONSE_OK)},
              [overwrite = std::move(writeTofile), file = std::move(file)](int response) mutable {
                  if (response == GTK_RESPONSE_OK) {
                      overwrite(file);
                  }
              });
}

constexpr auto* XOJ_HELP = "https://xournalpp.github.io/community/help/";

void XojMsgBox::showHelp(GtkWindow* win) { openURL(win, XOJ_HELP); }

void XojMsgBox::openURL(GtkWindow* win, const char* url) {
#ifdef _WIN32
    // gvfs is not in MSYS repositories, so we can't use gtk_show_uri.
    // Instead, we use the native API.
    ShellExecute(nullptr, "open", url, nullptr, nullptr, SW_SHOW);
#else
#if GTK_CHECK_VERSION(4, 10, 0)
    GtkUriLauncher* launcher = gtk_uri_launcher_new(url);
    gtk_uri_launcher_launch(launcher, win, nullptr, nullptr, nullptr);
    g_object_unref(launcher);
#else
    gtk_show_uri(win, url, GDK_CURRENT_TIME);
#endif
#endif
}
