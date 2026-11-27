/*
 * Xournal++
 *
 * Exports a document as a Saber note (.sba)
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <map>       // for map
#include <optional>  // for optional
#include <string>  // for string
#include <vector>  // for vector

#include "model/PageRef.h"       // for ConstPageRef
#include "util/Color.h"         // for Color
#include "util/ElementRange.h"  // for PageRangeVector

#include "Bson.h"        // for Document
#include "filesystem.h"  // for path

class Document;
class Element;
class Stroke;
class Text;
class Image;

namespace xoj::sba {

class SbaExporter {
public:
    /**
     * Writes the pages in `range` (all pages if empty) of `doc` to `out`. The caller must hold a (shared) lock on the
     * document. Returns false and sets getError() on failure.
     */
    bool exportTo(const Document* doc, const fs::path& out, const PageRangeVector& range = {});

    /**
     * Builds the contents of the archive: the note (BSON) and its assets. Exposed for the tests.
     */
    bool build(const Document* doc, const PageRangeVector& range, std::string& mainFile,
               std::vector<std::string>& assetFiles);

    const std::string& getError() const { return error; }
    const std::vector<std::string>& getWarnings() const { return warnings; }

private:
    bson::Document buildPage(const Document* doc, const ConstPageRef& page, int pageIndex);
    bson::Value strokeToJson(const Stroke* s, int pageIndex, Color pageColor);
    std::optional<bson::Value> imageToJson(const Image* img, int pageIndex);
    std::optional<bson::Value> rasterizedElementToJson(const Element* e, int pageIndex);
    std::optional<bson::Document> backgroundToJson(const Document* doc, const ConstPageRef& page, int pageIndex);
    bson::Array textToQuill(std::vector<const Text*> texts);

    int addAsset(std::string data);
    bson::Document imageJson(int pageIndex, const std::string& ext, double x, double y, double w, double h, double nw,
                             double nh, int asset);
    void choosePattern(const Document* doc, const std::vector<size_t>& pageNumbers);
    void warn(std::string msg);

    std::vector<std::string> assets;
    int pdfAsset = -1;
    std::map<const void*, int> backgroundImageAssets;
    int nextImageId = 0;
    /// Vertical offset of the current page's content, see rulingShift()
    double shiftY = 0;

    std::string pattern;
    int lineHeight = 40;
    std::optional<uint32_t> backgroundColor;

    std::string error;
    std::vector<std::string> warnings;
};

}  // namespace xoj::sba
