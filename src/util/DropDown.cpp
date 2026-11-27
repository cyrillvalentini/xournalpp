#include "util/DropDown.h"

#include <utility>  // for move

namespace {
constexpr auto IDS_KEY = "xopp-dropdown-ids";

auto ids(GtkDropDown* dd) -> std::vector<std::string>* {
    auto* v = static_cast<std::vector<std::string>*>(g_object_get_data(G_OBJECT(dd), IDS_KEY));
    if (!v) {
        v = new std::vector<std::string>();
        g_object_set_data_full(
                G_OBJECT(dd), IDS_KEY, v, +[](gpointer p) { delete static_cast<std::vector<std::string>*>(p); });
    }
    return v;
}
}  // namespace

namespace xoj::util::dropdown {

GtkStringList* getStringList(GtkDropDown* dd) {
    GListModel* model = gtk_drop_down_get_model(dd);
    if (model && GTK_IS_STRING_LIST(model)) {
        return GTK_STRING_LIST(model);
    }
    GtkStringList* list = gtk_string_list_new(nullptr);
    gtk_drop_down_set_model(dd, G_LIST_MODEL(list));
    g_object_unref(list);  // the drop down holds a reference
    return list;
}

void setItems(GtkDropDown* dd, const std::vector<std::string>& labels, std::vector<std::string> newIds) {
    std::vector<const char*> strings;
    strings.reserve(labels.size() + 1);
    for (const auto& l: labels) {
        strings.push_back(l.c_str());
    }
    strings.push_back(nullptr);
    GtkStringList* list = gtk_string_list_new(strings.data());
    gtk_drop_down_set_model(dd, G_LIST_MODEL(list));
    g_object_unref(list);
    *ids(dd) = std::move(newIds);
}

void setIds(GtkDropDown* dd, std::vector<std::string> newIds) { *ids(dd) = std::move(newIds); }

void append(GtkDropDown* dd, const std::string& label, const std::string& id) {
    GtkStringList* list = getStringList(dd);
    auto* v = ids(dd);
    v->resize(g_list_model_get_n_items(G_LIST_MODEL(list)));  // keep ids aligned with the items
    gtk_string_list_append(list, label.c_str());
    v->push_back(id);
}

void clear(GtkDropDown* dd) {
    GtkStringList* list = getStringList(dd);
    gtk_string_list_splice(list, 0, g_list_model_get_n_items(G_LIST_MODEL(list)), nullptr);
    ids(dd)->clear();
}

int getActive(GtkDropDown* dd) {
    guint i = gtk_drop_down_get_selected(dd);
    return i == GTK_INVALID_LIST_POSITION ? -1 : static_cast<int>(i);
}

void setActive(GtkDropDown* dd, int index) {
    gtk_drop_down_set_selected(dd, index < 0 ? GTK_INVALID_LIST_POSITION : static_cast<guint>(index));
}

std::optional<std::string> getActiveId(GtkDropDown* dd) {
    int i = getActive(dd);
    auto* v = ids(dd);
    if (i < 0 || static_cast<size_t>(i) >= v->size()) {
        return std::nullopt;
    }
    return (*v)[static_cast<size_t>(i)];
}

bool setActiveId(GtkDropDown* dd, const std::string& id) {
    auto* v = ids(dd);
    for (size_t i = 0; i < v->size(); i++) {
        if ((*v)[i] == id) {
            gtk_drop_down_set_selected(dd, static_cast<guint>(i));
            return true;
        }
    }
    return false;
}

std::string getActiveText(GtkDropDown* dd) {
    gpointer item = gtk_drop_down_get_selected_item(dd);
    if (item && GTK_IS_STRING_OBJECT(item)) {
        return gtk_string_object_get_string(GTK_STRING_OBJECT(item));
    }
    return {};
}

gulong connectChanged(GtkDropDown* dd, void (*callback)(GtkDropDown*, gpointer), gpointer data) {
    // notify::selected passes a GParamSpec in the middle; forward to the two-argument callback
    struct Closure {
        void (*cb)(GtkDropDown*, gpointer);
        gpointer data;
    };
    auto* c = new Closure{callback, data};
    return g_signal_connect_data(
            dd, "notify::selected", G_CALLBACK(+[](GtkDropDown* d, GParamSpec*, gpointer p) {
                auto* c = static_cast<Closure*>(p);
                c->cb(d, c->data);
            }),
            c, +[](gpointer p, GClosure*) { delete static_cast<Closure*>(p); }, GConnectFlags(0));
}

}  // namespace xoj::util::dropdown
