#include "util/AppName.h"

#include "config.h"  // for XOPP_APP_NAME

namespace {
constexpr std::string_view ORIGINAL_NAME = "Xournal++";
}  // namespace

auto xoj::util::isRebranded() -> bool { return ORIGINAL_NAME != XOPP_APP_NAME; }

auto xoj::util::appName() -> const char* { return XOPP_APP_NAME; }

auto xoj::util::withAppName(std::string_view text) -> std::string {
    std::string res(text);
    if (!isRebranded()) {
        return res;
    }
    const std::string_view name = XOPP_APP_NAME;
    for (size_t pos = res.find(ORIGINAL_NAME); pos != std::string::npos;
         pos = res.find(ORIGINAL_NAME, pos + name.size())) {
        res.replace(pos, ORIGINAL_NAME.size(), name);
    }
    return res;
}

void xoj::util::applyAppName(GtkBuilder* builder) {
    if (!isRebranded() || !builder) {
        return;
    }
    auto contains = [](const char* s) { return s && std::string_view(s).find(ORIGINAL_NAME) != std::string_view::npos; };

    GSList* objects = gtk_builder_get_objects(builder);
    for (GSList* it = objects; it; it = it->next) {
        GObject* o = G_OBJECT(it->data);
        if (GTK_IS_WINDOW(o)) {
            const char* title = gtk_window_get_title(GTK_WINDOW(o));
            if (contains(title)) {
                gtk_window_set_title(GTK_WINDOW(o), withAppName(title).c_str());
            }
        }
        if (GTK_IS_LABEL(o)) {
            const char* label = gtk_label_get_label(GTK_LABEL(o));  // keeps the markup
            if (contains(label)) {
                gtk_label_set_label(GTK_LABEL(o), withAppName(label).c_str());
            }
        }
        if (GTK_IS_BUTTON(o)) {
            const char* label = gtk_button_get_label(GTK_BUTTON(o));
            if (contains(label)) {
                gtk_button_set_label(GTK_BUTTON(o), withAppName(label).c_str());
            }
        }
        if (GTK_IS_CHECK_BUTTON(o)) {
            const char* label = gtk_check_button_get_label(GTK_CHECK_BUTTON(o));
            if (contains(label)) {
                gtk_check_button_set_label(GTK_CHECK_BUTTON(o), withAppName(label).c_str());
            }
        }
        if (GTK_IS_WIDGET(o)) {
            const char* tooltip = gtk_widget_get_tooltip_markup(GTK_WIDGET(o));
            if (contains(tooltip)) {
                gtk_widget_set_tooltip_markup(GTK_WIDGET(o), withAppName(tooltip).c_str());
            }
        }
    }
    g_slist_free(objects);
}
