#include "SelectBackgroundColorDialog.h"

#include <algorithm>  // for min
#include <cstdint>    // for uint32_t
#include <string>     // for allocator

#include <glib.h>  // for g_free, g_strdup_printf

#include "control/Control.h"            // for Control
#include "control/settings/Settings.h"  // for SElement, Settings
#include "util/Color.h"
#include "util/i18n.h"                  // for _
#include "util/safe_casts.h"            // for as_unsigned

static inline std::array<GdkRGBA, 9> background1 = {
        Util::rgb_to_GdkRGBA(Colors::xopp_lightpink),  //
        Util::rgb_to_GdkRGBA(Colors::xopp_bisque),  //
        Util::rgb_to_GdkRGBA(Colors::xopp_lemonchifon),  //
        Util::rgb_to_GdkRGBA(Colors::xopp_palegoldenrod),  //
        Util::rgb_to_GdkRGBA(Colors::xopp_lavender),  //
        Util::rgb_to_GdkRGBA(Colors::xopp_gainsboro03),  //
        Util::rgb_to_GdkRGBA(Colors::xopp_antiquewhite),  //
        Util::rgb_to_GdkRGBA(Colors::xopp_gainsboro),  //
        Util::rgb_to_GdkRGBA(Colors::xopp_snow)   //
};

static inline std::array<GdkRGBA, 6> backgroundXournal = {
        Util::rgb_to_GdkRGBA(Colors::white),  //
        Util::rgb_to_GdkRGBA(Colors::xopp_paleturqoise),  //
        Util::rgb_to_GdkRGBA(Colors::xopp_aquamarine),  //
        Util::rgb_to_GdkRGBA(Colors::xopp_pink),  //
        Util::rgb_to_GdkRGBA(Colors::xopp_lightsalmon),  //
        Util::rgb_to_GdkRGBA(Colors::xopp_khaki)   //
};

SelectBackgroundColorDialog::SelectBackgroundColorDialog(Control* control, std::function<void(Color)> callback):
        control(control), callback(callback) {
    lastBackgroundColors.fill(Util::rgb_to_GdkRGBA(Colors::white));
    Settings* settings = control->getSettings();
    SElement& el = settings->getCustomElement("lastUsedPageBgColor");

    int count = 0;
    el.getInt("count", count);

    auto index = 0ULL;

    for (int i = 0; i < count && index < LAST_BACKGROUND_COLOR_COUNT; i++) {
        int iColor{};
        char* settingName = g_strdup_printf("color%02i", i);
        bool read = el.getInt(settingName, iColor);
        g_free(settingName);

        if (!read) {
            continue;
        }

        lastBackgroundColors[index] = Util::rgb_to_GdkRGBA(Color(as_unsigned(iColor)));

        ++index;
    }


    lastBackgroundColorCount = index;

    window.reset(GTK_WINDOW(gtk_window_new()));
    gtk_window_set_title(window.get(), _("Select background color"));
    gtk_window_set_resizable(window.get(), false);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_window_set_child(window.get(), box);

    gtk_box_append(GTK_BOX(box), makeSwatchRow(_("Background colors"), background1.data(), background1.size()));
    gtk_box_append(GTK_BOX(box),
                   makeSwatchRow(_("Xournal colors"), backgroundXournal.data(), backgroundXournal.size()));
    // Last used colors (only by background, the general last used are shared with the pen and highlighter etc.)
    if (lastBackgroundColorCount > 0) {
        gtk_box_append(GTK_BOX(box),
                       makeSwatchRow(_("Recently used"), lastBackgroundColors.data(), lastBackgroundColorCount));
    }

    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    gtk_widget_set_margin_top(buttons, 6);
    GtkWidget* custom = gtk_button_new_with_mnemonic(_("_Custom…"));
    GtkWidget* cancel = gtk_button_new_with_mnemonic(_("_Cancel"));
    gtk_box_append(GTK_BOX(buttons), custom);
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(box), buttons);

    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_close), window.get());
    g_signal_connect(custom, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) {
                         auto* self = static_cast<SelectBackgroundColorDialog*>(d);
                         GtkColorDialog* dlg = gtk_color_dialog_new();
                         gtk_color_dialog_set_with_alpha(dlg, false);
                         gtk_color_dialog_set_title(dlg, _("Select background color"));
                         gtk_color_dialog_choose_rgba(
                                 dlg, self->window.get(), &self->lastBackgroundColors[0], nullptr,
                                 +[](GObject* src, GAsyncResult* res, gpointer d) {
                                     GdkRGBA* c = gtk_color_dialog_choose_rgba_finish(GTK_COLOR_DIALOG(src), res,
                                                                                      nullptr);
                                     if (c) {
                                         static_cast<SelectBackgroundColorDialog*>(d)->choose(*c);
                                         gdk_rgba_free(c);
                                     }
                                     g_object_unref(src);
                                 },
                                 self);
                     }),
                     this);
}

GtkWidget* SelectBackgroundColorDialog::makeSwatchRow(const char* title, const GdkRGBA* colors, size_t count) {
    constexpr int SWATCH_SIZE = 28;

    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget* label = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_widget_add_css_class(label, "heading");
    gtk_box_append(GTK_BOX(row), label);

    GtkWidget* swatches = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    for (size_t i = 0; i < count; i++) {
        GtkWidget* button = gtk_button_new();
        gtk_widget_add_css_class(button, "flat");
        gtk_widget_add_css_class(button, "circular");
        GtkWidget* area = gtk_drawing_area_new();
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), SWATCH_SIZE);
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), SWATCH_SIZE);
        gtk_drawing_area_set_draw_func(
                GTK_DRAWING_AREA(area),
                +[](GtkDrawingArea*, cairo_t* cr, int w, int h, gpointer c) {
                    const auto* color = static_cast<const GdkRGBA*>(c);
                    cairo_arc(cr, w / 2.0, h / 2.0, std::min(w, h) / 2.0 - 1, 0, 2 * G_PI);
                    gdk_cairo_set_source_rgba(cr, color);
                    cairo_fill_preserve(cr);
                    cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.5);  // keeps light colours visible
                    cairo_set_line_width(cr, 1);
                    cairo_stroke(cr);
                },
                const_cast<GdkRGBA*>(&colors[i]), nullptr);
        gtk_button_set_child(GTK_BUTTON(button), area);

        g_object_set_data(G_OBJECT(button), "xopp-color", const_cast<GdkRGBA*>(&colors[i]));
        g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer d) {
                             auto* color = static_cast<GdkRGBA*>(g_object_get_data(G_OBJECT(b), "xopp-color"));
                             static_cast<SelectBackgroundColorDialog*>(d)->choose(*color);
                         }),
                         this);
        gtk_box_append(GTK_BOX(swatches), button);
    }
    gtk_box_append(GTK_BOX(row), swatches);
    return row;
}

void SelectBackgroundColorDialog::choose(GdkRGBA color) {
    storeLastUsedValuesInSettings(color);
    callback(Util::GdkRGBA_to_argb(color));
    gtk_window_close(window.get());  // Destroys *this (see PopupWindowWrapper)
}

void SelectBackgroundColorDialog::storeLastUsedValuesInSettings(GdkRGBA color) {
    for (size_t i = 0; i < lastBackgroundColorCount; i++) {
        if (gdk_rgba_equal(&lastBackgroundColors[i], &color)) {
            // The new color is already in the list, do not save
            return;
        }
    }

    Settings* settings = control->getSettings();
    SElement& el = settings->getCustomElement("lastUsedPageBgColor");

    // Move all colors one step back
    for (size_t i = LAST_BACKGROUND_COLOR_COUNT - 1; i > 0; i--) {
        lastBackgroundColors[i] = lastBackgroundColors[i - 1];
    }
    lastBackgroundColors[0] = color;
    lastBackgroundColorCount = std::min<size_t>(lastBackgroundColorCount + 1, LAST_BACKGROUND_COLOR_COUNT);

    // Only the colors actually used are stored (older versions padded the list with white)
    el.setInt("count", static_cast<int>(lastBackgroundColorCount));
    for (size_t i = 0; i < lastBackgroundColorCount; i++) {
        char* settingName = g_strdup_printf("color%02zu", i);
        el.setIntHex(settingName, int(uint32_t(Util::GdkRGBA_to_argb(lastBackgroundColors[i]))));
        g_free(settingName);
    }

    settings->customSettingsChanged();
}
