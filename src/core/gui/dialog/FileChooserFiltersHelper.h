/*
 * Xournal++
 *
 * Helper functions to build the file filters of GtkFileDialogs
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>

#include <gtk/gtk.h>

#include "util/i18n.h"

namespace xoj {
struct FileType {
    const char* extension;
    const char* mimetype;
    const char* description;
};

/// The filetypes we may save or export to, in SaveFileDialogs
namespace FileTypes {
// Use N_ for the descriptions so it is send to translators (but still constexpr)
constexpr FileType PDF = {"pdf", "application/pdf", N_("PDF files")};
constexpr FileType XOJ = {"xoj", "application/x-xojpp", N_("Xournal files")};
constexpr FileType XOPP = {"xopp", "application/x-xopp", N_("Xournal++ files")};
constexpr FileType XOPT = {"xopt", "application/x-xopt", N_("Xournal++ template")};
constexpr FileType SVG = {"svg", "image/svg+xml", N_("SVG graphics")};
constexpr FileType PNG = {"png", "image/png", N_("PNG graphics")};
constexpr FileType ZIP = {"zip", "application/zip", N_("ZIP archive")};
// Saber notes have no registered MIME type: they are matched by their extension
constexpr FileType SBA = {"sba", nullptr, N_("Saber notes")};
};  // namespace FileTypes
/// The filter-adding functions append GtkFileFilter's to `filters` (a GListStore of GtkFileFilter, as GtkFileDialog
/// takes)
GListStore* newFilterList();  ///< Returns a new, empty GListStore for GtkFileFilter's (full reference)

void addFilterForFile(GListStore* filters, const FileType& f);

inline void addFilterPdf(GListStore* filters) { addFilterForFile(filters, FileTypes::PDF); }
inline void addFilterXoj(GListStore* filters) { addFilterForFile(filters, FileTypes::XOJ); }
inline void addFilterXopp(GListStore* filters) { addFilterForFile(filters, FileTypes::XOPP); }
inline void addFilterXopt(GListStore* filters) { addFilterForFile(filters, FileTypes::XOPT); }
inline void addFilterSvg(GListStore* filters) { addFilterForFile(filters, FileTypes::SVG); }
inline void addFilterPng(GListStore* filters) { addFilterForFile(filters, FileTypes::PNG); }
inline void addFilterSba(GListStore* filters) { addFilterForFile(filters, FileTypes::SBA); }

void addFilterAllFiles(GListStore* filters);
void addFilterSupported(GListStore* filters);
void addFilterImages(GListStore* filters);  ///< All images supported by GdkPixbuf
void addFilterTex(GListStore* filters);
};  // namespace xoj
