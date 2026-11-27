#include "SbaFormat.h"

#include <cmath>  // for fmod, sqrt, isfinite

#include "model/BackgroundConfig.h"
#include "model/PageType.h"

namespace xoj::sba {

std::pair<double, double> normalizedPageSize(double width, double height) {
    const double area = width * height;
    if (!(area > 0) || !std::isfinite(area)) {
        return {DEFAULT_PAGE_WIDTH, DEFAULT_PAGE_HEIGHT};
    }
    const double sf = std::sqrt(DEFAULT_PAGE_WIDTH * DEFAULT_PAGE_HEIGHT / area);
    return {width * sf, height * sf};
}

double rulingShift(const PageType& type) {
    if (type.format != PageTypeFormat::Ruled && type.format != PageTypeFormat::Lined) {
        return 0;
    }
    constexpr double XOPP_HEADER = 80.0;  // RuledBackgroundView::HEADER_SIZE
    double spacing = 24.0;                 // RuledBackgroundView default
    BackgroundConfig(type.config).loadValue("r1", spacing);
    if (spacing <= 0) {
        return 0;
    }
    double shift = std::fmod(XOPP_HEADER - FIRST_RULING_LINE * spacing, spacing);
    if (shift < 0) {
        shift += spacing;
    }
    if (shift > spacing / 2) {
        shift -= spacing;
    }
    return shift;
}

std::optional<std::string> surfaceToPng(cairo_surface_t* surface) {
    std::string out;
    auto write = +[](void* closure, const unsigned char* data, unsigned int length) -> cairo_status_t {
        static_cast<std::string*>(closure)->append(reinterpret_cast<const char*>(data), length);
        return CAIRO_STATUS_SUCCESS;
    };
    if (cairo_surface_write_to_png_stream(surface, write, &out) != CAIRO_STATUS_SUCCESS) {
        return std::nullopt;
    }
    return out;
}

}  // namespace xoj::sba
