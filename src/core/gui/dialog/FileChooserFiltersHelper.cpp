#include "FileChooserFiltersHelper.h"

#include "util/i18n.h"

namespace xoj {
GListStore* newFilterList() { return g_list_store_new(GTK_TYPE_FILE_FILTER); }

void addFilterAllFiles(GListStore* filters) {
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("All files"));
    gtk_file_filter_add_pattern(filter, "*");
    g_list_store_append(filters, filter);
    g_object_unref(filter);
}

void addFilterSupported(GListStore* filters) {
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("Supported files"));
#ifndef _WIN32
    gtk_file_filter_add_mime_type(filter, FileTypes::XOJ.mimetype);
    gtk_file_filter_add_mime_type(filter, FileTypes::XOPP.mimetype);
    gtk_file_filter_add_mime_type(filter, FileTypes::XOPT.mimetype);
    gtk_file_filter_add_mime_type(filter, FileTypes::PDF.mimetype);
#else
    gtk_file_filter_add_pattern(filter, (std::string("*.") + FileTypes::XOJ.extension).c_str());
    gtk_file_filter_add_pattern(filter, (std::string("*.") + FileTypes::XOPP.extension).c_str());
    gtk_file_filter_add_pattern(filter, (std::string("*.") + FileTypes::XOPT.extension).c_str());
    gtk_file_filter_add_pattern(filter, (std::string("*.") + FileTypes::PDF.extension).c_str());
#endif
    gtk_file_filter_add_pattern(filter, "*.moj");  // MrWriter
    gtk_file_filter_add_pattern(filter, "*.sba");  // Saber
    gtk_file_filter_add_pattern(filter, "*.SBA");
    g_list_store_append(filters, filter);
    g_object_unref(filter);
}

void addFilterForFile(GListStore* filters, const FileType& f) {
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, fetch_translation(f.description));
#ifndef _WIN32
    if (f.mimetype) {
        gtk_file_filter_add_mime_type(filter, f.mimetype);
    } else {
        gtk_file_filter_add_pattern(filter, (std::string("*.") + f.extension).c_str());
    }
#else
    gtk_file_filter_add_pattern(filter, (std::string("*.") + f.extension).c_str());
#endif
    g_list_store_append(filters, filter);
    g_object_unref(filter);
}

void addFilterImages(GListStore* filters) {
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("Image files"));
    gtk_file_filter_add_pixbuf_formats(filter);
    g_list_store_append(filters, filter);
    g_object_unref(filter);
}

void addFilterTex(GListStore* filters) {
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("Latex files"));
#ifndef _WIN32
    gtk_file_filter_add_mime_type(filter, "application/x-latex");
    gtk_file_filter_add_mime_type(filter, "text/x-tex");
#else
    gtk_file_filter_add_pattern(filter, "*.tex");
#endif
    g_list_store_append(filters, filter);
    g_object_unref(filter);
}
};  // namespace xoj
