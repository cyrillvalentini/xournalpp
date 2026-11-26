/*
 * Xournal++
 *
 * Helpers for GtkDropDown with a GtkStringList model, replacing GtkComboBox(Text) (deprecated since GTK 4.10).
 * GtkDropDown has no item ids; ids can be attached to a drop down with setItems()/setIds() and used with
 * getActiveId()/setActiveId().
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <gtk/gtk.h>

namespace xoj::util::dropdown {

/// Returns the drop down's GtkStringList, creating and setting an empty one if it has none
GtkStringList* getStringList(GtkDropDown* dd);

/// Replaces all items. `ids` (if given) must have the same length as `labels`.
void setItems(GtkDropDown* dd, const std::vector<std::string>& labels, std::vector<std::string> ids = {});

/// Attaches ids to the existing items (same order and length)
void setIds(GtkDropDown* dd, std::vector<std::string> ids);

/// Appends an item; `id` may be empty
void append(GtkDropDown* dd, const std::string& label, const std::string& id = {});

/// Removes all items (and their ids)
void clear(GtkDropDown* dd);

/// Index of the selected item, or -1 if none
int getActive(GtkDropDown* dd);
/// Selects the item at `index`; a negative index clears the selection
void setActive(GtkDropDown* dd, int index);

/// Id of the selected item, if any
std::optional<std::string> getActiveId(GtkDropDown* dd);
/// Selects the item with the given id. Returns false if there is none.
bool setActiveId(GtkDropDown* dd, const std::string& id);

/// Label of the selected item (empty if none)
std::string getActiveText(GtkDropDown* dd);

/// Calls `callback(dd, data)` whenever the selection changes (the GtkComboBox "changed" signal)
gulong connectChanged(GtkDropDown* dd, void (*callback)(GtkDropDown*, gpointer), gpointer data);

}  // namespace xoj::util::dropdown
