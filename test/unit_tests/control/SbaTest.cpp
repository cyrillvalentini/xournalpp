/*
 * Xournal++
 *
 * Tests for the Saber (.sba) import and export
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "control/sba/Bson.h"
#include "control/sba/SbaExporter.h"
#include "control/sba/SbaFormat.h"
#include "control/sba/SbaImporter.h"
#include "model/BackgroundConfig.h"
#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/Image.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "util/Matrix.h"
#include "util/raii/CairoWrappers.h"

using namespace xoj::sba;
namespace bs = xoj::sba::bson;

TEST(SbaBson, roundTrip) {
    bs::Document doc;
    doc.push_back({"d", 1.5});
    doc.push_back({"s", "héllo"});
    doc.push_back({"i", static_cast<int32_t>(-7)});
    doc.push_back({"l", static_cast<int64_t>(0xff112233LL)});
    doc.push_back({"t", true});
    doc.push_back({"n", nullptr});
    doc.push_back({"b", bs::Binary{std::string("\0\1\2", 3)}});
    bs::Document inner;
    inner.push_back({"x", 2.0});
    doc.push_back({"a", bs::Array{bs::Value(inner), bs::Value(static_cast<int32_t>(3))}});

    auto parsed = bs::parse(bs::serialize(doc));
    ASSERT_EQ(parsed.size(), doc.size());
    EXPECT_DOUBLE_EQ(bs::getNumber(parsed, "d", 0), 1.5);
    EXPECT_EQ(*bs::getString(parsed, "s"), "héllo");
    EXPECT_EQ(bs::getInteger(parsed, "i", 0), -7);
    EXPECT_EQ(bs::getInteger(parsed, "l", 0), 0xff112233LL);
    EXPECT_TRUE(bs::getBool(parsed, "t", false));
    EXPECT_TRUE(bs::find(parsed, "n")->isNull());
    EXPECT_EQ(bs::find(parsed, "b")->binary()->data, std::string("\0\1\2", 3));
    const auto* arr = bs::getArray(parsed, "a");
    ASSERT_NE(arr, nullptr);
    ASSERT_EQ(arr->size(), 2u);
    EXPECT_DOUBLE_EQ(bs::getNumber(*(*arr)[0].document(), "x", 0), 2.0);
    EXPECT_EQ((*arr)[1].integer().value(), 3);
}

TEST(SbaBson, rejectsGarbage) {
    EXPECT_THROW(bs::parse("abc"), bs::ParseError);
    std::string truncated = bs::serialize({{"x", 1.0}});
    truncated.resize(truncated.size() - 3);
    EXPECT_THROW(bs::parse(truncated), bs::ParseError);
}

namespace {
std::unique_ptr<Stroke> makeStroke(StrokeTool tool, Color color, double width, bool pressure) {
    auto s = std::make_unique<Stroke>();
    s->setToolType(tool);
    s->setColor(color);
    s->setWidth(width);
    std::vector<Point> pts;
    for (int i = 0; i < 20; i++) {
        double z = pressure && i < 19 ? width * (0.8 + 0.01 * i) : Point::NO_PRESSURE;
        pts.emplace_back(100 + 10 * i, 200 + 3 * i, z);
    }
    s->setPointVector(std::move(pts));
    return s;
}

std::string makePng() {
    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 4), xoj::util::adopt);
    xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);
    cairo_set_source_rgb(cr.get(), 1, 0, 0);
    cairo_paint(cr.get());
    cairo_surface_flush(surface.get());
    return surfaceToPng(surface.get()).value();
}

std::vector<Element*> elements(const PageRef& page) {
    std::vector<Element*> res;
    for (Layer* l: page->getLayers()) {
        for (auto& e: l->getElements()) {
            res.push_back(e.get());
        }
    }
    return res;
}
}  // namespace

TEST(Sba, exportImportRoundTrip) {
    DocumentHandler handler;
    Document doc(&handler);
    auto page = std::make_shared<XojPage>(595.2755905511812, 833.0);
    PageType ruled(PageTypeFormat::Ruled);
    ruled.config = "r1=23.811";
    page->setBackgroundType(ruled);
    Layer* layer = page->getLayers().front();
    layer->addElement(makeStroke(StrokeTool::HIGHLIGHTER, Color(0xffffff00U), 8, false));
    layer->addElement(makeStroke(StrokeTool::PEN, Color(0xff0000ffU), 2, true));
    layer->addElement(makeStroke(StrokeTool::PEN, Color(0xff00ff00U), 1.5, false));

    auto text = std::make_unique<Text>();
    text->getFont().setName("Sans");
    text->getFont().setSize(12);
    text->setText("Hello\nSaber");
    // Third line of the page: top = (1.2 + 2) * lineHeight
    text->setTransformation(xoj::util::Matrix::TRANSLATION(12, (1.2 + 2) * 40 * SCALE));
    layer->addElement(std::move(text));

    auto img = std::make_unique<Image>();
    img->setImage(makePng());
    img->setTransformation(xoj::util::Matrix::SCALING(10, 10));
    img->move(50, 60);
    layer->addElement(std::move(img));
    doc.addPage(page);

    SbaExporter exporter;
    std::string mainFile;
    std::vector<std::string> assetFiles;
    ASSERT_TRUE(exporter.build(&doc, {}, mainFile, assetFiles)) << exporter.getError();
    ASSERT_EQ(assetFiles.size(), 1u);

    // Check a few raw values Saber relies on
    auto root = bs::parse(mainFile);
    EXPECT_EQ(bs::getInteger(root, "v", 0), SBN_VERSION);
    EXPECT_EQ(*bs::getString(root, "p"), "lined");
    EXPECT_EQ(bs::getInteger(root, "l", 0), 40);
    const auto* pagesJson = bs::getArray(root, "z");
    ASSERT_EQ(pagesJson->size(), 1u);
    const auto& pageJson = *(*pagesJson)[0].document();
    EXPECT_NEAR(bs::getNumber(pageJson, "w", 0), 1000.0, 1e-9);
    // Saber reads the page size as doubles
    EXPECT_TRUE(std::holds_alternative<double>(bs::find(pageJson, "w")->v));
    const auto* quill = bs::getArray(pageJson, "q");
    ASSERT_NE(quill, nullptr);
    const std::string quillText = *bs::getString(*(*quill)[0].document(), "insert");
    EXPECT_TRUE(quillText.ends_with("Hello\nSaber\n")) << quillText;

    std::map<int, std::string> assets;
    for (size_t i = 0; i < assetFiles.size(); i++) {
        assets[static_cast<int>(i)] = assetFiles[i];
    }
    SbaImporter importer;
    auto back = importer.loadFromContents(mainFile, std::move(assets), "note.sba");
    ASSERT_EQ(back->getPageCount(), 1u);
    auto p = back->getPage(0);
    EXPECT_NEAR(p->getWidth(), page->getWidth(), 1e-6);
    EXPECT_NEAR(p->getHeight(), page->getHeight(), 1e-6);
    EXPECT_EQ(p->getBackgroundType().format, PageTypeFormat::Ruled);
    double spacing = 0;
    BackgroundConfig(p->getBackgroundType().config).loadValue("r1", spacing);
    EXPECT_NEAR(spacing, 40 * SCALE, 1e-3);

    auto els = elements(p);
    // The importer puts images first, then strokes, then text (one text box per line)
    ASSERT_EQ(els.size(), 6u);
    ASSERT_EQ(els[0]->getType(), ELEMENT_IMAGE);
    EXPECT_NEAR(els[0]->getBoundingBox().x, 50, 1e-3);
    EXPECT_NEAR(els[0]->getBoundingBox().width, 80, 1e-3);

    auto* hl = dynamic_cast<Stroke*>(els[1]);
    ASSERT_NE(hl, nullptr);
    EXPECT_EQ(hl->getToolType(), StrokeTool::HIGHLIGHTER);
    EXPECT_EQ(static_cast<uint32_t>(hl->getColor()) & 0xffffff, 0xffff00u);
    EXPECT_NEAR(hl->getWidth(), 8, 1e-3);

    auto* pen = dynamic_cast<Stroke*>(els[2]);
    ASSERT_NE(pen, nullptr);
    EXPECT_EQ(pen->getToolType(), StrokeTool::PEN);
    EXPECT_TRUE(pen->hasPressure());
    // The export adds points between points that are far apart (for Saber's zoomed-out rendering)
    const auto& pp = pen->getPointVector();
    ASSERT_GT(pp.size(), 20u);
    EXPECT_NEAR(pp.front().x, 100, 1e-3);
    EXPECT_NEAR(pp.front().y, 200, 1e-3);
    EXPECT_NEAR(pp.back().x, 290, 1e-3);
    EXPECT_NEAR(pp.back().y, 257, 1e-3);
    for (const auto& q: pp) {
        // The original points are on a straight line, so the added ones must be too
        EXPECT_NEAR(q.y, 200 + 0.3 * (q.x - 100), 1e-3);
    }
    for (size_t i = 0; i + 1 < pp.size(); i++) {
        // About 2 Saber units apart (not exactly: the curve parameter is not spaced evenly at the ends)
        EXPECT_LE(std::hypot(pp[i + 1].x - pp[i].x, pp[i + 1].y - pp[i].y), 3 * SCALE);
    }
    // Widths are interpolated: around x = 150 (original point 5) the width is close to the original one
    auto near150 = std::min_element(pp.begin(), pp.end(),
                                    [](const Point& a, const Point& b) { return std::abs(a.x - 150) < std::abs(b.x - 150); });
    EXPECT_NEAR(near150->z, 2 * (0.8 + 0.01 * 5), 0.01);

    auto* plain = dynamic_cast<Stroke*>(els[3]);
    ASSERT_NE(plain, nullptr);
    EXPECT_FALSE(plain->hasPressure());
    EXPECT_NEAR(plain->getWidth(), 1.5, 1e-3);

    auto* t1 = dynamic_cast<Text*>(els[4]);
    auto* t2 = dynamic_cast<Text*>(els[5]);
    ASSERT_NE(t1, nullptr);
    ASSERT_NE(t2, nullptr);
    EXPECT_EQ(t1->getText(), "Hello");
    EXPECT_EQ(t2->getText(), "Saber");
    EXPECT_NEAR(t2->getTransformation().shift.y - t1->getTransformation().shift.y, 40 * SCALE, 1e-3);

    // Exporting the imported note again gives the same text, on the same lines
    SbaExporter exporter2;
    std::string mainFile2;
    std::vector<std::string> assetFiles2;
    ASSERT_TRUE(exporter2.build(back.get(), {}, mainFile2, assetFiles2));
    auto root2 = bs::parse(mainFile2);
    const auto& page2 = *(*bs::getArray(root2, "z"))[0].document();
    EXPECT_EQ(*bs::getString(*(*bs::getArray(page2, "q"))[0].document(), "insert"), quillText);
}

TEST(Sba, dropsTrailingEmptyPageAndReadsShapes) {
    bs::Document circle;
    circle.push_back({"shape", "circle"});
    circle.push_back({"cx", 100.0});
    circle.push_back({"cy", 100.0});
    circle.push_back({"r", 50.0});
    circle.push_back({"ty", "ShapePen"});
    circle.push_back({"c", static_cast<int64_t>(0xffff0000LL)});

    bs::Document page1;
    page1.push_back({"w", 1000.0});
    page1.push_back({"h", 1400.0});
    page1.push_back({"s", bs::Array{bs::Value(circle)}});
    bs::Document page2;
    page2.push_back({"w", 1000.0});
    page2.push_back({"h", 1400.0});

    bs::Document root;
    root.push_back({"v", static_cast<int32_t>(19)});
    root.push_back({"p", "grid"});
    root.push_back({"z", bs::Array{bs::Value(page1), bs::Value(page2)}});

    SbaImporter importer;
    auto doc = importer.loadFromContents(bs::serialize(root), {}, "x.sba");
    ASSERT_EQ(doc->getPageCount(), 1u);
    auto p = doc->getPage(0);
    EXPECT_EQ(p->getBackgroundType().format, PageTypeFormat::Graph);
    auto els = elements(p);
    ASSERT_EQ(els.size(), 1u);
    auto* s = dynamic_cast<Stroke*>(els[0]);
    ASSERT_NE(s, nullptr);
    EXPECT_NEAR(s->getBoundingBox().width, 100 * SCALE + s->getWidth(), 0.1);
    EXPECT_EQ(static_cast<uint32_t>(s->getColor()) & 0xffffff, 0xff0000u);
}

/**
 * Converts every note in $XOPP_SABER_SAMPLES (Saber's test/sbn_examples or test/demo_notes) and checks that a round
 * trip keeps the number of strokes. Skipped when the variable is not set, as the samples are not part of this
 * repository.
 */
TEST(Sba, saberSamples) {
    const char* dir = std::getenv("XOPP_SABER_SAMPLES");
    if (!dir) {
        GTEST_SKIP() << "XOPP_SABER_SAMPLES not set";
    }
    auto read = [](const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    int count = 0;
    for (const auto& entry: fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".sbn2") {
            continue;
        }
        count++;
        std::map<int, std::string> assets;
        for (int i = 0; i < 100; i++) {
            fs::path a = entry.path();
            a += "." + std::to_string(i);
            if (fs::exists(a)) {
                assets[i] = read(a);
            }
        }
        SbaImporter importer;
        std::unique_ptr<Document> doc;
        ASSERT_NO_THROW(doc = importer.loadFromContents(read(entry.path()), assets, entry.path()))
                << entry.path();
        size_t strokes = 0;
        for (size_t i = 0; i < doc->getPageCount(); i++) {
            for (auto* e: elements(doc->getPage(i))) {
                strokes += e->getType() == ELEMENT_STROKE;
            }
        }
        std::cout << entry.path().filename() << ": " << doc->getPageCount() << " pages, " << strokes << " strokes";
        for (const auto& w: importer.getWarnings()) {
            std::cout << "\n    " << w;
        }
        std::cout << std::endl;

        SbaExporter exporter;
        std::string mainFile;
        std::vector<std::string> assetFiles;
        ASSERT_TRUE(exporter.build(doc.get(), {}, mainFile, assetFiles)) << exporter.getError();
        std::map<int, std::string> assets2;
        for (size_t i = 0; i < assetFiles.size(); i++) {
            assets2[static_cast<int>(i)] = assetFiles[i];
        }
        SbaImporter importer2;
        auto doc2 = importer2.loadFromContents(mainFile, assets2, entry.path());
        size_t strokes2 = 0;
        for (size_t i = 0; i < doc2->getPageCount(); i++) {
            for (auto* e: elements(doc2->getPage(i))) {
                strokes2 += e->getType() == ELEMENT_STROKE;
            }
        }
        EXPECT_EQ(doc2->getPageCount(), doc->getPageCount()) << entry.path();
        EXPECT_EQ(strokes2, strokes) << entry.path();
    }
    EXPECT_GT(count, 0);
}

TEST(Sba, writesAndReadsArchive) {
    DocumentHandler handler;
    Document doc(&handler);
    auto page = std::make_shared<XojPage>(595.0, 842.0);
    page->getLayers().front()->addElement(makeStroke(StrokeTool::PEN, Color(0xff000000U), 2, true));
    doc.addPage(page);

    gchar* dir = g_dir_make_tmp("xopp-sba-XXXXXX", nullptr);
    ASSERT_NE(dir, nullptr);
    fs::path file = fs::path(dir) / "note.sba";
    g_free(dir);

    SbaExporter exporter;
    ASSERT_TRUE(exporter.exportTo(&doc, file)) << exporter.getError();

    SbaImporter importer;
    auto back = importer.load(file);
    ASSERT_EQ(back->getPageCount(), 1u);
    EXPECT_EQ(elements(back->getPage(0)).size(), 1u);
    EXPECT_TRUE(back->getFilepath().empty());

    std::error_code ec;
    fs::remove_all(file.parent_path(), ec);
}

TEST(Sba, freeTextAndFilledShapesBecomePictures) {
    DocumentHandler handler;
    Document doc(&handler);
    auto page = std::make_shared<XojPage>(595.2755905511812, 833.0);
    page->setBackgroundType(PageType(PageTypeFormat::Plain));  // no line offset (see rulingShift())
    Layer* layer = page->getLayers().front();

    auto text = std::make_unique<Text>();
    text->getFont().setName("Sans");
    text->getFont().setSize(12);
    text->setText("hallo");
    text->setTransformation(xoj::util::Matrix::TRANSLATION(200, 100));
    const auto textBox = text->getBoundingBox();
    layer->addElement(std::move(text));

    auto filled = makeStroke(StrokeTool::HIGHLIGHTER, Color(0xff00ff00U), 2, false);
    filled->setFill(128);
    layer->addElement(std::move(filled));
    doc.addPage(page);

    SbaExporter exporter;
    std::string mainFile;
    std::vector<std::string> assetFiles;
    ASSERT_TRUE(exporter.build(&doc, {}, mainFile, assetFiles));
    EXPECT_EQ(assetFiles.size(), 2u);

    auto root = bs::parse(mainFile);
    const auto& pageJson = *(*bs::getArray(root, "z"))[0].document();
    EXPECT_EQ(bs::find(pageJson, "q"), nullptr);
    EXPECT_EQ(bs::find(pageJson, "s"), nullptr);
    const auto* images = bs::getArray(pageJson, "i");
    ASSERT_NE(images, nullptr);
    ASSERT_EQ(images->size(), 2u);
    // The text picture covers exactly the text box
    const auto& textImage = *(*images)[0].document();
    EXPECT_NEAR(bs::getNumber(textImage, "x", 0) * SCALE, textBox.x, 1e-6);
    EXPECT_NEAR(bs::getNumber(textImage, "y", 0) * SCALE, textBox.y, 1e-6);
    EXPECT_NEAR(bs::getNumber(textImage, "w", 0) * SCALE, textBox.width, 1e-6);
}
