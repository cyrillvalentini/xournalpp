#include "ToolbarManageDialog.h"

#include <string>  // for allocator, string
#include <vector>  // for vector

#include <glib-object.h>  // for g_signal_connect

#include "gui/Builder.h"
#include "gui/toolbarMenubar/model/ToolbarData.h"   // for ToolbarData
#include "gui/toolbarMenubar/model/ToolbarModel.h"  // for ToolbarModel
#include "util/Assert.h"                            // for xoj_assert
#include "util/i18n.h"                              // for _

class GladeSearchpath;

constexpr auto UI_FILE = "toolbarManageDialog.ui";
constexpr auto UI_DIALOG_NAME = "manageToolbarsDialog";

/// Key under which each entry row stores its ToolbarData*
constexpr auto DATA_KEY = "toolbar-data";

ToolbarManageDialog::ToolbarManageDialog(GladeSearchpath* gladeSearchPath, ToolbarModel* tbModel,
                                         std::function<void()> callback):
        tbModel(tbModel), callback(callback) {
    xoj_assert(tbModel);
    Builder builder(gladeSearchPath, UI_FILE);
    window.reset(GTK_WINDOW(builder.get(UI_DIALOG_NAME)));
    list = GTK_LIST_BOX(builder.get("toolbarList"));
    copyButton = builder.get("btCopy");
    deleteButton = builder.get("btDelete");

    appendHeader(_("Predefined"));
    for (const auto& data: tbModel->getToolbars()) {
        if (data->isPredefined()) {
            appendEntry(data.get());
        }
    }
    appendHeader(_("Customized"));
    for (const auto& data: tbModel->getToolbars()) {
        if (!data->isPredefined()) {
            appendEntry(data.get());
        }
    }

    g_signal_connect(list, "row-selected", G_CALLBACK(selectionChangedCallback), this);

    g_signal_connect(builder.get("btNew"), "clicked", G_CALLBACK(buttonNewCallback), this);
    g_signal_connect(deleteButton, "clicked", G_CALLBACK(buttonDeleteCallback), this);
    g_signal_connect(copyButton, "clicked", G_CALLBACK(buttonCopyCallback), this);

    g_signal_connect_swapped(builder.get("btClose"), "clicked", G_CALLBACK(gtk_window_close), this->window.get());
}

ToolbarManageDialog::~ToolbarManageDialog() {
    // The window (and the list with it) may be destroyed after this object: the list then removes its rows and emits
    // "row-selected", and the labels emit "notify::editing". Make sure none of these reach us anymore.
    g_signal_handlers_disconnect_by_data(list, this);
    for (GtkWidget* row = gtk_widget_get_first_child(GTK_WIDGET(list)); row; row = gtk_widget_get_next_sibling(row)) {
        if (GTK_IS_LIST_BOX_ROW(row)) {
            if (GtkWidget* label = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row))) {
                g_signal_handlers_disconnect_by_data(label, this);
            }
        }
    }
    this->callback();
}

void ToolbarManageDialog::appendHeader(const char* title) {
    GtkWidget* label = gtk_label_new(title);
    gtk_widget_add_css_class(label, "heading");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);

    GtkWidget* row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), false);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), false);
    gtk_widget_set_focusable(row, false);
    gtk_list_box_append(list, row);
}

auto ToolbarManageDialog::appendEntry(ToolbarData* data) -> GtkListBoxRow* {
    GtkWidget* label = nullptr;
    if (data->isPredefined()) {
        label = gtk_label_new(data->getName().c_str());
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    } else {
        // Custom toolbars can be renamed in place (double click or Enter)
        label = gtk_editable_label_new(data->getName().c_str());
        g_signal_connect(label, "notify::editing", G_CALLBACK(nameEditedCallback), this);
    }
    gtk_widget_set_margin_start(label, 12);

    GtkWidget* row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
    g_object_set_data(G_OBJECT(row), DATA_KEY, data);
    gtk_list_box_append(list, row);
    return GTK_LIST_BOX_ROW(row);
}

void ToolbarManageDialog::buttonNewCallback(GtkButton* button, ToolbarManageDialog* dlg) {
    auto data = std::make_unique<ToolbarData>(false);
    data->setName(_("New"));
    data->setId("custom");
    dlg->tbModel->initCopyNameId(data.get());
    dlg->addToolbarData(std::move(data));
}

void ToolbarManageDialog::buttonDeleteCallback(GtkButton* button, ToolbarManageDialog* dlg) {
    ToolbarData* selected = dlg->getSelectedEntry();
    if (selected == nullptr || selected->isPredefined()) {
        return;
    }

    gtk_list_box_remove(dlg->list, GTK_WIDGET(gtk_list_box_get_selected_row(dlg->list)));
    dlg->tbModel->remove(selected);
    dlg->updateSelectionData();
}

void ToolbarManageDialog::buttonCopyCallback(GtkButton* button, ToolbarManageDialog* dlg) {
    ToolbarData* selected = dlg->getSelectedEntry();
    if (selected == nullptr) {
        return;
    }

    auto data = std::make_unique<ToolbarData>(*selected);
    dlg->tbModel->initCopyNameId(data.get());
    dlg->addToolbarData(std::move(data));
}

void ToolbarManageDialog::addToolbarData(std::unique_ptr<ToolbarData> tbd) {
    auto* data = this->tbModel->add(std::move(tbd));

    // The "Customized" section is the last one, so new entries go at the end
    GtkListBoxRow* row = appendEntry(data);
    gtk_list_box_select_row(list, row);
    GtkWidget* label = gtk_list_box_row_get_child(row);
    gtk_widget_grab_focus(label);
    gtk_editable_label_start_editing(GTK_EDITABLE_LABEL(label));
}

void ToolbarManageDialog::nameEditedCallback(GtkEditableLabel* label, GParamSpec*, ToolbarManageDialog* dlg) {
    if (gtk_editable_label_get_editing(label)) {
        return;
    }
    GtkWidget* row = gtk_widget_get_ancestor(GTK_WIDGET(label), GTK_TYPE_LIST_BOX_ROW);
    auto* data = row ? static_cast<ToolbarData*>(g_object_get_data(G_OBJECT(row), DATA_KEY)) : nullptr;
    if (!data) {
        return;
    }
    std::string name = gtk_editable_get_text(GTK_EDITABLE(label));
    if (name.find_first_not_of(" \t") == std::string::npos) {
        // An empty name would make the toolbar invisible in the menus: keep the previous one
        gtk_editable_set_text(GTK_EDITABLE(label), data->getName().c_str());
        return;
    }
    data->setName(name);
}

void ToolbarManageDialog::entrySelected(ToolbarData* data) {
    if (data == nullptr) {
        gtk_widget_set_sensitive(copyButton, false);
        gtk_widget_set_sensitive(deleteButton, false);
    } else {
        gtk_widget_set_sensitive(copyButton, true);
        gtk_widget_set_sensitive(deleteButton, !data->isPredefined());
    }
}

auto ToolbarManageDialog::getSelectedEntry() -> ToolbarData* {
    GtkListBoxRow* row = gtk_list_box_get_selected_row(list);
    if (row == nullptr) {
        return nullptr;
    }
    return static_cast<ToolbarData*>(g_object_get_data(G_OBJECT(row), DATA_KEY));
}

void ToolbarManageDialog::updateSelectionData() { entrySelected(getSelectedEntry()); }

void ToolbarManageDialog::selectionChangedCallback(GtkListBox*, GtkListBoxRow*, ToolbarManageDialog* dlg) {
    dlg->updateSelectionData();
}
