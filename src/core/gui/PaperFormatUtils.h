/*
 * Xournal++
 *
 * Paper format utilities
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <gtk/gtk.h>

#include "util/raii/GtkPaperSizeUPtr.h"

#pragma once

namespace PaperFormatUtils {
/**
 * @brief A vector of menu options, either being a GtkPaperSize or the displayed name of the option.
 * @details The variant facilitates adding options which can't be expressed as a GtkPaperSize (e.g. a custom option
 *      or a copy current page format option)
 */
using PaperFormatMenuOptionVector = std::vector<std::variant<std::string, xoj::util::GtkPaperSizeUPtr>>;

/**
 * @brief Loads the default PaperSizes into the passed vector
 * @param menuOptions The vector containg the menu options
 */
void loadDefaultPaperSizes(PaperFormatMenuOptionVector& menuOptions);

/**
 * @brief Sets the entries of the drop down to the given GtkPaperSize entries and/or special entries.
 * Entry i of the drop down corresponds to menuOptions[i].
 */
void fillPaperFormatDropDown(const PaperFormatMenuOptionVector& menuOptions, GtkDropDown* paperFormatDropDown);
}  // namespace PaperFormatUtils