#include "PaperFormatUtils.h"

#include "util/DropDown.h"
#include "util/GListView.h"
#include "util/StringUtils.h"
#include "util/raii/GObjectSPtr.h"

using xoj::util::GtkPaperSizeUPtr;

void PaperFormatUtils::loadDefaultPaperSizes(PaperFormatMenuOptionVector& paperSizes) {
    GList* default_sizes = gtk_paper_size_get_paper_sizes(false);
    for (auto& s: GListView<GtkPaperSize>(default_sizes)) {
        std::string name = gtk_paper_size_get_name(&s);
        if (name == GTK_PAPER_NAME_A3 || name == GTK_PAPER_NAME_A4 || name == GTK_PAPER_NAME_A5 ||
            name == GTK_PAPER_NAME_LETTER || name == GTK_PAPER_NAME_LEGAL) {
            paperSizes.emplace_back(GtkPaperSizeUPtr(&s));
        } else {
            gtk_paper_size_free(&s);
        }
    }
    g_list_free(default_sizes);

    // Name format: ftp://ftp.pwg.org/pub/pwg/candidates/cs-pwgmsn10-20020226-5101.1.pdf
    paperSizes.emplace_back(GtkPaperSizeUPtr(gtk_paper_size_new("custom_16x9_320x180mm")));
    paperSizes.emplace_back(GtkPaperSizeUPtr(gtk_paper_size_new("custom_4x3_320x240mm")));
}
void PaperFormatUtils::fillPaperFormatDropDown(const PaperFormatMenuOptionVector& menuOptions,
                                               GtkDropDown* paperFormatDropDown) {
    std::vector<std::string> labels;
    labels.reserve(menuOptions.size());
    for (auto& menuOption: menuOptions) {
        if (menuOption.index()) {
            std::string displayName = gtk_paper_size_get_display_name(std::get<GtkPaperSizeUPtr>(menuOption).get());
            if (StringUtils::startsWith(displayName, "custom_")) {
                displayName = displayName.substr(7);
            }
            labels.push_back(std::move(displayName));
        } else {
            // Special non-GtkPaperSize option
            labels.push_back(std::get<std::string>(menuOption));
        }
    }
    // Entry i corresponds to menuOptions[i]
    xoj::util::dropdown::setItems(paperFormatDropDown, labels);
}
