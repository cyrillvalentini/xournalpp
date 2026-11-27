#include "LinkDestination.h"

#include <utility>  // for move

#include "util/Util.h"  // for npos

struct _LinkDestClass {
    GObjectClass base_class;
};

G_DEFINE_TYPE(XojLinkDest, link_dest, G_TYPE_OBJECT)  // @suppress("Unused static function")

enum { PROP_0, PROP_PAGE_LABEL, N_PROPS };
static GParamSpec* linkDestProps[N_PROPS] = {};

static void link_dest_init(XojLinkDest* link) {
    link->dest = nullptr;
    link->children = nullptr;
    link->pageLabel = g_strdup("");
}

static void link_dest_finalize(GObject* object) {
    auto* link = LINK_DEST(object);
    delete link->dest;
    link->dest = nullptr;
    g_clear_object(&link->children);
    g_clear_pointer(&link->pageLabel, g_free);

    G_OBJECT_CLASS(link_dest_parent_class)->finalize(object);
}

static void link_dest_get_property(GObject* object, guint propId, GValue* value, GParamSpec* pspec) {
    switch (propId) {
        case PROP_PAGE_LABEL:
            g_value_set_string(value, LINK_DEST(object)->pageLabel);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propId, pspec);
    }
}

static void link_dest_class_init(XojLinkDestClass* linkClass) {
    GObjectClass* objectClass = G_OBJECT_CLASS(linkClass);
    objectClass->finalize = link_dest_finalize;
    objectClass->get_property = link_dest_get_property;

    linkDestProps[PROP_PAGE_LABEL] = g_param_spec_string(
            "page-label", nullptr, nullptr, "", static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_properties(objectClass, N_PROPS, linkDestProps);
}

auto link_dest_new() -> XojLinkDest* { return LINK_DEST(g_object_new(TYPE_LINK_DEST, nullptr)); }

void link_dest_set_page_label(XojLinkDest* link, const gchar* label) {
    if (label == nullptr) {
        label = "";
    }
    if (g_strcmp0(link->pageLabel, label) == 0) {
        return;
    }
    g_free(link->pageLabel);
    link->pageLabel = g_strdup(label);
    g_object_notify_by_pspec(G_OBJECT(link), linkDestProps[PROP_PAGE_LABEL]);
}

LinkDestination::LinkDestination():
        page(npos),
        expand(false),
        left(0),
        top(0),
        zoom(0),
        changeLeft(false),
        changeZoom(false),
        changeTop(false),
        name(""),
        contents(UnknownType{}) {}

LinkDestination::~LinkDestination() = default;

auto LinkDestination::getPdfPage() const -> size_t { return this->page; }

void LinkDestination::setPdfPage(size_t page) { this->page = page; }

void LinkDestination::setExpand(bool expand) { this->expand = expand; }

auto LinkDestination::getExpand() const -> bool { return this->expand; }

auto LinkDestination::shouldChangeLeft() const -> bool { return changeLeft; }

auto LinkDestination::shouldChangeTop() const -> bool { return changeTop; }

auto LinkDestination::getZoom() const -> double { return zoom; }

auto LinkDestination::getLeft() const -> double { return left; }

auto LinkDestination::getTop() const -> double { return top; }

void LinkDestination::setChangeLeft(double left) {
    this->left = left;
    this->changeLeft = true;
}

void LinkDestination::setChangeZoom(double zoom) {
    this->zoom = zoom;
    this->changeZoom = true;
}

void LinkDestination::setChangeTop(double top) {
    this->top = top;
    this->changeTop = true;
}

void LinkDestination::setName(std::string name) { this->name = std::move(name); }
auto LinkDestination::getName() const -> const std::string& { return name; }

void LinkDestination::setURI(std::string uri) { this->contents = LinkType{std::move(uri)}; }

auto LinkDestination::isURI() const -> bool { return std::holds_alternative<LinkDestination::LinkType>(contents); }

auto LinkDestination::getURI() const -> std::optional<std::string> {
    if (auto* t = std::get_if<LinkDestination::LinkType>(&contents)) {
        return t->uri;
    } else {
        return std::nullopt;
    }
}
