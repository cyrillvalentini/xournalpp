/*
 * Xournal++
 *
 * A link destination in a PDF Document
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>   // for size_t
#include <optional>  // for optional
#include <string>    // for string
#include <variant>   // for variant

#include <gio/gio.h>      // for GListStore
#include <glib-object.h>  // for G_TYPE_CHECK_INSTANCE_CAST, GObject, GType
#include <glib.h>         // for G_GNUC_CONST

struct _LinkDest;


typedef struct _LinkDest XojLinkDest;
typedef struct _LinkDestClass XojLinkDestClass;

class LinkDestination {
public:
    LinkDestination();
    virtual ~LinkDestination();

    /// A link to a URI.
    struct LinkType {
        std::string uri;
    };
    /// A link to some unknown PDF action.
    struct UnknownType {};

    /// Represents a possible PDF link action.
    using Type = std::variant<UnknownType, LinkType>;

public:
    size_t getPdfPage() const;
    void setPdfPage(size_t page);

    void setExpand(bool expand);
    bool getExpand() const;

    bool shouldChangeLeft() const;
    bool shouldChangeTop() const;

    double getZoom() const;
    [[maybe_unused]] double getLeft() const;
    double getTop() const;

    void setChangeLeft(double left);
    void setChangeZoom(double zoom);
    void setChangeTop(double top);

    void setName(std::string name);
    const std::string& getName() const;

    /// \return Whether this link refers to a URI.
    bool isURI() const;

    std::optional<std::string> getURI() const;

    /// \return Changes this link to refer to the given URI.
    void setURI(std::string uri);

private:
    size_t page;
    bool expand;

    double left;
    double top;
    double zoom;

    bool changeLeft;
    bool changeZoom;
    bool changeTop;

    std::string name;
    Type contents;
};

/**
 * An entry of the PDF outline (table of contents).
 *
 * The outline is a GListStore of XojLinkDest; entries with sub-entries hold them in `children`.
 */
struct _LinkDest {
    GObject base_instance;
    LinkDestination* dest;
    /// Sub-entries (XojLinkDest), or nullptr if there are none. Owned.
    GListStore* children;
    /// Page number in the document as shown to the user ("" if the page is not in the document). Owned.
    /// Exposed as the GObject property "page-label"; change it with link_dest_set_page_label.
    gchar* pageLabel;
};

#define TYPE_LINK_DEST (link_dest_get_type())
#define LINK_DEST(object) (G_TYPE_CHECK_INSTANCE_CAST((object), TYPE_LINK_DEST, XojLinkDest))
#define LINK_DEST_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), TYPE_LINK_DEST, XojLinkDestClass))
#define IS_LINK_DEST(object) (G_TYPE_CHECK_INSTANCE_TYPE((object), TYPE_LINK_DEST))
#define IS_LINK_DEST_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), TYPE_LINK_DEST))
#define LINK_DEST_GET_CLASS(object) (G_TYPE_INSTANCE_GET_CLASS((object), TYPE_LINK_DEST, XojLinkDestClass))

GType link_dest_get_type(void);
XojLinkDest* link_dest_new();
void link_dest_set_page_label(XojLinkDest* link, const gchar* label);
