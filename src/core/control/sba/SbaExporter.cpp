#include "SbaExporter.h"

#include <algorithm>  // for max, clamp, sort
#include <cmath>      // for round, ceil
#include <cstring>    // for memcpy
#include <fstream>    // for ifstream
#include <iterator>   // for istreambuf_iterator
#include <list>       // for list

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>
#include <zip.h>

#include "model/BackgroundConfig.h"
#include "model/BackgroundImage.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Image.h"
#include "model/Layer.h"
#include "model/LineStyle.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfDocument.h"
#include "util/StringUtils.h"
#include "util/i18n.h"
#include "util/raii/CairoWrappers.h"
#include "util/raii/GObjectSPtr.h"
#include "view/View.h"

#include "SbaFormat.h"

using namespace xoj::sba;
namespace bs = xoj::sba::bson;

namespace {

bs::Value colorValue(uint32_t argb) {
    // Saber reads both int32 and int64. Like Dart's bson, use int64 when the value does not fit into an int32.
    if (argb > static_cast<uint32_t>(INT32_MAX)) {
        return static_cast<int64_t>(argb);
    }
    return static_cast<int32_t>(argb);
}

uint32_t rgb(Color c) { return static_cast<uint32_t>(c) & 0x00ffffffU; }

bs::Value point(double x, double y, std::optional<double> pressure) {
    float f[3] = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(pressure.value_or(0))};
    std::string data(pressure ? 12 : 8, '\0');
    std::memcpy(data.data(), f, data.size());
    return bs::Binary{std::move(data)};
}

std::optional<std::string> pixbufToPng(GdkPixbuf* pixbuf) {
    gchar* buffer = nullptr;
    gsize size = 0;
    GError* err = nullptr;
    if (!gdk_pixbuf_save_to_buffer(pixbuf, &buffer, &size, "png", &err, nullptr)) {
        if (err) {
            g_error_free(err);
        }
        return std::nullopt;
    }
    std::string png(buffer, size);
    g_free(buffer);
    return png;
}

/// Returns "png" or "jpeg" if GdkPixbuf recognizes the data as such, the format name otherwise ("" if unreadable)
std::string imageFormat(const std::string& data, int* width, int* height) {
    xoj::util::GObjectSPtr<GdkPixbufLoader> loader(gdk_pixbuf_loader_new(), xoj::util::adopt);
    GError* err = nullptr;
    if (!gdk_pixbuf_loader_write(loader.get(), reinterpret_cast<const guchar*>(data.data()), data.size(), &err) ||
        !gdk_pixbuf_loader_close(loader.get(), &err)) {
        if (err) {
            g_error_free(err);
        }
        return "";
    }
    GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(loader.get());
    GdkPixbufFormat* format = gdk_pixbuf_loader_get_format(loader.get());
    if (!pixbuf || !format) {
        return "";
    }
    *width = gdk_pixbuf_get_width(pixbuf);
    *height = gdk_pixbuf_get_height(pixbuf);
    gchar* name = gdk_pixbuf_format_get_name(format);
    std::string res = name ? name : "";
    g_free(name);
    return res;
}

/// The background PDF as bytes
std::optional<std::string> pdfBytes(const Document* doc) {
    gchar* tmpName = nullptr;
    GError* err = nullptr;
    int fd = g_file_open_tmp("xournalpp-sba-XXXXXX.pdf", &tmpName, &err);
    if (fd < 0) {
        if (err) {
            g_error_free(err);
        }
        return std::nullopt;
    }
    close(fd);
    fs::path tmp(tmpName);
    g_free(tmpName);

    std::optional<std::string> result;
    if (doc->getPdfDocument().save(tmp, &err)) {
        std::ifstream in(tmp, std::ios::binary);
        result = std::string(std::istreambuf_iterator<char>(in), {});
    } else if (err) {
        g_error_free(err);
    }
    std::error_code ec;
    fs::remove(tmp, ec);
    return result;
}

}  // namespace

void SbaExporter::warn(std::string msg) {
    if (std::find(warnings.begin(), warnings.end(), msg) == warnings.end()) {
        warnings.push_back(std::move(msg));
    }
}

int SbaExporter::addAsset(std::string data) {
    assets.push_back(std::move(data));
    return static_cast<int>(assets.size() - 1);
}

bool SbaExporter::exportTo(const Document* doc, const fs::path& out, const PageRangeVector& range) {
    std::string mainFile;
    std::vector<std::string> assetFiles;
    if (!build(doc, range, mainFile, assetFiles)) {
        return false;
    }

    int zipError = 0;
    zip_t* zip = zip_open(char_cast(out.u8string().c_str()), ZIP_CREATE | ZIP_TRUNCATE, &zipError);
    if (!zip) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, zipError);
        error = FS(_F("Could not create \"{1}\": {2}") % out.u8string() % zip_error_strerror(&ze));
        zip_error_fini(&ze);
        return false;
    }

    auto add = [&](const std::string& name, const std::string& data) {
        zip_source_t* src = zip_source_buffer(zip, data.data(), data.size(), 0);
        if (!src || zip_file_add(zip, name.c_str(), src, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8) < 0) {
            if (src) {
                zip_source_free(src);
            }
            return false;
        }
        return true;
    };

    bool ok = add(MAIN_FILE, mainFile);
    for (size_t i = 0; ok && i < assetFiles.size(); i++) {
        ok = add(std::string(MAIN_FILE) + "." + std::to_string(i), assetFiles[i]);
    }
    // The data buffers must stay alive until zip_close(), which writes the archive
    if (!ok || zip_close(zip) != 0) {
        error = FS(_F("Could not write \"{1}\": {2}") % out.u8string() % zip_strerror(zip));
        zip_discard(zip);
        return false;
    }
    return true;
}

bool SbaExporter::build(const Document* doc, const PageRangeVector& range, std::string& mainFile,
                        std::vector<std::string>& assetFiles) {
    std::vector<size_t> pageNumbers;
    const size_t count = doc->getPageCount();
    if (range.empty()) {
        for (size_t i = 0; i < count; i++) {
            pageNumbers.push_back(i);
        }
    } else {
        for (const auto& e: range) {
            for (size_t i = e.first; i <= e.last && i < count; i++) {
                pageNumbers.push_back(i);
            }
        }
    }
    if (pageNumbers.empty()) {
        error = _("There are no pages to export.");
        return false;
    }

    choosePattern(doc, pageNumbers);

    bs::Array pagesJson;
    for (size_t i = 0; i < pageNumbers.size(); i++) {
        pagesJson.emplace_back(buildPage(doc, doc->getPage(pageNumbers[i]), static_cast<int>(i)));
    }

    bs::Document root;
    root.push_back({"v", static_cast<int32_t>(SBN_VERSION)});
    root.push_back({"ni", static_cast<int32_t>(nextImageId)});
    root.push_back({"b", backgroundColor ? colorValue(*backgroundColor) : bs::Value(nullptr)});
    root.push_back({"p", pattern});
    root.push_back({"l", static_cast<int32_t>(lineHeight)});
    root.push_back({"lt", static_cast<int32_t>(DEFAULT_LINE_THICKNESS)});
    root.push_back({"z", std::move(pagesJson)});
    root.push_back({"c", static_cast<int32_t>(0)});

    mainFile = bs::serialize(root);
    assetFiles = std::move(assets);
    assets.clear();
    return true;
}

void SbaExporter::choosePattern(const Document* doc, const std::vector<size_t>& pageNumbers) {
    // Saber has one background for the whole note: use the one of the first page with a ruling
    bool found = false;
    for (size_t nr: pageNumbers) {
        auto page = doc->getPage(nr);
        const PageType& type = page->getBackgroundType();
        if (type.isSpecial()) {
            continue;
        }
        if (found) {
            if (type != doc->getPage(pageNumbers.front())->getBackgroundType()) {
                warn(_("Saber uses the same background for all pages. The background of the first page was used."));
            }
            continue;
        }
        found = true;

        BackgroundConfig config(type.config);
        double spacing = 0;
        switch (type.format) {
            case PageTypeFormat::Plain:
                pattern = "";
                break;
            case PageTypeFormat::Ruled:
                pattern = "lined";
                spacing = 24.0;
                break;
            case PageTypeFormat::Lined: {
                pattern = "college";
                spacing = 24.0;
                double margin = 72.0;
                config.loadValue("m1", margin);
                if (margin < 0) {
                    pattern = "college-rtl";
                }
                break;
            }
            case PageTypeFormat::Staves:
                pattern = "staffs";
                break;
            case PageTypeFormat::Graph:
            case PageTypeFormat::IsoGraph:
                pattern = "grid";
                spacing = 14.17;
                break;
            case PageTypeFormat::Dotted:
            case PageTypeFormat::IsoDotted:
                pattern = "dots";
                spacing = 14.17;
                break;
            default:
                break;
        }
        if (type.format == PageTypeFormat::IsoGraph || type.format == PageTypeFormat::IsoDotted) {
            warn(_("Isometric backgrounds were replaced by a square grid or dots."));
        }
        if (spacing > 0) {
            config.loadValue("r1", spacing);
            lineHeight = std::clamp(static_cast<int>(std::round(spacing / SCALE)), 10, 300);
        }

        const uint32_t color = static_cast<uint32_t>(page->getBackgroundColor()) | 0xff000000U;
        if (color != 0xffffffffU) {
            backgroundColor = color;
        }
    }
}

bs::Document SbaExporter::buildPage(const Document* doc, const ConstPageRef& page, int pageIndex) {
    shiftY = rulingShift(page->getBackgroundType());

    bs::Document json;
    json.push_back({"w", page->getWidth() / SCALE});
    json.push_back({"h", page->getHeight() / SCALE});

    bs::Array strokes;
    bs::Array images;
    std::vector<const Text*> texts;

    size_t layersWithContent = 0;
    for (const Layer* layer: page->getLayersView()) {
        auto elements = layer->getElementsView();
        if (elements.begin() != elements.end()) {
            layersWithContent++;
        }
        for (const Element* e: elements) {
            switch (e->getType()) {
                case ELEMENT_STROKE:
                    strokes.push_back(strokeToJson(static_cast<const Stroke*>(e), pageIndex, page->getBackgroundColor()));
                    break;
                case ELEMENT_IMAGE:
                    if (auto j = imageToJson(static_cast<const Image*>(e), pageIndex)) {
                        images.push_back(std::move(*j));
                    }
                    break;
                case ELEMENT_TEXT:
                    texts.push_back(static_cast<const Text*>(e));
                    break;
                case ELEMENT_TEXIMAGE:
                    warn(_("LaTeX formulas were exported as pictures."));
                    [[fallthrough]];
                default:
                    if (auto j = rasterizedElementToJson(e, pageIndex)) {
                        images.push_back(std::move(*j));
                    }
                    break;
            }
        }
    }
    if (layersWithContent > 1) {
        warn(_("The layers of a page were merged into one."));
    }

    if (!strokes.empty()) {
        json.push_back({"s", std::move(strokes)});
    }
    if (!images.empty()) {
        json.push_back({"i", std::move(images)});
    }
    if (!texts.empty()) {
        json.push_back({"q", textToQuill(std::move(texts))});
    }
    if (auto bg = backgroundToJson(doc, page, pageIndex)) {
        json.push_back({"b", std::move(*bg)});
    }
    return json;
}

bs::Value SbaExporter::strokeToJson(const Stroke* s, int pageIndex, Color pageColor) {
    const auto& pts = s->getPointVector();
    const StrokeTool tool = s->getToolType();
    const bool highlighter = tool == StrokeTool::HIGHLIGHTER;
    const bool pressure = !highlighter && s->hasPressure() && pts.size() >= 2;

    double size = s->getWidth() / SCALE;
    std::vector<double> diameters;
    if (pressure) {
        // Width of point i: the width of the segment starting there (the last point has none)
        diameters.reserve(pts.size());
        double sum = 0, max = 0;
        for (size_t i = 0; i < pts.size(); i++) {
            double d = (i + 1 < pts.size() ? pts[i].z : pts[i - 1].z) / SCALE;
            d = std::max(d, 0.0);
            diameters.push_back(d);
            sum += d;
            max = std::max(max, d);
        }
        // perfect_freehand with thinning 0.5: diameter = size * (0.5 + pressure), pressure in [0, 1]
        size = std::max(sum / static_cast<double>(diameters.size()), max / 1.5);
        if (size <= 0) {
            size = s->getWidth() / SCALE;
        }
    }

    bs::Array points;
    points.reserve(pts.size());
    for (size_t i = 0; i < pts.size(); i++) {
        std::optional<double> p;
        if (pressure) {
            p = std::clamp(diameters[i] / size - 0.5, 0.0, 1.0);
        }
        points.push_back(point(pts[i].x / SCALE, (pts[i].y - shiftY) / SCALE, p));
    }

    uint32_t color = rgb(s->getColor()) | 0xff000000U;
    std::string type = pressure ? "fountainPen" : "ballpointPen";
    if (highlighter) {
        type = "Highlighter";
        color = rgb(s->getColor()) | (HIGHLIGHTER_ALPHA << 24);
    } else if (tool == StrokeTool::ERASER) {
        // Whiteout strokes: paint with the page color
        color = rgb(pageColor) | 0xff000000U;
    }
    if (s->getLineStyle().hasDashes()) {
        warn(_("Dashed and dotted lines were exported as solid lines."));
    }
    if (s->getFill() != -1) {
        warn(_("Filled shapes were exported without their fill."));
    }

    bs::Document json;
    json.push_back({"shape", nullptr});
    json.push_back({"p", std::move(points)});
    json.push_back({"i", static_cast<int32_t>(pageIndex)});
    json.push_back({"ty", type});
    json.push_back({"pe", pressure});
    json.push_back({"c", colorValue(color)});
    json.push_back({"s", size});
    // No smoothing and no streamline, so that Saber draws the points as they are
    json.push_back({"sl", 0.0});
    json.push_back({"sp", false});
    return json;
}

bs::Document SbaExporter::imageJson(int pageIndex, const std::string& ext, double x, double y, double w, double h,
                                    double nw, double nh, int asset) {
    bs::Document json;
    json.push_back({"id", static_cast<int32_t>(nextImageId++)});
    json.push_back({"e", ext});
    json.push_back({"i", static_cast<int32_t>(pageIndex)});
    json.push_back({"v", true});
    json.push_back({"f", static_cast<int32_t>(BOX_FIT_CONTAIN)});
    json.push_back({"x", x});
    json.push_back({"y", y});
    json.push_back({"w", w});
    json.push_back({"h", h});
    if (nw > 0 && nh > 0) {
        json.push_back({"nw", nw});
        json.push_back({"nh", nh});
    }
    json.push_back({"a", static_cast<int32_t>(asset)});
    return json;
}

std::optional<bs::Value> SbaExporter::imageToJson(const Image* img, int pageIndex) {
    const auto& m = img->getTransformation();
    if (m.xy != 0 || m.yx != 0) {
        // Rotated: Saber has no rotation, use a picture of the rotated image
        warn(_("Rotated images were exported as pictures of their bounding box."));
        return rasterizedElementToJson(img, pageIndex);
    }

    int w = 0, h = 0;
    std::string format = imageFormat(img->getBinaryData(), &w, &h);
    std::string data;
    std::string ext;
    if (format == "png") {
        data = img->getBinaryData();
        ext = ".png";
    } else if (format == "jpeg") {
        data = img->getBinaryData();
        ext = ".jpg";
    } else {
        return rasterizedElementToJson(img, pageIndex);
    }

    const auto& box = img->getBoundingBox();
    const int asset = addAsset(std::move(data));
    return imageJson(pageIndex, ext, box.x / SCALE, (box.y - shiftY) / SCALE, box.width / SCALE, box.height / SCALE, w, h, asset);
}

std::optional<bs::Value> SbaExporter::rasterizedElementToJson(const Element* e, int pageIndex) {
    const auto& box = e->getBoundingBox();
    if (box.width <= 0 || box.height <= 0) {
        return std::nullopt;
    }
    constexpr double ZOOM = 3.0;
    const int pw = std::max(1, static_cast<int>(std::ceil(box.width * ZOOM)));
    const int ph = std::max(1, static_cast<int>(std::ceil(box.height * ZOOM)));
    if (static_cast<int64_t>(pw) * ph > 64LL * 1024 * 1024) {
        warn(_("An element was too large to be exported."));
        return std::nullopt;
    }
    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph), xoj::util::adopt);
    {
        xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);
        cairo_scale(cr.get(), ZOOM, ZOOM);
        cairo_translate(cr.get(), -box.x, -box.y);
        auto view = xoj::view::ElementView::createFromElement(e);
        view->draw(xoj::view::Context::createDefault(cr.get()));
    }
    cairo_surface_flush(surface.get());
    auto png = surfaceToPng(surface.get());
    if (!png) {
        return std::nullopt;
    }
    const int asset = addAsset(std::move(*png));
    return imageJson(pageIndex, ".png", box.x / SCALE, (box.y - shiftY) / SCALE, box.width / SCALE, box.height / SCALE, pw, ph,
                     asset);
}

std::optional<bs::Document> SbaExporter::backgroundToJson(const Document* doc, const ConstPageRef& page,
                                                          int pageIndex) {
    const PageType& type = page->getBackgroundType();
    const double w = page->getWidth() / SCALE;
    const double h = page->getHeight() / SCALE;

    if (type.isPdfPage()) {
        if (pdfAsset == -1) {
            auto bytes = pdfBytes(doc);
            if (!bytes) {
                warn(_("The PDF background could not be exported."));
                return std::nullopt;
            }
            pdfAsset = addAsset(std::move(*bytes));
        }
        auto pdfPage = doc->getPdfPage(page->getPdfPageNr());
        const double nw = pdfPage ? pdfPage->getWidth() : 0;
        const double nh = pdfPage ? pdfPage->getHeight() : 0;
        auto json = imageJson(pageIndex, ".pdf", 0, 0, w, h, nw, nh, pdfAsset);
        json.push_back({"pdfi", static_cast<int32_t>(page->getPdfPageNr())});
        return json;
    }

    if (type.isImagePage()) {
        const GdkPixbuf* pixbuf = page->getBackgroundImage().getPixbuf();
        if (!pixbuf) {
            return std::nullopt;
        }
        int asset = -1;
        if (auto it = backgroundImageAssets.find(pixbuf); it != backgroundImageAssets.end()) {
            asset = it->second;
        } else {
            auto png = pixbufToPng(const_cast<GdkPixbuf*>(pixbuf));
            if (!png) {
                warn(_("An image background could not be exported."));
                return std::nullopt;
            }
            asset = addAsset(std::move(*png));
            backgroundImageAssets[pixbuf] = asset;
        }
        return imageJson(pageIndex, ".png", 0, 0, w, h, gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf),
                         asset);
    }
    return std::nullopt;
}

bs::Array SbaExporter::textToQuill(std::vector<const Text*> texts) {
    // Saber's typed text flows from the top of the page, one line every `lineHeight`. Put each text box on the line
    // closest to its position, in top-to-bottom order.
    // The baseline of each text box decides its line: Saber's line k has its baseline just above y = (2 + k) * lh
    const double lh = lineHeight * SCALE;
    std::vector<std::pair<double, const Text*>> sorted;
    for (const Text* t: texts) {
        auto layout = t->createPangoLayout();
        const std::string& content = t->getText();
        pango_layout_set_text(layout.get(), content.c_str(), static_cast<int>(content.size()));
        const double baseline = pango_layout_get_baseline(layout.get()) / static_cast<double>(PANGO_SCALE);
        sorted.emplace_back(t->getTransformation().shift.y + baseline - shiftY, t);
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first < b.first :
                                    a.second->getBoundingBox().x < b.second->getBoundingBox().x;
    });

    std::string out;
    int line = 0;  // the line the next character goes to
    for (const auto& [baselineY, t]: sorted) {
        const std::string& content = t->getText();
        if (content.empty()) {
            continue;
        }
        const int target = std::max(
                0, static_cast<int>(std::round((baselineY + TEXT_BASELINE_GAP * lh) / lh - FIRST_RULING_LINE)));
        if (!out.empty()) {
            // Start each text box on a new line
            out += '\n';
            line++;
        }
        while (line < target) {
            out += '\n';
            line++;
        }
        out += content;
        line += static_cast<int>(std::count(content.begin(), content.end(), '\n'));
    }
    if (out.empty()) {
        return {};
    }
    out += '\n';  // Quill documents always end with a newline
    warn(_("Text boxes were converted to Saber's typed text. Their positions were adjusted to the lines of the page."));

    bs::Document op;
    op.push_back({"insert", out});
    return bs::Array{bs::Value(std::move(op))};
}
