#include "Document.h"

#include <cstddef>
#include <ctime>  // for size_t, localtime, strf...
#include <memory>
#include <string>  // for string
#include <string_view>
#include <utility>  // for move, pair

#include <glib-object.h>  // for g_object_unref, G_TYPE_...

#include "model/DocumentChangeType.h"         // for DOCUMENT_CHANGE_CLEARED
#include "model/DocumentHandler.h"            // for DocumentHandler
#include "model/PageRef.h"                    // for PageRef
#include "model/PageType.h"                   // for PageType
#include "pdf/base/XojPdfAction.h"            // for XojPdfAction
#include "pdf/base/XojPdfBookmarkIterator.h"  // for XojPdfBookmarkIterator
#include "util/Assert.h"                      // for xoj_assert
#include "util/PathUtil.h"                    // for clearExtensions
#include "util/PlaceholderString.h"           // for PlaceholderString
#include "util/SaveNameUtils.h"               // for parseFilename
#include "util/Util.h"                        // for npos
#include "util/i18n.h"                        // for FS, _F
#include "util/raii/GObjectSPtr.h"            // for GObjectSPtr
#include "util/safe_casts.h"                  // for as_signed
#include "util/utf8_view.h"                   // for utf8

#include "LinkDestination.h"  // for XojLinkDest, DOCUMENT_L...
#include "XojPage.h"          // for XojPage
#include "filesystem.h"       // for path

Document::Document(DocumentHandler* handler): handler(handler) {}

Document::~Document() {
    clearDocument(true);
    freeTreeContentModel();
}

void Document::freeTreeContentModel() { this->contentsModel.reset(); }

void Document::lock() { this->documentLock.lock(); }
void Document::unlock() { this->documentLock.unlock(); }
auto Document::try_lock() -> bool { return this->documentLock.try_lock(); }
void Document::lock_shared() { this->documentLock.lock_shared(); }
void Document::unlock_shared() { this->documentLock.unlock_shared(); }
auto Document::try_lock_shared() -> bool { return this->documentLock.try_lock_shared(); }

void Document::clearDocument(bool destroy) {
    this->preview.reset();

    if (!destroy) {
        // release lock
        bool lastLock = try_lock();
        unlock();
        this->handler->fireDocumentChanged(DOCUMENT_CHANGE_CLEARED);
        if (!lastLock)  // document was locked before
        {
            lock();
        }
    }

    this->pages.clear();
    this->pageIndex.reset();
    freeTreeContentModel();

    this->filepath = fs::path{};
    this->pdfFilepath = fs::path{};
}

/**
 * Returns the pageCount, this call don't need to be synchronized (if it's not critical, you may get wrong data)
 */
auto Document::getPageCount() const -> size_t { return this->pages.size(); }

auto Document::getPdfPageCount() const -> size_t { return pdfDocument.getPageCount(); }

void Document::setFilepath(fs::path filepath) { this->filepath = std::move(filepath); }

auto Document::getFilepath() const -> fs::path { return filepath; }

auto Document::getPdfFilepath() const -> fs::path { return pdfFilepath; }

auto Document::createSaveFoldername(const fs::path& lastSavePath) const -> fs::path {
    if (!filepath.empty()) {
        return filepath.parent_path();
    }
    if (!pdfFilepath.empty()) {
        return pdfFilepath.parent_path();
    }


    return lastSavePath;
}

/**
 * Preprocesses a format string to standardize date/time format specifiers.
 *
 * This function is necessary because different systems or libraries may use
 * non-standard or shorthand format specifiers (e.g., %F, %T, %V) for date/time formatting.
 * By converting these to widely supported specifiers (e.g., %Y-%m-%d, %H-%M-%S, %U),
 * it ensures compatibility and consistency across platforms and libraries.
 *
 * @param formatStr The input format string to preprocess.
 * @return The processed format string with standardized specifiers.
 */
static std::u8string preprocessFormatString(std::u8string formatStr) {
    auto replace = [&formatStr](std::u8string_view pattern, std::u8string_view replacement) {
        for (size_t pos = formatStr.find(pattern); pos != std::string::npos;
             pos = formatStr.find(pattern, pos + replacement.length())) {
            formatStr.replace(pos, pattern.length(), replacement);
        }
    };

    replace(u8"%F", u8"%Y-%m-%d");
    replace(u8"%T", u8"%H-%M-%S");
    replace(u8"%V", u8"%U");

    return formatStr;
}

auto Document::createSaveFilename(DocumentType type, std::u8string_view defaultSaveName,
                                  std::u8string_view defaultPdfName) const -> fs::path {
    constexpr static std::string_view forbiddenChars = {"\\/:*?\"<>|"};
    std::u8string wildcardString;
    if (type != Document::PDF) {
        if (!filepath.empty()) {
            // This can be any extension
            fs::path p = filepath.filename();
            Util::clearExtensions(p);
            return p;
        }
        if (!pdfFilepath.empty()) {
            fs::path p = pdfFilepath.filename();
            Util::clearExtensions(p, ".pdf");
            return p;
        }
    } else if (!pdfFilepath.empty()) {
        wildcardString = SaveNameUtils::parseFilenameFromWildcardString(defaultPdfName, this->pdfFilepath.filename(),
                                                                        this->filepath.filename());
    } else if (!filepath.empty()) {
        wildcardString = SaveNameUtils::parseFilenameFromWildcardString(defaultPdfName, this->filepath.filename(),
                                                                        this->filepath.filename());
    }

    auto format_str = preprocessFormatString(wildcardString.empty() ? defaultSaveName.data() : wildcardString);

    // Expand the date/time placeholders (strftime syntax); GLib works in UTF-8 on all platforms
    std::unique_ptr<GDateTime, decltype(&g_date_time_unref)> now(g_date_time_new_now_local(), g_date_time_unref);
    std::unique_ptr<gchar, decltype(&g_free)> formatted(g_date_time_format(now.get(), char_cast(format_str).data()),
                                                         g_free);
    std::string filename = formatted ? formatted.get() : std::string(char_cast(format_str));
    // All forbidden characters are ASCII, so replacing bytes keeps the UTF-8 sequences intact
    for (auto& c: filename) {
        const auto u = static_cast<unsigned char>(c);
        if ((u < 32 && u > 0) || u == 127 || forbiddenChars.find(c) != std::string_view::npos) {
            c = '_';
        }
    }

    auto p = fs::path(xoj::util::utf8(filename));

    Util::clearExtensions(p);
    return p;
}

auto Document::getPreview() const -> xoj::util::CairoSurfaceSPtr { return this->preview; }

void Document::setPreview(xoj::util::CairoSurfaceSPtr preview) { this->preview = std::move(preview); }

auto Document::getEvMetadataFilename() const -> fs::path {
    if (!this->filepath.empty()) {
        return this->filepath;
    }
    if (!this->pdfFilepath.empty()) {
        return this->pdfFilepath;
    }
    return fs::path{};
}

auto Document::isAttachPdf() const -> bool { return this->attachPdf; }

auto Document::findPdfPage(size_t pdfPage) const -> size_t {
    // Create a page index if not already indexed.
    xoj_assert(this->pageIndex);
    auto pos = this->pageIndex->find(pdfPage);
    if (pos == this->pageIndex->end()) {
        return npos;
    } else {
        return pos->second;
    }
}

void Document::buildTreeContentsModel(GListStore* store, XojPdfBookmarkIterator* iter) {
    do {
        std::unique_ptr<XojPdfAction> action(iter->getAction());
        if (action->getTitle().empty()) {
            continue;
        }

        xoj::util::GObjectSPtr<XojLinkDest> link(link_dest_new(), xoj::util::adopt);
        link->dest = new LinkDestination(*action->getDestination());
        link->dest->setExpand(iter->isOpen());

        std::unique_ptr<XojPdfBookmarkIterator> child(iter->getChildIter());
        if (child) {
            GListStore* children = g_list_store_new(TYPE_LINK_DEST);
            buildTreeContentsModel(children, child.get());
            if (g_list_model_get_n_items(G_LIST_MODEL(children)) > 0) {
                link->children = children;
            } else {
                g_object_unref(children);
            }
        }

        g_list_store_append(store, link.get());
    } while (iter->next());
}

void Document::indexPdfPages() {
    auto index = std::make_unique<PageIndex>();
    for (size_t i = 0; i < this->pages.size(); ++i) {
        const auto& p = this->pages[i];
        if (p->getBackgroundType().isPdfPage()) {
            index->emplace(p->getPdfPageNr(), i);
        }
    }
    this->pageIndex.swap(index);
}


void Document::buildContentsModel() {
    freeTreeContentModel();

    std::unique_ptr<XojPdfBookmarkIterator> iter(pdfDocument.getContentsIter());
    if (iter == nullptr) {
        // No Bookmarks
        return;
    }

    this->contentsModel.reset(g_list_store_new(TYPE_LINK_DEST), xoj::util::adopt);
    buildTreeContentsModel(this->contentsModel.get(), iter.get());
}

auto Document::getContentsModel() const -> GListModel* { return G_LIST_MODEL(this->contentsModel.get()); }

void Document::fillPageLabels(GListModel* model) {
    guint n = g_list_model_get_n_items(model);
    for (guint i = 0; i < n; i++) {
        xoj::util::GObjectSPtr<XojLinkDest> link(LINK_DEST(g_list_model_get_item(model, i)), xoj::util::adopt);

        auto page = findPdfPage(link->dest->getPdfPage());
        if (page != npos) {
            link_dest_set_page_label(link.get(), std::to_string(page + 1).c_str());
        } else {
            link_dest_set_page_label(link.get(), "");
        }

        if (link->children) {
            fillPageLabels(G_LIST_MODEL(link->children));
        }
    }
}

void Document::updateIndexPageNumbers() {
    indexPdfPages();
    if (this->contentsModel) {
        fillPageLabels(G_LIST_MODEL(this->contentsModel.get()));
    }
}

void Document::setPdfAttributes(const fs::path& filename, bool attachToDocument) {
    this->pdfFilepath = filename;
    this->attachPdf = attachToDocument;
}

auto Document::readPdf(const fs::path& filename, bool initPages, bool attachToDocument,
                       std::unique_ptr<std::string> data) -> bool {
    GError* popplerError = nullptr;

    lock();

    if (data != nullptr) {
        if (!pdfDocument.load(std::move(data), password, &popplerError)) {
            lastError = FS(_F("Document not loaded! ({1}), {2}") % filename.u8string() % popplerError->message);
            g_error_free(popplerError);
            unlock();

            return false;
        }
    } else {
        if (!pdfDocument.load(filename, password, &popplerError)) {
            if (popplerError) {
                lastError = FS(_F("Document not loaded! ({1}), {2}") % filename.u8string() % popplerError->message);
                g_error_free(popplerError);
            } else {
                lastError = FS(_F("Document not loaded! ({1}), {2}") % filename.u8string() % "");
            }
            unlock();
            return false;
        }
    }

    this->pdfFilepath = filename;
    this->attachPdf = attachToDocument;
    lastError = "";

    if (initPages) {
        this->pages.clear();
    }

    if (initPages) {
        for (size_t i = 0; i < pdfDocument.getPageCount(); i++) {
            XojPdfPageSPtr page = pdfDocument.getPage(i);
            auto p = std::make_shared<XojPage>(page->getWidth(), page->getHeight());
            p->setBackgroundPdfPageNr(i);
            this->pages.emplace_back(std::move(p));
        }
    }

    indexPdfPages();
    buildContentsModel();
    updateIndexPageNumbers();

    unlock();

    this->handler->fireDocumentChanged(DOCUMENT_CHANGE_PDF_BOOKMARKS);

    return true;
}

void Document::resetPdf() { pdfDocument.reset(); }

void Document::setPageSize(PageRef p, double width, double height) { p->setSize(width, height); }

auto Document::getPageWidth(PageRef p) -> double { return p->getWidth(); }

auto Document::getPageHeight(PageRef p) -> double { return p->getHeight(); }

/**
 * @return The last error message to show to the user
 */
auto Document::getLastErrorMsg() const -> std::string { return lastError; }

void Document::deletePage(size_t pNr) {
    auto it = this->pages.begin() + as_signed(pNr);
    this->pages.erase(it);

    // Reset the page index
    this->pageIndex.reset();
    updateIndexPageNumbers();
}

void Document::insertPage(const PageRef& p, size_t position) {
    this->pages.insert(this->pages.begin() + as_signed(position), p);

    // Reset the page index
    this->pageIndex.reset();
    updateIndexPageNumbers();
}

void Document::addPage(const PageRef& p) {
    this->pages.push_back(p);

    // Reset the page index
    this->pageIndex.reset();
    updateIndexPageNumbers();
}

auto Document::indexOf(const PageRef& page) -> size_t {
    for (size_t i = 0; i < this->pages.size(); i++) {
        PageRef pg = this->pages[i];
        if (pg == page) {
            return i;
        }
    }

    return npos;
}

auto Document::getPage(size_t page) const -> PageRef {
    if (getPageCount() <= page) {
        return nullptr;
    }
    if (page == npos) {
        return nullptr;
    }

    return this->pages[page];
}

auto Document::getPdfPage(size_t page) const -> XojPdfPageSPtr { return this->pdfDocument.getPage(page); }

auto Document::getPdfDocument() const -> const XojPdfDocument& { return this->pdfDocument; }

auto Document::operator=(const Document& doc) -> Document& {
    clearDocument();

    // Copy PDF Document
    this->pdfDocument = doc.pdfDocument;

    this->password = doc.password;
    this->createBackupOnSave = doc.createBackupOnSave;
    this->pdfFilepath = doc.pdfFilepath;
    this->filepath = doc.filepath;
    this->pages = doc.pages;
    this->attachPdf = doc.attachPdf;
    this->pathStorageMode = doc.pathStorageMode;

    indexPdfPages();
    buildContentsModel();
    updateIndexPageNumbers();

    bool lastLock = try_lock();
    xoj_assert(!lastLock);
    unlock();
    this->handler->fireDocumentChanged(DOCUMENT_CHANGE_COMPLETE);
    if (!lastLock)  // document was locked before
    {
        lock();
    }
    return *this;
}

void Document::setCreateBackupOnSave(bool backup) { this->createBackupOnSave = backup; }

auto Document::shouldCreateBackupOnSave() const -> bool { return this->createBackupOnSave; }
