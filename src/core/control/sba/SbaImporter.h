/*
 * Xournal++
 *
 * Converts a Saber note (.sba) into a document
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <map>     // for map
#include <memory>  // for unique_ptr
#include <string>  // for string
#include <vector>  // for vector

#include "model/DocumentHandler.h"  // for DocumentHandler
#include "model/PageRef.h"          // for PageRef

#include "Bson.h"        // for Document
#include "filesystem.h"  // for path

class Document;
class Layer;
class XojPdfDocument;

namespace xoj::sba {

class SbaImporter {
public:
    SbaImporter();
    ~SbaImporter();

    /**
     * Loads the .sba file. Throws std::runtime_error if the file cannot be read at all. Smaller problems (an unreadable
     * image, a feature without counterpart...) are collected in getWarnings().
     *
     * The returned document has no file path, so that saving asks for a new (.xopp) location.
     */
    std::unique_ptr<Document> load(const fs::path& filepath);

    /// Converts already extracted archive contents (exposed for the tests)
    std::unique_ptr<Document> loadFromContents(const std::string& mainFile, std::map<int, std::string> assets,
                                               const fs::path& filepath);

    const std::vector<std::string>& getWarnings() const { return warnings; }

private:
    void readPage(const bson::Document& page, int pageIndex);
    void readBackgroundImage(const bson::Document& image, const PageRef& page);
    void readStroke(const bson::Document& stroke, Layer* layer, double scale);
    void readImage(const bson::Document& image, Layer* layer, double scale);
    void readText(const bson::Array& ops, Layer* layer, double scale, double pageWidth);
    void setPatternBackground(const PageRef& page);

    /// Renders a page of a PDF asset as PNG (for PDFs that cannot be the document's background PDF)
    std::string rasterizePdfPage(int assetIndex, int pdfPage);

    const std::string* getAsset(int index);
    void warn(std::string msg);

    DocumentHandler dHandler;
    std::unique_ptr<Document> doc;
    std::vector<PageRef> pages;

    std::map<int, std::string> assets;
    fs::path pdfName;
    /// The asset used as the document's background PDF (Xournal++ documents have at most one)
    int backgroundPdfAsset = -1;
    std::map<int, std::unique_ptr<XojPdfDocument>> otherPdfs;

    std::string pattern;
    int lineHeight = 0;
    uint32_t backgroundColor = 0xffffffff;
    int fileVersion = 0;

    std::vector<std::string> warnings;
};

}  // namespace xoj::sba
