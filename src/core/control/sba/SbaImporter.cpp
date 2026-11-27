#include "SbaImporter.h"

#include <algorithm>  // for max, clamp
#include <cmath>      // for cos, sin, round
#include <cstring>    // for memcpy
#include <locale>     // for locale
#include <numbers>    // for pi
#include <sstream>    // for ostringstream
#include <stdexcept>  // for runtime_error

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <zip.h>

#include "model/BackgroundImage.h"
#include "model/Document.h"
#include "model/Image.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfDocument.h"
#include "pdf/base/XojPdfPage.h"
#include "util/Color.h"
#include "util/Matrix.h"
#include "util/StringUtils.h"
#include "util/i18n.h"
#include "util/raii/CairoWrappers.h"
#include "util/raii/GObjectSPtr.h"

#include "SbaFormat.h"

using namespace xoj::sba;
namespace bs = xoj::sba::bson;

namespace {

/// Reads a whole zip entry
std::string readZipEntry(zip_t* zip, zip_uint64_t index) {
    zip_stat_t stat;
    if (zip_stat_index(zip, index, 0, &stat) != 0 || !(stat.valid & ZIP_STAT_SIZE)) {
        throw std::runtime_error("cannot read archive entry");
    }
    // Refuse absurd sizes instead of allocating them (zip bomb protection)
    constexpr zip_uint64_t MAX_ENTRY_SIZE = 2ULL * 1024 * 1024 * 1024;
    if (stat.size > MAX_ENTRY_SIZE) {
        throw std::runtime_error("archive entry too large");
    }
    std::string data(stat.size, '\0');
    zip_file_t* file = zip_fopen_index(zip, index, 0);
    if (!file) {
        throw std::runtime_error("cannot open archive entry");
    }
    zip_int64_t read = data.empty() ? 0 : zip_fread(file, data.data(), data.size());
    zip_fclose(file);
    if (read < 0 || static_cast<zip_uint64_t>(read) != stat.size) {
        throw std::runtime_error("cannot read archive entry");
    }
    return data;
}

bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

Color argbToColor(int64_t argb) { return Color(static_cast<uint32_t>(argb & 0xffffffff) | 0xff000000U); }

std::string formatConfig(std::initializer_list<std::pair<const char*, double>> values) {
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    bool first = true;
    for (auto& [key, value]: values) {
        if (!first) {
            ss << ',';
        }
        first = false;
        ss << key << '=' << value;
    }
    return ss.str();
}

/// Converts any image GdkPixbuf can read (PNG, JPEG, SVG, WebP...) to PNG, cropped to srcRect if given
std::optional<std::string> toPng(const std::string& data, double sx, double sy, double sw, double sh, double nw,
                                 double nh, bool* wasPngOrJpeg) {
    xoj::util::GObjectSPtr<GdkPixbufLoader> loader(gdk_pixbuf_loader_new(), xoj::util::adopt);
    GError* err = nullptr;
    if (!gdk_pixbuf_loader_write(loader.get(), reinterpret_cast<const guchar*>(data.data()), data.size(), &err) ||
        !gdk_pixbuf_loader_close(loader.get(), &err)) {
        if (err) {
            g_error_free(err);
        }
        return std::nullopt;
    }
    GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(loader.get());
    if (!pixbuf) {
        return std::nullopt;
    }
    GdkPixbufFormat* format = gdk_pixbuf_loader_get_format(loader.get());
    std::string formatName = format ? gdk_pixbuf_format_get_name(format) : "";
    const bool isPlain = formatName == "png" || formatName == "jpeg";

    const int pw = gdk_pixbuf_get_width(pixbuf);
    const int ph = gdk_pixbuf_get_height(pixbuf);
    bool crop = false;
    int cx = 0, cy = 0, cw = pw, ch = ph;
    if (sw > 0 && sh > 0) {
        // srcRect is given in the coordinates of the natural size
        const double fx = nw > 0 ? pw / nw : 1.0;
        const double fy = nh > 0 ? ph / nh : 1.0;
        cx = std::clamp(static_cast<int>(std::round(sx * fx)), 0, pw - 1);
        cy = std::clamp(static_cast<int>(std::round(sy * fy)), 0, ph - 1);
        cw = std::clamp(static_cast<int>(std::round(sw * fx)), 1, pw - cx);
        ch = std::clamp(static_cast<int>(std::round(sh * fy)), 1, ph - cy);
        crop = cx != 0 || cy != 0 || cw != pw || ch != ph;
    }

    if (isPlain && !crop) {
        *wasPngOrJpeg = true;
        return data;
    }
    *wasPngOrJpeg = false;

    xoj::util::GObjectSPtr<GdkPixbuf> sub(crop ? gdk_pixbuf_new_subpixbuf(pixbuf, cx, cy, cw, ch) :
                                                 GDK_PIXBUF(g_object_ref(pixbuf)),
                                          xoj::util::adopt);
    gchar* buffer = nullptr;
    gsize size = 0;
    if (!gdk_pixbuf_save_to_buffer(sub.get(), &buffer, &size, "png", &err, nullptr)) {
        if (err) {
            g_error_free(err);
        }
        return std::nullopt;
    }
    std::string png(buffer, size);
    g_free(buffer);
    return png;
}

struct SaberPoint {
    double x;
    double y;
    std::optional<double> pressure;
};

std::optional<SaberPoint> readPoint(const bs::Value& v, double ox, double oy) {
    if (const auto* bin = v.binary()) {
        // Float32List [x, y] or [x, y, pressure]
        const auto& d = bin->data;
        if (d.size() != 8 && d.size() != 12) {
            return std::nullopt;
        }
        float f[3] = {0, 0, 0};
        std::memcpy(f, d.data(), d.size());
        SaberPoint p{f[0] + ox, f[1] + oy, std::nullopt};
        if (d.size() == 12) {
            p.pressure = f[2];
        }
        return p;
    }
    if (const auto* doc = v.document()) {
        // File versions before 13: {x, y, p}
        SaberPoint p{bs::getNumber(*doc, "x", 0) + ox, bs::getNumber(*doc, "y", 0) + oy, std::nullopt};
        if (const auto* pr = bs::find(*doc, "p"); pr && pr->number()) {
            p.pressure = *pr->number();
        }
        return p;
    }
    return std::nullopt;
}

}  // namespace

SbaImporter::SbaImporter() = default;
SbaImporter::~SbaImporter() = default;

void SbaImporter::warn(std::string msg) {
    if (std::find(warnings.begin(), warnings.end(), msg) == warnings.end()) {
        warnings.push_back(std::move(msg));
    }
}

auto SbaImporter::load(const fs::path& filepath) -> std::unique_ptr<Document> {
    int zipError = 0;
    zip_t* zip = zip_open(char_cast(filepath.u8string().c_str()), ZIP_RDONLY, &zipError);
    if (!zip) {
        if (zipError == ZIP_ER_NOENT) {
            throw std::runtime_error(FS(_F("The file \"{1}\" does not exist") % filepath.u8string()));
        }
        if (zipError == ZIP_ER_OPEN || zipError == ZIP_ER_READ) {
            throw std::runtime_error(FS(_F("The file \"{1}\" could not be read") % filepath.u8string()));
        }
        throw std::runtime_error(_("The file is not a Saber archive (not a zip file)"));
    }

    std::string mainFile;
    bool foundMain = false;
    bool oldJson = false;
    std::map<int, std::string> assetData;
    try {
        const zip_int64_t count = zip_get_num_entries(zip, 0);
        // The main note is the first entry ending in .sbn2 (or .sbn for very old notes). Assets are named
        // "<main>.<number>".
        std::string mainName;
        for (zip_int64_t i = 0; i < count; i++) {
            const char* name = zip_get_name(zip, static_cast<zip_uint64_t>(i), 0);
            if (!name) {
                continue;
            }
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            if (endsWith(lower, ".sbn2")) {
                mainName = name;
                mainFile = readZipEntry(zip, static_cast<zip_uint64_t>(i));
                foundMain = true;
                break;
            }
            if (endsWith(lower, ".sbn")) {
                oldJson = true;
            }
        }
        if (foundMain) {
            for (zip_int64_t i = 0; i < count; i++) {
                const char* name = zip_get_name(zip, static_cast<zip_uint64_t>(i), 0);
                if (!name) {
                    continue;
                }
                std::string_view n = name;
                if (n.size() <= mainName.size() + 1 || n.substr(0, mainName.size()) != mainName ||
                    n[mainName.size()] != '.') {
                    continue;
                }
                auto suffix = n.substr(mainName.size() + 1);
                if (suffix.empty() || !std::all_of(suffix.begin(), suffix.end(), [](char c) { return std::isdigit(c); }) ||
                    suffix.size() > 6) {
                    continue;  // e.g. the preview image "main.sbn2.p"
                }
                assetData[std::stoi(std::string(suffix))] = readZipEntry(zip, static_cast<zip_uint64_t>(i));
            }
        }
    } catch (const std::exception& e) {
        zip_discard(zip);
        throw std::runtime_error(FS(_F("Could not read the Saber archive: {1}") % e.what()));
    }
    zip_discard(zip);

    if (!foundMain) {
        if (oldJson) {
            throw std::runtime_error(_("This Saber note uses an old file format (.sbn). Open it in a current version "
                                       "of Saber and export it again."));
        }
        throw std::runtime_error(_("The archive does not contain a Saber note"));
    }

    return loadFromContents(mainFile, std::move(assetData), filepath);
}

auto SbaImporter::loadFromContents(const std::string& mainFile, std::map<int, std::string> assetData,
                                   const fs::path& filepath) -> std::unique_ptr<Document> {
    this->assets = std::move(assetData);

    bs::Document root;
    try {
        root = bs::parse(mainFile);
    } catch (const bs::ParseError& e) {
        throw std::runtime_error(FS(_F("The Saber note is damaged: {1}") % e.what()));
    }

    this->doc = std::make_unique<Document>(&this->dHandler);
    // The PDF background is attached to the document. Name it after the note, so that "Save as" suggests that name.
    fs::path pdfName = filepath;
    pdfName.replace_extension(".pdf");
    this->pdfName = pdfName;

    fileVersion = static_cast<int>(bs::getInteger(root, "v", 0));
    if (fileVersion > SBN_VERSION) {
        warn(_("The Saber note was made by a newer version of Saber. Some content may be missing."));
    }
    if (const auto* b = bs::find(root, "b"); b && b->integer()) {
        backgroundColor = static_cast<uint32_t>(*b->integer() & 0xffffffff);
    }
    if (const auto* p = bs::getString(root, "p")) {
        pattern = *p;
    }
    lineHeight = static_cast<int>(std::clamp<int64_t>(bs::getInteger(root, "l", DEFAULT_LINE_HEIGHT), 4, 1000));

    if (const auto* pagesJson = bs::getArray(root, "z")) {
        for (size_t i = 0; i < pagesJson->size(); i++) {
            const auto& pageValue = (*pagesJson)[i];
            if (const auto* page = pageValue.document()) {
                readPage(*page, static_cast<int>(i));
            } else if (const auto* wh = pageValue.array(); wh && wh->size() >= 2) {
                // Old format: [width, height]
                bs::Document page{{"w", (*wh)[0].number().value_or(DEFAULT_PAGE_WIDTH)},
                                  {"h", (*wh)[1].number().value_or(DEFAULT_PAGE_HEIGHT)}};
                readPage(page, static_cast<int>(i));
            }
        }
    }

    // File versions before 8 stored the strokes and images of all pages at the top level, with a page index
    const auto* legacyStrokes = bs::getArray(root, "s");
    const auto* legacyImages = bs::getArray(root, "i");
    if (legacyStrokes || legacyImages) {
        auto pageFor = [&](const bs::Document& e) -> PageRef {
            auto idx = static_cast<size_t>(std::max<int64_t>(0, bs::getInteger(e, "i", 0)));
            while (idx >= pages.size()) {
                readPage(bs::Document{}, static_cast<int>(pages.size()));
            }
            return pages[idx];
        };
        if (legacyImages) {
            for (const auto& v: *legacyImages) {
                if (const auto* img = v.document()) {
                    auto page = pageFor(*img);
                    readImage(*img, page->getLayers().front(), SCALE);
                }
            }
        }
        if (legacyStrokes) {
            for (const auto& v: *legacyStrokes) {
                if (const auto* s = v.document()) {
                    auto page = pageFor(*s);
                    readStroke(*s, page->getLayers().front(), SCALE);
                }
            }
        }
    }

    // Saber always keeps an empty page at the end of a note. Drop trailing empty pages, but keep at least one.
    while (pages.size() > 1 && pages.back()->getLayers().front()->getElements().empty() &&
           !pages.back()->getBackgroundType().isSpecial()) {
        pages.pop_back();
    }
    if (pages.empty()) {
        readPage(bs::Document{}, 0);
    }

    doc->addPages(pages.begin(), pages.end());
    pages.clear();
    doc->setFilepath("");
    return std::move(doc);
}

const std::string* SbaImporter::getAsset(int index) {
    auto it = assets.find(index);
    if (it == assets.end()) {
        warn(_("The Saber note references a missing image or PDF."));
        return nullptr;
    }
    return &it->second;
}

void SbaImporter::readPage(const bs::Document& pageJson, int pageIndex) {
    const double w = std::clamp(bs::getNumber(pageJson, "w", DEFAULT_PAGE_WIDTH), 1.0, 100000.0);
    const double h = std::clamp(bs::getNumber(pageJson, "h", DEFAULT_PAGE_HEIGHT), 1.0, 100000.0);
    double scale = SCALE;

    // XojPage creates the first layer itself
    auto page = std::make_shared<XojPage>(w * scale, h * scale);
    Layer* layer = page->getLayers().front();
    pages.push_back(page);

    if (const auto* bg = bs::getDocument(pageJson, "b")) {
        readBackgroundImage(*bg, page);
    } else {
        setPatternBackground(page);
    }

    if (page->getBackgroundType().isPdfPage()) {
        // Saber scales PDF pages to the area of a standard page. Use the real size of the PDF page instead (the PDF
        // would not fit the page otherwise), and scale the content accordingly.
        if (auto pdfPage = doc->getPdfPage(page->getPdfPageNr()); pdfPage && pdfPage->getWidth() > 0) {
            scale = pdfPage->getWidth() / w;
            page->setSize(pdfPage->getWidth(), pdfPage->getHeight());
        }
    }

    if (const auto* images = bs::getArray(pageJson, "i")) {
        for (const auto& v: *images) {
            if (const auto* img = v.document()) {
                readImage(*img, layer, scale);
            }
        }
    }
    if (const auto* strokes = bs::getArray(pageJson, "s")) {
        for (const auto& v: *strokes) {
            if (const auto* s = v.document()) {
                readStroke(*s, layer, scale);
            }
        }
    }
    if (const auto* quill = bs::getArray(pageJson, "q")) {
        readText(*quill, layer, scale, w);
    }

    if (const double shift = rulingShift(page->getBackgroundType()); shift != 0) {
        for (auto& e: layer->getElements()) {
            e->move(0, shift);
        }
    }
    (void)pageIndex;
}

void SbaImporter::setPatternBackground(const PageRef& page) {
    const double spacing = lineHeight * SCALE;
    PageType type(PageTypeFormat::Plain);
    if (pattern == "lined") {
        type = PageType(PageTypeFormat::Ruled);
        type.config = formatConfig({{"r1", spacing}});
    } else if (pattern == "college") {
        type = PageType(PageTypeFormat::Lined);
        type.config = formatConfig({{"r1", spacing}, {"m1", 2 * spacing}});
    } else if (pattern == "college-rtl") {
        type = PageType(PageTypeFormat::Lined);
        type.config = formatConfig({{"r1", spacing}, {"m1", -2 * spacing}});
    } else if (pattern == "grid") {
        type = PageType(PageTypeFormat::Graph);
        type.config = formatConfig({{"r1", spacing}});
    } else if (pattern == "dots") {
        type = PageType(PageTypeFormat::Dotted);
        type.config = formatConfig({{"r1", spacing}});
    } else if (pattern == "staffs") {
        type = PageType(PageTypeFormat::Staves);
    } else if (pattern == "tablature" || pattern == "cornell") {
        type = PageType(PageTypeFormat::Ruled);
        type.config = formatConfig({{"r1", spacing}});
        warn(_("The Saber background pattern (tablature or Cornell notes) has no counterpart and was replaced by "
               "lines."));
    } else if (!pattern.empty()) {
        warn(FS(_F("Unknown Saber background pattern \"{1}\"") % pattern));
    }
    page->setBackgroundType(type);
    page->setBackgroundColor(Color(backgroundColor | 0xff000000U));
}

void SbaImporter::readBackgroundImage(const bs::Document& image, const PageRef& page) {
    const auto* ext = bs::getString(image, "e");
    const int asset = static_cast<int>(bs::getInteger(image, "a", -1));

    if (ext && *ext == ".pdf") {
        const int pdfPage = static_cast<int>(std::max<int64_t>(0, bs::getInteger(image, "pdfi", 0)));
        if (backgroundPdfAsset == -1) {
            if (const auto* bytes = getAsset(asset)) {
                if (doc->readPdf(pdfName, false, /*attach*/ true, std::make_unique<std::string>(*bytes))) {
                    backgroundPdfAsset = asset;
                } else {
                    warn(FS(_F("Could not read a PDF in the Saber note: {1}") % doc->getLastErrorMsg()));
                }
            }
        }
        if (asset == backgroundPdfAsset && backgroundPdfAsset != -1) {
            if (static_cast<size_t>(pdfPage) < doc->getPdfPageCount()) {
                page->setBackgroundPdfPageNr(static_cast<size_t>(pdfPage));
            } else {
                warn(_("The Saber note references a PDF page that does not exist."));
                setPatternBackground(page);
            }
            return;
        }
        // A second PDF: a document can only have one background PDF, so use a picture of the page
        std::string png = rasterizePdfPage(asset, pdfPage);
        if (png.empty()) {
            setPatternBackground(page);
            return;
        }
        warn(_("The Saber note contains more than one PDF. The pages of the additional PDFs were imported as "
               "pictures."));
        BackgroundImage img;
        xoj::util::GObjectSPtr<GInputStream> stream(
                g_memory_input_stream_new_from_data(g_memdup2(png.data(), png.size()), static_cast<gssize>(png.size()),
                                                    g_free),
                xoj::util::adopt);
        GError* err = nullptr;
        img.loadFile(stream.get(), fs::path("saber_pdf_page.png"), &err);
        if (err) {
            g_error_free(err);
            setPatternBackground(page);
            return;
        }
        img.setAttach(true);
        page->setBackgroundImage(std::move(img));
        page->setBackgroundType(PageType(PageTypeFormat::Image));
        return;
    }

    // Image background
    const auto* bytes = getAsset(asset);
    bool plain = false;
    auto png = bytes ? toPng(*bytes, 0, 0, 0, 0, 0, 0, &plain) : std::nullopt;
    if (!png) {
        if (bytes) {
            warn(_("Could not read an image in the Saber note."));
        }
        setPatternBackground(page);
        return;
    }
    BackgroundImage img;
    xoj::util::GObjectSPtr<GInputStream> stream(
            g_memory_input_stream_new_from_data(g_memdup2(png->data(), png->size()), static_cast<gssize>(png->size()),
                                                g_free),
            xoj::util::adopt);
    GError* err = nullptr;
    img.loadFile(stream.get(), fs::path("saber_background.png"), &err);
    if (err) {
        g_error_free(err);
        warn(_("Could not read an image in the Saber note."));
        setPatternBackground(page);
        return;
    }
    img.setAttach(true);
    page->setBackgroundImage(std::move(img));
    page->setBackgroundType(PageType(PageTypeFormat::Image));
}

std::string SbaImporter::rasterizePdfPage(int assetIndex, int pdfPage) {
    auto it = otherPdfs.find(assetIndex);
    if (it == otherPdfs.end()) {
        const auto* bytes = getAsset(assetIndex);
        if (!bytes) {
            return {};
        }
        auto pdf = std::make_unique<XojPdfDocument>();
        GError* err = nullptr;
        if (!pdf->load(std::make_unique<std::string>(*bytes), "", &err)) {
            if (err) {
                g_error_free(err);
            }
            warn(_("Could not read a PDF in the Saber note."));
            return {};
        }
        it = otherPdfs.emplace(assetIndex, std::move(pdf)).first;
    }
    if (static_cast<size_t>(pdfPage) >= it->second->getPageCount()) {
        return {};
    }
    auto page = it->second->getPage(static_cast<size_t>(pdfPage));
    constexpr double ZOOM = 2.0;  // 144 dpi
    const int pw = std::max(1, static_cast<int>(page->getWidth() * ZOOM));
    const int ph = std::max(1, static_cast<int>(page->getHeight() * ZOOM));
    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_RGB24, pw, ph), xoj::util::adopt);
    xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);
    cairo_set_source_rgb(cr.get(), 1, 1, 1);
    cairo_paint(cr.get());
    cairo_scale(cr.get(), ZOOM, ZOOM);
    page->render(cr.get());
    cairo_surface_flush(surface.get());
    return surfaceToPng(surface.get()).value_or("");
}

void SbaImporter::readStroke(const bs::Document& s, Layer* layer, double scale) {
    const std::string* shape = bs::getString(s, "shape");
    const std::string* type = bs::getString(s, "ty");
    const std::string tool = type ? *type : std::string("fountainPen");

    const double size = std::max(0.01, bs::getNumber(s, "s", DEFAULT_STROKE_SIZE));
    const double thinning = std::clamp(bs::getNumber(s, "t", DEFAULT_THINNING), -1.0, 1.0);
    const bool pressureEnabled = bs::getBool(s, "pe", true);
    const auto* colorValue = bs::find(s, "c");
    const Color color =
            colorValue && colorValue->integer() ? argbToColor(*colorValue->integer()) : Color(0xff000000U);

    std::vector<SaberPoint> points;
    if (shape && *shape == "circle") {
        const double cx = bs::getNumber(s, "cx", 0), cy = bs::getNumber(s, "cy", 0), r = bs::getNumber(s, "r", 0);
        constexpr int N = 72;
        for (int i = 0; i <= N; i++) {
            const double a = 2 * std::numbers::pi * i / N;
            points.push_back({cx + r * std::cos(a), cy + r * std::sin(a), std::nullopt});
        }
    } else if (shape && *shape == "rect") {
        const double l = bs::getNumber(s, "rl", 0), t = bs::getNumber(s, "rt", 0);
        const double r = l + bs::getNumber(s, "rw", 0), b = t + bs::getNumber(s, "rh", 0);
        points = {{l, t, {}}, {r, t, {}}, {r, b, {}}, {l, b, {}}, {l, t, {}}};
    } else {
        const double ox = bs::getNumber(s, "ox", 0), oy = bs::getNumber(s, "oy", 0);
        if (const auto* pts = bs::getArray(s, "p")) {
            points.reserve(pts->size());
            for (const auto& v: *pts) {
                if (auto p = readPoint(v, ox, oy); p && std::isfinite(p->x) && std::isfinite(p->y)) {
                    points.push_back(*p);
                }
            }
        }
    }
    if (points.empty()) {
        return;
    }
    if (points.size() == 1) {
        // A dot: Xournal++ strokes need two points
        points.push_back(points.front());
    }

    auto stroke = std::make_unique<Stroke>();
    const bool isHighlighter = tool == "Highlighter";
    stroke->setToolType(isHighlighter ? StrokeTool::HIGHLIGHTER : StrokeTool::PEN);
    stroke->setColor(color);
    stroke->setWidth(size * scale);

    // perfect_freehand: diameter = size * (1 - thinning * (1 - 2 * pressure))
    const bool hasPressure = !isHighlighter && pressureEnabled && thinning != 0 &&
                             std::all_of(points.begin(), points.end(), [](auto& p) { return p.pressure.has_value(); });
    auto diameter = [&](const SaberPoint& p) {
        const double pr = std::clamp(*p.pressure, 0.0, 1.0);
        return std::max(0.05, size * (1 - thinning * (1 - 2 * pr))) * scale;
    };

    std::vector<Point> xpts;
    xpts.reserve(points.size());
    for (size_t i = 0; i < points.size(); i++) {
        double z = Point::NO_PRESSURE;
        if (hasPressure && i + 1 < points.size()) {
            z = 0.5 * (diameter(points[i]) + diameter(points[i + 1]));
        }
        xpts.emplace_back(points[i].x * scale, points[i].y * scale, z);
    }
    stroke->setPointVector(std::move(xpts));
    if (tool == "Pencil") {
        warn(_("Pencil strokes were imported as pen strokes (the pencil texture is not available)."));
    }
    layer->addElement(std::move(stroke));
}

void SbaImporter::readImage(const bs::Document& image, Layer* layer, double scale) {
    const auto* ext = bs::getString(image, "e");
    const int asset = static_cast<int>(bs::getInteger(image, "a", -1));
    const double x = bs::getNumber(image, "x", 0), y = bs::getNumber(image, "y", 0);
    const double w = bs::getNumber(image, "w", 0), h = bs::getNumber(image, "h", 0);
    if (w <= 0 || h <= 0) {
        return;
    }

    std::optional<std::string> png;
    if (ext && *ext == ".pdf") {
        const int pdfPage = static_cast<int>(std::max<int64_t>(0, bs::getInteger(image, "pdfi", 0)));
        auto data = rasterizePdfPage(asset, pdfPage);
        if (!data.empty()) {
            png = std::move(data);
        }
    } else if (const auto* bytes = getAsset(asset)) {
        bool plain = false;
        png = toPng(*bytes, bs::getNumber(image, "sx", 0), bs::getNumber(image, "sy", 0), bs::getNumber(image, "sw", 0),
                    bs::getNumber(image, "sh", 0), bs::getNumber(image, "nw", 0), bs::getNumber(image, "nh", 0),
                    &plain);
    } else {
        return;
    }
    if (!png) {
        warn(_("Could not read an image in the Saber note."));
        return;
    }

    auto img = std::make_unique<Image>();
    img->setImage(std::move(*png));
    auto [nw, nh] = img->getNaturalSize();
    if (nw <= 0 || nh <= 0) {
        warn(_("Could not read an image in the Saber note."));
        return;
    }
    img->setTransformation(xoj::util::Matrix::SCALING(w * scale / nw, h * scale / nh));
    img->move(x * scale, y * scale);
    layer->addElement(std::move(img));
}

void SbaImporter::readText(const bs::Array& ops, Layer* layer, double scale, double pageWidth) {
    std::string all;
    bool hadFormatting = false;
    for (const auto& op: ops) {
        const auto* d = op.document();
        if (!d) {
            continue;
        }
        if (const auto* ins = bs::getString(*d, "insert")) {
            all += *ins;
        }
        if (bs::find(*d, "attributes")) {
            hadFormatting = true;
        }
    }

    // Split into lines; consecutive non-empty lines become one text box placed on the line where it starts
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= all.size()) {
        auto end = all.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(all.substr(start));
            break;
        }
        lines.push_back(all.substr(start, end - start));
        start = end + 1;
    }

    // Every line becomes its own text box, placed exactly on its line of the page. A line that wraps takes several
    // lines, and pushes the following ones down (as in Saber).
    const double lh = lineHeight * scale;
    bool any = false;
    size_t visualLine = 0;
    for (const auto& line: lines) {
        if (line.find_first_not_of(" \t") == std::string::npos) {
            visualLine++;
            continue;
        }
        auto text = std::make_unique<Text>();
        text->getFont().setName(TEXT_FONT);
        text->getFont().setSize(lh * TEXT_FONT_SIZE);
        text->setColor(Color(0xff000000U));
        text->setWrap((pageWidth - lineHeight * 2 * TEXT_LEFT) * scale);
        text->setText(line);
        auto layout = text->createPangoLayout();
        pango_layout_set_text(layout.get(), line.c_str(), static_cast<int>(line.size()));
        const int wrappedLines = std::max(1, pango_layout_get_line_count(layout.get()));
        const double baseline = pango_layout_get_baseline(layout.get()) / static_cast<double>(PANGO_SCALE);
        if (wrappedLines == 1) {
            // Keep the wrap width only where it matters: text boxes without one can be stored in file format version 4
            text->setWrap(Text::NO_WRAP);
        }

        // Saber's line k (starting at TEXT_TOP) has its ruling line at FIRST_RULING_LINE + k
        const double ruling = (FIRST_RULING_LINE * lineHeight) * scale + static_cast<double>(visualLine) * lh;
        text->setTransformation(xoj::util::Matrix::TRANSLATION(lineHeight * TEXT_LEFT * scale,
                                                               ruling - TEXT_BASELINE_GAP * lh - baseline));
        visualLine += static_cast<size_t>(wrappedLines);
        layer->addElement(std::move(text));
        any = true;
    }
    if (any && hadFormatting) {
        warn(_("Text formatting (bold, headings, lists...) of typed text was not imported."));
    }
}
