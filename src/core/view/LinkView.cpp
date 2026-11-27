#include "LinkView.h"

#include <string>  // for string

#include <glib.h>  // for g_uri_split, g_uri_join

#include "model/Link.h"               // for Link
#include "util/Color.h"               // for cairo_set_source_rgbi
#include "util/Matrix.h"              // for Matrix
#include "util/raii/CairoWrappers.h"  // for CairoSaveGuard

using namespace xoj::view;

/**
 * Cairo only accepts ASCII link targets in PDFs (anything else fails the whole export with "invalid tag name,
 * attributes, or nesting"). Turn the URL into an ASCII URI: an internationalised host name becomes its punycode
 * (ASCII) form, other non-ASCII bytes are percent-encoded. Finally escape it for Cairo's quoted attribute syntax.
 */
static auto toCairoUriAttribute(const std::string& url) -> std::string {
    std::string uri = url;

    char* scheme = nullptr;
    char* userinfo = nullptr;
    char* host = nullptr;
    int port = -1;
    char* path = nullptr;
    char* query = nullptr;
    char* fragment = nullptr;
    // Only URLs with non-ASCII characters are rebuilt, so all others stay exactly as they are
    if (!g_str_is_ascii(url.c_str()) &&
        g_uri_split(url.c_str(), G_URI_FLAGS_ENCODED, &scheme, &userinfo, &host, &port, &path, &query, &fragment,
                    nullptr)) {
        if (host) {
            // g_uri_split already returns an internationalised host name in its ASCII (punycode) form
            char* joined = g_uri_join(G_URI_FLAGS_ENCODED, scheme, userinfo, host, port, path, query, fragment);
            uri = joined;
            g_free(joined);
        }
        g_free(scheme);
        g_free(userinfo);
        g_free(host);
        g_free(path);
        g_free(query);
        g_free(fragment);
    }

    std::string res;
    res.reserve(uri.size());
    for (unsigned char c: uri) {
        if (c >= 0x80) {
            constexpr char HEX[] = "0123456789ABCDEF";
            res += '%';
            res += HEX[c >> 4];
            res += HEX[c & 0x0f];
        } else {
            if (c == '\\' || c == '\'') {
                res += '\\';  // Cairo attribute strings escape these with a backslash
            }
            res += static_cast<char>(c);
        }
    }
    return res;
}

LinkView::LinkView(const Link* link): link(link) {}

auto LinkView::initPango(cairo_t* cr, const Link* l) -> xoj::util::GObjectSPtr<PangoLayout> {
    auto layout = l->createPangoLayout();
    pango_cairo_update_layout(cr, layout.get());
    pango_context_set_matrix(pango_layout_get_context(layout.get()), nullptr);

    return layout;
}

void LinkView::draw(const Context& ctx) const {
    xoj::util::CairoSaveGuard saveGuard(ctx.cr);

    cairo_set_operator(ctx.cr, CAIRO_OPERATOR_SOURCE);
    Util::cairo_set_source_rgbi(ctx.cr, link->getColor());

    cairo_tag_begin(ctx.cr, CAIRO_TAG_LINK, ("uri='" + toCairoUriAttribute(link->getUrl()) + "'").c_str());

    link->getTransformation().transformCairo(ctx.cr);

    auto layout = initPango(ctx.cr, link);
    const std::string& content = link->getText();
    pango_layout_set_text(layout.get(), content.c_str(), static_cast<int>(content.length()));

    pango_cairo_show_layout(ctx.cr, layout.get());

    cairo_tag_end(ctx.cr, CAIRO_TAG_LINK);
}
