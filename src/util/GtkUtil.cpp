#include "util/GtkUtil.h"

#include <algorithm>

#include <gtk/gtk.h>

#include "util/Assert.h"
#include "util/Point.h"

namespace xoj::util::gtk {
void setWidgetFollowActionEnabled(GtkWidget* w, GAction* a) {
    g_signal_connect_object(a, "notify::enabled", G_CALLBACK(+[](GObject* a, GParamSpec*, gpointer w) {
                                bool b = g_action_get_enabled(G_ACTION(a));
                                gtk_widget_set_sensitive(GTK_WIDGET(w), b);
                            }),
                            w, GConnectFlags(0));
    gtk_widget_set_sensitive(w, g_action_get_enabled(a));
}

std::optional<double> getWidgetDPI(GtkWidget* w) {
    GtkNative* nat = gtk_widget_get_native(w);
    GdkDisplay* disp = gtk_widget_get_display(w);
    if (nat && disp) {
        GdkSurface* surf = gtk_native_get_surface(nat);
        GdkMonitor* mon = surf ? gdk_display_get_monitor_at_surface(disp, surf) : nullptr;
        if (mon) {
            int h = gdk_monitor_get_height_mm(mon);
            GdkRectangle r;
            gdk_monitor_get_geometry(mon, &r);
            // Some setups (e.g. RDP sessions or missing EDID data on Windows) report a null physical size, which
            // would produce an infinite or NaN DPI and break e.g. pasted image scaling
            if (h <= 0 || r.height <= 0) {
                g_debug("Automatic DPI conf: invalid monitor data - height %d mm - pixel height %d", h, r.height);
                return std::nullopt;
            }
            auto res = static_cast<double>(r.height) * 25.4 / static_cast<double>(h);
            g_debug("Automatic DPI conf: monitor height %d mm - DPI = %f", h, res);
            // I assume some monitors may not return the right value: clamp to avoid wild values that could cause very
            // bad performances. Not sure what good clamping values should be here.
            // DPI > 720 should use HiDPI scaling but smaller devices (tablet/phone) can have high DPI values
            res = std::clamp(res, 36., 720.);
            return res;
        }
    }
    return std::nullopt;
}

xoj::util::Point<double> gdkSurfaceToWidgetCoordinates(xoj::util::Point<double> p, GtkWidget* w) {
    xoj::util::Point<double> q;
    GtkWidget* window = gtk_widget_get_ancestor(w, GTK_TYPE_WINDOW);
    gtk_native_get_surface_transform(GTK_NATIVE(window), &q.x, &q.y);
    p -= q;

    auto zero = GRAPHENE_POINT_INIT_ZERO;
    auto r = GRAPHENE_POINT_INIT_ZERO;
    if (!gtk_widget_compute_point(window, w, &zero, &r)) {
        xoj_assert(false);  // Widget and its GtkWindow ancestor are related, so this should always succeed
    }
    p += {r.x, r.y};
    return p;
}
namespace {
/// Depth-first search for the first descendant of the given type name (the colour chooser's classes aren't public)
GtkWidget* findDescendant(GtkWidget* w, const char* typeName) {
    for (GtkWidget* c = gtk_widget_get_first_child(w); c; c = gtk_widget_get_next_sibling(c)) {
        if (g_str_equal(G_OBJECT_TYPE_NAME(c), typeName)) {
            return c;
        }
        if (GtkWidget* found = findDescendant(c, typeName)) {
            return found;
        }
    }
    return nullptr;
}

/// Grows the window so the content of its first GtkScrolledWindow fits without scrolling
gboolean fitWindowToScrolledContent(gpointer data) {
    auto* win = GTK_WINDOW(data);
    GtkWidget* scrolled = findDescendant(GTK_WIDGET(win), "GtkScrolledWindow");
    GtkWidget* content = scrolled ? gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(scrolled)) : nullptr;
    if (content) {
        int natW = 0;
        int natH = 0;
        gtk_widget_measure(content, GTK_ORIENTATION_HORIZONTAL, -1, nullptr, &natW, nullptr, nullptr);
        gtk_widget_measure(content, GTK_ORIENTATION_VERTICAL, natW, nullptr, &natH, nullptr, nullptr);
        // A mapped GTK4 window ignores a new default size, but grows to fit its content's minimum size
        auto* sw = GTK_SCROLLED_WINDOW(scrolled);
        // The dialog also caps the content size; the minimum may not exceed that cap, so raise it first
        if (natW > gtk_widget_get_width(scrolled)) {
            if (int max = gtk_scrolled_window_get_max_content_width(sw); max != -1 && max < natW) {
                gtk_scrolled_window_set_max_content_width(sw, natW);
            }
            gtk_scrolled_window_set_min_content_width(sw, natW);
        }
        if (natH > gtk_widget_get_height(scrolled)) {
            if (int max = gtk_scrolled_window_get_max_content_height(sw); max != -1 && max < natH) {
                gtk_scrolled_window_set_max_content_height(sw, natH);
            }
            gtk_scrolled_window_set_min_content_height(sw, natH);
        }

    }
    g_object_unref(win);
    return G_SOURCE_REMOVE;
}
}  // namespace

void installColorChooserDialogSizeFix() {
    g_signal_connect(gtk_window_get_toplevels(), "items-changed",
                     G_CALLBACK(+[](GListModel* list, guint pos, guint, guint added, gpointer) {
                         for (guint i = pos; i < pos + added; i++) {
                             auto* win = static_cast<GtkWidget*>(g_list_model_get_item(list, i));  // full reference
                             if (g_str_equal(G_OBJECT_TYPE_NAME(win), "GtkColorChooserDialog") &&
                                 !g_object_get_data(G_OBJECT(win), "xopp-size-fix")) {
                                 g_object_set_data(G_OBJECT(win), "xopp-size-fix", GINT_TO_POINTER(1));
                                 if (GtkWidget* chooser = findDescendant(win, "GtkColorChooserWidget")) {
                                     g_signal_connect_object(
                                             chooser, "notify::show-editor",
                                             G_CALLBACK(+[](GObject* chooser, GParamSpec*, gpointer w) {
                                                 gboolean editor = false;
                                                 g_object_get(chooser, "show-editor", &editor, nullptr);
                                                 if (editor) {
                                                     // Measure once the editor is laid out
                                                     g_idle_add(fitWindowToScrolledContent, g_object_ref(w));
                                                 }
                                             }),
                                             win, GConnectFlags(0));
                                 }
                             }
                             g_object_unref(win);
                         }
                     }),
                     nullptr);
}
};  // namespace xoj::util::gtk
