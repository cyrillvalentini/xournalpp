#include "LinkDialog.h"

#include "control/Control.h"           // for Control
#include "gui/Builder.h"               // for Builder
#include "gui/GladeSearchpath.h"       // for GladeSearchPath
#include "util/DropDown.h"
#include "util/glib_casts.h"           // for wrap_for_g_callback_v
#include "util/raii/CStringWrapper.h"  // for OwnedCString

constexpr auto UI_FILE = "linkDialog.ui";
constexpr auto UI_DIALOG_NAME = "linkDialog";

static void okButtonClicked(GtkButton*, LinkDialog* dialog) { dialog->okButtonPressed(); }
static void cancelButtonClicked(GtkButton*, LinkDialog* dialog) { dialog->cancelButtonPressed(); }
static void layoutToggledLeft(GtkButton*, LinkDialog* dialog) { dialog->layoutToggled(TextAlignment::LEFT); };
static void layoutToggledCenter(GtkButton*, LinkDialog* dialog) { dialog->layoutToggled(TextAlignment::CENTER); };
static void layoutToggledRight(GtkButton*, LinkDialog* dialog) { dialog->layoutToggled(TextAlignment::RIGHT); };
static void urlPrefixChangedClb(GtkDropDown* source, gpointer dialog) {
    static_cast<LinkDialog*>(dialog)->urlPrefixChanged(source);
}

LinkDialog::LinkDialog(Control* control, std::function<void(LinkDialog*)> callbackOK,
                       std::function<void()> callbackCancel):
        callbackOK(std::move(callbackOK)), callbackCancel(std::move(callbackCancel)) {

    Builder builder(control->getGladeSearchPath(), UI_FILE);

    this->linkDialog.reset(GTK_WINDOW(builder.get(UI_DIALOG_NAME)));

    this->textInput = GTK_TEXT_VIEW(builder.get("inpLinkEditText"));
    this->urlInput = GTK_ENTRY(builder.get("inpLinkEditURL"));
    auto* okButton = GTK_BUTTON(builder.get("btOk"));
    auto* cancelButton = GTK_BUTTON(builder.get("btCancel"));

    this->fontChooser = GTK_FONT_DIALOG_BUTTON(builder.get("btnFontChooser"));

    this->layoutLeft = GTK_TOGGLE_BUTTON(builder.get("btnLeftLayout"));
    this->layoutCenter = GTK_TOGGLE_BUTTON(builder.get("btnCenterLayout"));
    this->layoutRight = GTK_TOGGLE_BUTTON(builder.get("btnRightLayout"));

    this->linkTypeChooser = GTK_DROP_DOWN(builder.get("cbLinkPrefix"));
    // The URLPrefix of each entry (same order as in linkDialog.ui)
    xoj::util::dropdown::setIds(this->linkTypeChooser, {"1", "2", "3", "4", "0"});
    this->urlPrefixChanged(this->linkTypeChooser);

    g_signal_connect(G_OBJECT(okButton), "clicked", xoj::util::wrap_for_g_callback_v<okButtonClicked>, this);
    g_signal_connect(G_OBJECT(cancelButton), "clicked", xoj::util::wrap_for_g_callback_v<cancelButtonClicked>, this);

    g_signal_connect(G_OBJECT(layoutLeft), "clicked", xoj::util::wrap_for_g_callback_v<layoutToggledLeft>, this);
    g_signal_connect(G_OBJECT(layoutCenter), "clicked", xoj::util::wrap_for_g_callback_v<layoutToggledCenter>, this);
    g_signal_connect(G_OBJECT(layoutRight), "clicked", xoj::util::wrap_for_g_callback_v<layoutToggledRight>, this);

    xoj::util::dropdown::connectChanged(linkTypeChooser, urlPrefixChangedClb, this);
}

LinkDialog::~LinkDialog() { this->callbackCancel(); };

void LinkDialog::preset(XojFont font, std::string text, std::string url, TextAlignment layout) {
    GtkTextBuffer* textBuffer = gtk_text_view_get_buffer(this->textInput);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(textBuffer, &start, &end);
    gtk_text_buffer_insert(textBuffer, &start, text.c_str(), static_cast<int>(text.length()));
    gtk_text_view_set_buffer(this->textInput, textBuffer);
    URLPrefix prefix = this->identifyAndShortenURL(url);
    gtk_editable_set_text(GTK_EDITABLE(this->urlInput), url.c_str());
    xoj::util::dropdown::setActiveId(this->linkTypeChooser, std::to_string(static_cast<int>(prefix)));
    std::string fontName = font.getName() + " " + std::to_string(font.getSize());
    {
        PangoFontDescription* desc = pango_font_description_from_string(fontName.c_str());
        gtk_font_dialog_button_set_font_desc(this->fontChooser, desc);
        pango_font_description_free(desc);
    }
    this->layoutToggled(layout);
}

std::string LinkDialog::getText() { return this->linkText; }

std::string LinkDialog::getURL() {
    std::string res = xoj::util::dropdown::getActiveText(this->linkTypeChooser);
    if (res == "<custom>") {
        res = "";
    }
    res = res + this->linkURL;
    return res;
}

void LinkDialog::okButtonPressed() {
    GtkTextBuffer* text = gtk_text_view_get_buffer(this->textInput);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(text, &start, &end);
    this->linkText = std::string(gtk_text_buffer_get_text(text, &start, &end, TRUE));
    if (!this->isTextValid(this->linkText)) {
        this->linkText = "Link";
    }

    this->linkURL = std::string(gtk_editable_get_text(GTK_EDITABLE(this->urlInput)));
    if (!this->isUrlValid(this->linkURL)) {
        this->linkURL = PLACEHOLDER_HTTPS;
    }
    this->callbackOK(this);
    gtk_window_close(this->linkDialog.get());
}

void LinkDialog::cancelButtonPressed() { gtk_window_close(this->linkDialog.get()); }

bool LinkDialog::isTextValid(const std::string& text) {
    return !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isblank(c); });
}

bool LinkDialog::isUrlValid(const std::string& url) { return !url.empty(); }

void LinkDialog::layoutToggled(TextAlignment layout) {
    this->layout = layout;
    gtk_toggle_button_set_active(this->layoutLeft, layout == TextAlignment::LEFT);
    gtk_toggle_button_set_active(this->layoutCenter, layout == TextAlignment::CENTER);
    gtk_toggle_button_set_active(this->layoutRight, layout == TextAlignment::RIGHT);
}

TextAlignment LinkDialog::getLayout() { return this->layout; }

XojFont LinkDialog::getFont() {
    auto font = xoj::util::OwnedCString::assumeOwnership(
            pango_font_description_to_string(gtk_font_dialog_button_get_font_desc(this->fontChooser)));
    return XojFont(font.get());
}

void LinkDialog::urlPrefixChanged(GtkDropDown* eventSource) {
    URLPrefix prefix = static_cast<URLPrefix>(std::stoi(xoj::util::dropdown::getActiveId(eventSource).value_or("0")));
    switch (prefix) {
        case URLPrefix::NONE:
            gtk_entry_set_placeholder_text(this->urlInput, PLACEHOLDER_OTHER);
            break;
        case URLPrefix::HTTP:
        case URLPrefix::HTTPS:
            gtk_entry_set_placeholder_text(this->urlInput, PLACEHOLDER_HTTPS);
            break;
        case URLPrefix::MAILTO:
            gtk_entry_set_placeholder_text(this->urlInput, PLACEHOLDER_MAIL);
            break;
        case URLPrefix::FILE:
            gtk_entry_set_placeholder_text(this->urlInput, PLACEHOLDER_FILE);
            break;
        default:
            break;
    }
}

URLPrefix LinkDialog::identifyAndShortenURL(std::string& url) {
    if (url.rfind("https://", 0) == 0) {
        url = url.substr(8);
        return URLPrefix::HTTPS;
    } else if (url.rfind("http://", 0) == 0) {
        url = url.substr(7);
        return URLPrefix::HTTP;
    } else if (url.rfind("mailto:", 0) == 0) {
        url = url.substr(7);
        return URLPrefix::MAILTO;
    } else if (url.rfind("file://", 0) == 0) {
        url = url.substr(7);
        return URLPrefix::FILE;
    } else {
        return URLPrefix::NONE;
    }
}
