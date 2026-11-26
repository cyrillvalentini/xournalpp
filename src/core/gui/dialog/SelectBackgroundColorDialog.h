/*
 * Xournal++
 *
 * Dialog for selecting a background color
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>  // for array
#include <functional>

#include <gdk/gdk.h>  // for GdkRGBA
#include <gtk/gtk.h>  // for GtkWindow

#include "util/Color.h"
#include "util/raii/GtkWindowUPtr.h"

class Control;

const int LAST_BACKGROUND_COLOR_COUNT = 9;

/**
 * A window with rows of colour swatches (suggested background colours, the classic Xournal colours and the recently
 * used ones) and a "Custom…" button opening GTK's colour dialog. Clicking a swatch applies it and closes the window.
 * (GtkColorChooserDialog, which could show these palettes, is deprecated; GtkColorDialog can't show custom palettes.)
 */
class SelectBackgroundColorDialog {
public:
    SelectBackgroundColorDialog(Control* control, std::function<void(Color)> callback);

    inline GtkWindow* getWindow() const { return window.get(); }

private:
    void storeLastUsedValuesInSettings(GdkRGBA color);
    /// Applies the color, remembers it and closes the window
    void choose(GdkRGBA color);
    GtkWidget* makeSwatchRow(const char* title, const GdkRGBA* colors, size_t count);

private:
    Control* control{nullptr};

    /**
     * Last used background colors (stored in settings)
     */
    std::array<GdkRGBA, LAST_BACKGROUND_COLOR_COUNT> lastBackgroundColors{};
    /// How many of lastBackgroundColors were actually used (the rest are placeholders)
    size_t lastBackgroundColorCount = 0;

    xoj::util::GtkWindowUPtr window;

    std::function<void(Color)> callback;
};
