/*
 * Xournal++
 *
 * Toolbar edit dialog
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>

#include <memory>

#include <gtk/gtk.h>  // for GtkButton, GtkListBox

#include "util/raii/GtkWindowUPtr.h"

class ToolbarData;
class ToolbarModel;
class GladeSearchpath;

class ToolbarManageDialog {
public:
    ToolbarManageDialog(GladeSearchpath* gladeSearchPath, ToolbarModel* model, std::function<void()> callback);
    ~ToolbarManageDialog();

public:
    inline GtkWindow* getWindow() const { return window.get(); }

private:
    static void selectionChangedCallback(GtkListBox* list, GtkListBoxRow* row, ToolbarManageDialog* dlg);
    static void nameEditedCallback(GtkEditableLabel* label, GParamSpec*, ToolbarManageDialog* dlg);

    void appendHeader(const char* title);
    GtkListBoxRow* appendEntry(ToolbarData* data);

    static void buttonNewCallback(GtkButton* button, ToolbarManageDialog* dlg);
    static void buttonDeleteCallback(GtkButton* button, ToolbarManageDialog* dlg);
    static void buttonCopyCallback(GtkButton* button, ToolbarManageDialog* dlg);

    void addToolbarData(std::unique_ptr<ToolbarData> tbd);
    void entrySelected(ToolbarData* data);

    void updateSelectionData();

    ToolbarData* getSelectedEntry();

private:
    ToolbarModel* tbModel;
    GtkListBox* list;
    GtkWidget* copyButton;
    GtkWidget* deleteButton;

    xoj::util::GtkWindowUPtr window;
    std::function<void()> callback;
};
