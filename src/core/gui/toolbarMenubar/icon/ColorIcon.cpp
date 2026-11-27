#include "ColorIcon.h"

#include <cmath>  // for M_PI
#include <iomanip>
#include <sstream>

#include "util/raii/CairoWrappers.h"
#include "util/raii/GObjectSPtr.h"
#include "util/serdesstream.h"

/// The colour as "rrggbb"
static auto hexRGB(Color c) -> std::string {
    auto s = serdes_stream<std::stringstream>();
    s << std::hex << std::setw(6) << std::setfill('0') << (uint32_t(c) & 0x00ffffff);
    return s.str();
}

/// A darker shade of the colour, for the outline (instead of black, which looks harsh on dark backgrounds)
static auto darker(Color c) -> Color {
    constexpr double FACTOR = 0.7;
    return Color(static_cast<uint8_t>(c.red * FACTOR), static_cast<uint8_t>(c.green * FACTOR),
                 static_cast<uint8_t>(c.blue * FACTOR));
}

static auto newGdkPixbuf(Color color, bool circle, std::optional<Color> secondaryColor, bool plus = false)
        -> xoj::util::GObjectSPtr<GdkPixbuf> {
    auto stream = serdes_stream<std::stringstream>();
    stream << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"48\" height=\"48\" stroke-width=\"4\" "
              "stroke-linecap=\"round\" stroke-linejoin=\"round\" stroke=\"#"
           << hexRGB(darker(color)) << "\" stroke-opacity=\"1\">";
    if (circle) {
        stream << "<circle cx=\"24\" cy=\"24\" r=\"20\" ";
    } else {
        stream << "<rect width=\"40\" height=\"40\" x=\"4\" y=\"4\" rx=\"2\" ry=\"2\" ";
    }
    stream << "style=\"fill:#" << std::hex << std::setw(6) << std::setfill('0') << (uint32_t(color) & 0x00ffffff)
           << "\"/>";

    if (plus) {
        // "Choose a colour": a plus in the middle, black or white depending on the colour behind it
        const double luminance = (0.2126 * color.red + 0.7152 * color.green + 0.0722 * color.blue) / 255.0;
        stream << "<path d=\"M 24 14 V 34 M 14 24 H 34\" stroke-width=\"4\" stroke=\""
               << (luminance > 0.6 ? "#000000\" stroke-opacity=\"0.75" : "#ffffff\" stroke-opacity=\"0.95") << "\"/>";
    }

    if (secondaryColor) {
        stream << "<path d=\"M 46 22 A 22 22 0 0 0 22 46 L 46 46 Z\" "
               << "style=\"fill:#" << std::hex << std::setw(6) << std::setfill('0')
               << (uint32_t(*secondaryColor) & 0x00ffffff) << "\"/>";
    }
    stream << "</svg>";

    std::string str = stream.str();  // Keep this alive as long as gstream is
    xoj::util::GObjectSPtr<GInputStream> gstream(g_memory_input_stream_new_from_data(str.c_str(), -1, nullptr),
                                                 xoj::util::adopt);

    return xoj::util::GObjectSPtr<GdkPixbuf>(gdk_pixbuf_new_from_stream(gstream.get(), nullptr, nullptr),
                                             xoj::util::adopt);
}

namespace ColorIcon {
auto newColorSelectorImage(Color color) -> GtkWidget* {
    xoj::util::GObjectSPtr<GdkTexture> texture(gdk_texture_new_for_pixbuf(newGdkPixbuf(color, false, std::nullopt, true).get()),
                                               xoj::util::adopt);
    return gtk_image_new_from_paintable(GDK_PAINTABLE(texture.get()));
}

/**
 * Create a new GtkImage with preview color
 */
auto newGtkImage(Color color, bool circle, std::optional<Color> secondaryColor) -> GtkWidget* {
    return gtk_image_new_from_paintable(newGdkPaintable(color, circle, secondaryColor).get());
}

/**
 * Create a new GdkPaintable* with preview color
 */
auto newGdkPaintable(Color color, bool circle, std::optional<Color> secondaryColor)
        -> xoj::util::GObjectSPtr<GdkPaintable> {
    return xoj::util::GObjectSPtr<GdkPaintable>(
            GDK_PAINTABLE(gdk_texture_new_for_pixbuf(newGdkPixbuf(color, circle, secondaryColor).get())),
            xoj::util::adopt);
}
};  // namespace ColorIcon
