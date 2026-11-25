#include "ColorToolItem.h"

#include <utility>  // for move

#include "enums/Action.enum.h"                  // for Action
#include "gui/toolbarMenubar/icon/ColorIcon.h"  // for ColorIcon
#include "util/GtkUtil.h"                       // for setToggleButtonUnreleasable
#include "util/gtk4_helper.h"                   // for gtk_button_set_child
#include "util/raii/GObjectSPtr.h"               // for GObjectSPtr

ColorToolItem::ColorToolItem(NamedColor namedColor, const std::optional<Recolor>& recolor):
        AbstractToolItem(std::string("COLOR(") + std::to_string(namedColor.getIndex()) + ")", Category::COLORS),
        namedColor(std::move(namedColor)),
        target(xoj::util::makeGVariantSPtr(this->namedColor.getColor())) {
    if (recolor) {
        secondaryColor = std::make_optional(recolor->convertColor(namedColor.getColor()));
    } else {
        secondaryColor = std::nullopt;
    }
}

ColorToolItem::~ColorToolItem() = default;

auto ColorToolItem::getColor() const -> Color { return this->namedColor.getColor(); }

auto ColorToolItem::createItem(bool) -> xoj::util::WidgetSPtr {
    auto* btn = gtk_toggle_button_new();
    gtk_widget_set_can_focus(btn, false);  // todo(gtk4) not necessary anymore
    auto actionName = std::string("win.") + Action_toString(Action::TOOL_COLOR);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(btn), actionName.data());
    gtk_actionable_set_action_target_value(GTK_ACTIONABLE(btn), target.get());
    xoj::util::gtk::setToggleButtonUnreleasable(GTK_TOGGLE_BUTTON(btn));

    gtk_widget_set_tooltip_text(btn, this->namedColor.getName().c_str());
    gtk_button_set_child(GTK_BUTTON(btn), getNewToolIcon());

    // Lets a stylesheet draw the selection as a ring in the swatch's own colour (like GNOME's accent colour picker):
    // the button's "color" is the swatch colour. Very dark and very light swatches are tagged, as a ring in their
    // colour could be invisible against the toolbar.
    {
        GtkStyleContext* ctx = gtk_widget_get_style_context(btn);
        gtk_style_context_add_class(ctx, "color-swatch");
        // Keep the button square (the toolbar would stretch it to its full height), so the ring stays a circle
        gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
        gtk_widget_set_halign(btn, GTK_ALIGN_CENTER);
        const Color c = this->namedColor.getColor();
        const double luminance = (0.2126 * c.red + 0.7152 * c.green + 0.0722 * c.blue) / 255.0;
        if (luminance < 0.05) {
            gtk_style_context_add_class(ctx, "dark-swatch");
        } else if (luminance > 0.95) {
            gtk_style_context_add_class(ctx, "light-swatch");
        }
        char css[64];
        g_snprintf(css, sizeof(css), "button { color: #%02x%02x%02x; }", c.red, c.green, c.blue);
        xoj::util::GObjectSPtr<GtkCssProvider> provider(gtk_css_provider_new(), xoj::util::adopt);
        gtk_css_provider_load_from_data(provider.get(), css, -1, nullptr);
        gtk_style_context_add_provider(ctx, GTK_STYLE_PROVIDER(provider.get()),
                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 3);
    }


    // For the sake of deprecated GtkToolbar, wrap the button in a GtkToolItem
    // Todo(gtk4): remove
    GtkToolItem* it = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(it), btn);
    /// Makes a proxy item for the toolbar's overflow menu
    auto createProxy = [this]() {
        GtkWidget* proxy = gtk_check_menu_item_new();
        gtk_check_menu_item_set_draw_as_radio(GTK_CHECK_MENU_ITEM(proxy), true);

        auto* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_container_add(GTK_CONTAINER(proxy), box);
        gtk_box_append(GTK_BOX(box), getNewToolIcon());
        gtk_box_append(GTK_BOX(box), gtk_label_new(getToolDisplayName().c_str()));

        gtk_actionable_set_action_name(GTK_ACTIONABLE(proxy),
                                       (std::string("win.") + Action_toString(Action::TOOL_COLOR)).c_str());
        if (target) {
            gtk_actionable_set_action_target_value(GTK_ACTIONABLE(proxy), target.get());
        }
        xoj::util::gtk::fixActionableInitialSensitivity(GTK_ACTIONABLE(proxy));
        return proxy;
    };
    gtk_tool_item_set_proxy_menu_item(it, "", createProxy());
    return xoj::util::WidgetSPtr(GTK_WIDGET(it), xoj::util::adopt);
}

auto ColorToolItem::getToolDisplayName() const -> std::string { return this->namedColor.getName(); }

auto ColorToolItem::getNewToolIcon() const -> GtkWidget* {
    return ColorIcon::newGtkImage(this->namedColor.getColor(), 22, true, this->secondaryColor);
}

void ColorToolItem::updateColor(const Palette& palette) { namedColor = palette.getColorAt(namedColor.getIndex()); }

void ColorToolItem::updateSecondaryColor(const std::optional<Recolor>& recolor) {
    if (recolor) {
        secondaryColor = std::make_optional(recolor->convertColor(namedColor.getColor()));
    } else {
        secondaryColor = std::nullopt;
    }
}
