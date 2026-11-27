/*
 * Xournal++
 *
 * The application name shown to the user
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>
#include <string_view>

#include <gtk/gtk.h>  // for GtkBuilder

namespace xoj::util {
/// @return The application name shown to the user (XOPP_APP_NAME, e.g. "Xournal++" or "Xournal#")
const char* appName();

/// @return Whether this build shows another name than "Xournal++" (e.g. Xournal#)
bool isRebranded();

/**
 * @return The text with "Xournal++" replaced by the application name.
 * For rebranded builds: messages and translations keep saying "Xournal++", this turns them into the shown name.
 * Do not use it for file format names ("Xournal++ files"), which keep the original name.
 */
std::string withAppName(std::string_view text);

/**
 * Replaces "Xournal++" by the application name in the window titles, labels, button labels and tooltips of all
 * objects of a GtkBuilder (no-op unless the application name differs).
 */
void applyAppName(GtkBuilder* builder);
}  // namespace xoj::util
