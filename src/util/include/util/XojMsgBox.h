/*
 * Xournal++
 *
 * Xournal util functions
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "util/move_only_function.h"

#include "filesystem.h"

class XojMsgBox final {
public:
    struct Button {
        /// How the button is drawn (libadwaita response appearance)
        enum Style { DEFAULT, SUGGESTED, DESTRUCTIVE };
        Button(std::string l, int r, Style s = DEFAULT): label(std::move(l)), response(r), style(s) {}
        std::string label;
        int response;
        Style style;
    };

    /**
     * Set window for messages without window
     */
    static void setDefaultWindow(GtkWindow* win);

    /// @param extraChild shown below the text (e.g. the file a question is about); the dialog takes ownership
    static void askQuestion(GtkWindow* win, const std::string& maintext, const std::string& secondarytext,
                            const std::vector<Button>& buttons, xoj::util::move_only_function<void(int)> callback,
                            GtkWidget* extraChild = nullptr);
    /**
     * @brief same as askQuestion() but the string maintext is not escaped for Pango markups
     */
    static void askQuestionWithMarkup(GtkWindow* win, std::string_view maintext, const std::string& secondarytext,
                                      const std::vector<Button>& buttons,
                                      xoj::util::move_only_function<void(int)> callback);

    /**
     * @brief Shows a message with title markupTitle and message content msg.
     * The title is formatted according to any Pango markups it contains.
     */
    static void showMarkupMessageToUser(GtkWindow* win, const std::string_view& markupTitle, const std::string& msg,
                                        GtkMessageType type);
    static void showMessageToUser(GtkWindow* win, const std::string& msg, GtkMessageType type);
    static void showMessageToUser(GtkWindow* win, const std::string& title, const std::string& msg,
                                  GtkMessageType type);
    static void showErrorToUser(GtkWindow* win, const std::string& msg);

    /// @brief This should be used for fatal errors, typically in early GUI startup (missing UI main file or so).
    [[noreturn]] static void showErrorAndQuit(std::string& msg, int exitCode);

    static void showPluginMessage(const std::string& pluginName, const std::string& msg, bool error = false);
    static void showHelp(GtkWindow* win);
    static void openURL(GtkWindow* win, const char* url);

    /**
     * @brief Calls writeToFile(file) if either file is not already present in the filesystem, or is the user answers
     * "Overwrite" to a popup dialog.
     */
    static void replaceFileQuestion(GtkWindow* win, fs::path file,
                                    xoj::util::move_only_function<void(const fs::path&)> writeToFile);
};
