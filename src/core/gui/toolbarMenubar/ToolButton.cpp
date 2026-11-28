#include "ToolButton.h"

#include <utility>  // for move

#include <glib.h>
#include <gtk/gtk.h>

#include "gui/toolbarMenubar/AbstractToolItem.h"  // for AbstractToolItem
#include "util/GVariantTemplate.h"
#include "util/raii/GObjectSPtr.h"  // for WidgetSPtr


ToolButton::ToolButton(std::string id, Category cat, Action action, std::string iconName, std::string description,
                       bool toggle):
        ItemWithNamedIcon(std::move(id), cat),
        iconName(std::move(iconName)),
        description(std::move(description)),
        action(action),
        toggle(toggle) {}

ToolButton::ToolButton(std::string id, Category cat, Action action, GVariant* target, std::string iconName,
                       std::string description):
        ItemWithNamedIcon(std::move(id), cat),
        iconName(std::move(iconName)),
        description(std::move(description)),
        action(action),
        target(target, xoj::util::refsink),
        toggle(true) {}

auto ToolButton::createItem(ToolbarSide side) -> Widgetry {
    GtkWidget* btn = toggle ? gtk_toggle_button_new() : gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(btn), getNewToolIcon());
    gtk_widget_set_tooltip_text(btn, getToolDisplayName().c_str());
    gtk_actionable_set_action_name(GTK_ACTIONABLE(btn), (std::string("win.") + Action_toString(action)).c_str());
    if (target) {
        gtk_actionable_set_action_target_value(GTK_ACTIONABLE(btn), target.get());
    }

    xoj::util::WidgetSPtr item;
    if (popoverFactory) {
        GtkMenuButton* menubutton = GTK_MENU_BUTTON(gtk_menu_button_new());
        gtk_menu_button_set_popover(menubutton, popoverFactory->createPopover());
        setMenuButtonDirection(menubutton, side);
        gtk_menu_button_set_always_show_arrow(menubutton, true);

        // The arrow next to the icon, also in vertical toolbars: they are wide enough (the color button sets their
        // width), and below the icon it doubles the height of the item
        GtkBox* box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
        gtk_widget_add_css_class(GTK_WIDGET(box), "with-arrow");  // see ToolbarBox::alignArrowColumns()
        gtk_box_append(box, btn);
        gtk_box_append(box, GTK_WIDGET(menubutton));

        item.reset(GTK_WIDGET(box), xoj::util::adopt);
    } else if (to_Orientation(side) == GTK_ORIENTATION_VERTICAL) {
        // In a vertical toolbar, an invisible stand-in of the arrow's width can keep the icon in the same column as the
        // icons with an arrow next to them. It is shown only in groups that have such icons (see
        // ToolbarBox::alignArrowColumns()), otherwise the icon stays centred.
        GtkWidget* standIn = gtk_menu_button_new();
        gtk_menu_button_set_always_show_arrow(GTK_MENU_BUTTON(standIn), true);
        setMenuButtonDirection(GTK_MENU_BUTTON(standIn), side);
        gtk_widget_set_opacity(standIn, 0);
        gtk_widget_set_can_target(standIn, false);
        gtk_widget_set_focusable(standIn, false);
        gtk_accessible_update_state(GTK_ACCESSIBLE(standIn), GTK_ACCESSIBLE_STATE_HIDDEN, true, -1);
        gtk_widget_set_visible(standIn, false);
        gtk_widget_add_css_class(standIn, "arrow-stand-in");

        GtkBox* box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
        gtk_widget_set_halign(GTK_WIDGET(box), GTK_ALIGN_CENTER);
        gtk_box_append(box, btn);
        gtk_box_append(box, standIn);
        item.reset(GTK_WIDGET(box), xoj::util::adopt);
    } else {
        item.reset(btn, xoj::util::adopt);
    }

    /// Makes a proxy item for the toolbar's overflow menu
    auto createProxy = [this]() {
        GtkWidget* proxy;
        auto* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_append(GTK_BOX(box), getNewToolIcon());
        gtk_box_append(GTK_BOX(box), gtk_label_new(getToolDisplayName().c_str()));

        if (toggle) {
            proxy = gtk_check_button_new();
#if GTK_CHECK_VERSION(4, 8, 0)
            gtk_check_button_set_child(GTK_CHECK_BUTTON(proxy), box);
#else
            gtk_check_button_set_label(GTK_CHECK_BUTTON(proxy), getToolDisplayName().c_str());
            g_object_unref(g_object_ref_sink(G_OBJECT(box)));
#endif
            // if (target) {
            //     gtk_check_menu_item_set_draw_as_radio(GTK_CHECK_MENU_ITEM(proxy), true);
            // }
        } else {
            proxy = gtk_button_new();
            gtk_button_set_child(GTK_BUTTON(proxy), box);
        }

        gtk_actionable_set_action_name(GTK_ACTIONABLE(proxy), (std::string("win.") + Action_toString(action)).c_str());
        if (target) {
            gtk_actionable_set_action_target_value(GTK_ACTIONABLE(proxy), target.get());
        }
        return proxy;
    };
    return {std::move(item), xoj::util::WidgetSPtr(createProxy(), xoj::util::adopt)};
}

auto ToolButton::getToolDisplayName() const -> std::string { return description; }

auto ToolButton::getIconName() const -> const char* { return iconName.c_str(); }

void ToolButton::setPopoverFactory(const PopoverFactory* factory) { popoverFactory = factory; }
