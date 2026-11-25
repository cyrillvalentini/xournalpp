#include "ColorIcon.h"

#include <algorithm>  // for max
#include <cmath>      // for M_PI, fmod, abs

#include "util/raii/CairoWrappers.h"
#include "util/raii/GObjectSPtr.h"

namespace ColorIcon {
/**
 * Create a new GtkImage with preview color
 */
auto newGtkImage(Color color, int size, bool circle, std::optional<Color> secondaryColor) -> GtkWidget* {
    xoj::util::GObjectSPtr<GdkPixbuf> img(newGdkPixbuf(color, size, circle, secondaryColor));
    GtkWidget* w = gtk_image_new_from_pixbuf(img.get());
    gtk_widget_show(w);
    return w;
}

/**
 * Create a new GdkPixbuf* with preview color
 */
auto newGdkPixbuf(Color color, int size, bool circle, std::optional<Color> secondaryColor)
        -> xoj::util::GObjectSPtr<GdkPixbuf> {
    xoj::util::CairoSurfaceSPtr buf(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size), xoj::util::adopt);
    xoj::util::CairoSPtr cr(cairo_create(buf.get()), xoj::util::adopt);

    Util::cairo_set_source_rgbi(cr.get(), color, 1);

    constexpr double PADDING = 1;

    if (circle) {
        const double mid = .5 * size;
        const double radius = mid - PADDING;
        cairo_arc(cr.get(), mid, mid, radius, 0, 2 * M_PI);
    } else {
        cairo_rectangle(cr.get(), PADDING, PADDING, size - 2 * PADDING, size - 2 * PADDING);
    }
    cairo_fill_preserve(cr.get());

    // A soft outline: keeps white swatches visible on light backgrounds and black ones on dark backgrounds
    cairo_set_source_rgba(cr.get(), 0.5, 0.5, 0.5, 0.5);
    cairo_set_line_width(cr.get(), 1);
    cairo_stroke(cr.get());

    if (secondaryColor) {
        // Draw an indicator for the secondary color
        Util::cairo_set_source_rgbi(cr.get(), secondaryColor.value(), 1);

        const double indicatorMid = size - 2 * PADDING;
        const double indicatorRadius = (0.5 * size) - PADDING;
        // only draws the upper left quarter of the arc
        cairo_arc(cr.get(), indicatorMid, indicatorMid, indicatorRadius, M_PI, M_PI / 2);
        cairo_fill_preserve(cr.get());

        cairo_set_source_rgba(cr.get(), 0, 0, 0, 1);
        cairo_set_line_width(cr.get(), 1);
        cairo_stroke(cr.get());
    }

    return xoj::util::GObjectSPtr<GdkPixbuf>(gdk_pixbuf_get_from_surface(buf.get(), 0, 0, size, size),
                                             xoj::util::adopt);
}
auto newColorPickerImage(Color current, int size) -> GtkWidget* {
    xoj::util::CairoSurfaceSPtr buf(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size), xoj::util::adopt);
    xoj::util::CairoSPtr cr(cairo_create(buf.get()), xoj::util::adopt);
    const double mid = .5 * size;
    const double outer = mid - 0.5;
    const double ringWidth = std::max(2.0, size / 8.0);

    // Hue ring, drawn as thin wedges
    constexpr int STEPS = 72;
    for (int i = 0; i < STEPS; i++) {
        const double a0 = 2 * M_PI * i / STEPS - M_PI / 2;
        const double a1 = 2 * M_PI * (i + 1.05) / STEPS - M_PI / 2;
        const double h = 6.0 * i / STEPS;  // HSV hue in [0, 6), full saturation and value
        const double x = 1 - std::abs(std::fmod(h, 2.0) - 1);
        double r = 0, g = 0, b = 0;
        switch (static_cast<int>(h)) {
            case 0: r = 1, g = x; break;
            case 1: r = x, g = 1; break;
            case 2: g = 1, b = x; break;
            case 3: g = x, b = 1; break;
            case 4: r = x, b = 1; break;
            default: r = 1, b = x; break;
        }
        cairo_set_source_rgb(cr.get(), r, g, b);
        cairo_arc(cr.get(), mid, mid, outer, a0, a1);
        cairo_arc_negative(cr.get(), mid, mid, outer - ringWidth, a1, a0);
        cairo_close_path(cr.get());
        cairo_fill(cr.get());
    }

    // The current colour, with a gap to the ring
    Util::cairo_set_source_rgbi(cr.get(), current, 1);
    cairo_arc(cr.get(), mid, mid, outer - ringWidth - 1.5, 0, 2 * M_PI);
    cairo_fill_preserve(cr.get());
    cairo_set_source_rgba(cr.get(), 0.5, 0.5, 0.5, 0.5);
    cairo_set_line_width(cr.get(), 1);
    cairo_stroke(cr.get());

    xoj::util::GObjectSPtr<GdkPixbuf> img(gdk_pixbuf_get_from_surface(buf.get(), 0, 0, size, size), xoj::util::adopt);
    GtkWidget* w = gtk_image_new_from_pixbuf(img.get());
    gtk_widget_show(w);
    return w;
}

};  // namespace ColorIcon
