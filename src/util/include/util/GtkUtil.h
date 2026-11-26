/*
 * Xournal++
 * Helper function for setting up some GtkWidget
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>

#include <gtk/gtk.h>
#include "util/GtkUtil.h"

namespace xoj::util {
template <typename T>
struct Point;
namespace gtk {
/**
 * @brief Make so a widget is automatically enabled/disabled whenever the given action is
 */
void setWidgetFollowActionEnabled(GtkWidget* w, GAction* a);

/**
 * @brief returns the user-space DPI (i.e. scaled by HiDPI scaling) of the monitor the widget is on.
 * Return std::nullopt if the widget is not realized, or if the monitor reports an invalid physical size
 * (e.g. RDP sessions or monitors with missing EDID data)
 */
std::optional<double> getWidgetDPI(GtkWidget* w);

/// Translate the point from GdkSurface coordinates to widget coordinates
xoj::util::Point<double> gdkSurfaceToWidgetCoordinates(xoj::util::Point<double> p, GtkWidget* w);

/**
 * @brief GTK's colour chooser dialog (shown by GtkColorDialog and GtkColorDialogButton) keeps the size of its palette
 * view when the user switches to the custom colour editor, which then gets a scrollbar. Call once at start-up: it
 * watches for such dialogs and enlarges them to fit the editor.
 */
void installColorChooserDialogSizeFix();

/**
 * @brief Copies a Cairo image surface (ARGB32 or RGB24) into a new GdkTexture (full reference).
 * Replaces the deprecated gdk_pixbuf_get_from_surface() + gdk_texture_new_for_pixbuf() round trip.
 */
GdkTexture* textureFromCairoSurface(cairo_surface_t* surface);

/// xoj::util::gtk::translateCoordinates() replacement (deprecated in GTK 4.12) with the same signature
bool translateCoordinates(GtkWidget* from, GtkWidget* to, double x, double y, double* outX, double* outY);
};  // namespace gtk
};  // namespace xoj::util
