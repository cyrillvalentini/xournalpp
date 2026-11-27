#include "ButtonConfigGui.h"

#include <memory>   // for allocator, allocator_trai...
#include <utility>  // for pair

#include <gdk-pixbuf/gdk-pixbuf.h>  // for GDK_TYPE_PIXBUF, GdkPixbuf
#include <gdk/gdk.h>                // for GdkRGBA
#include <glib-object.h>            // for g_value_get_int, GValue

#include "control/DeviceListHelper.h"       // for InputDevice, getDeviceList
#include "control/settings/ButtonConfig.h"  // for ButtonConfig
#include "control/settings/Settings.h"      // for Settings
#include "gui/Builder.h"
#include "util/Color.h"  // for GdkRGBA_to_argb, rgb_to_G...
#include "util/DropDown.h"
#include "util/i18n.h"  // for _

class GladeSearchpath;

constexpr auto UI_FILE = "settingsButtonConfig.ui";
constexpr auto UI_WIDGET_NAME = "mainBox";

ButtonConfigGui::ToolSizeIndexMap ButtonConfigGui::toolSizeIndexMap = {{0, TOOL_SIZE_NONE},  {1, TOOL_SIZE_VERY_FINE},
                                                                       {2, TOOL_SIZE_FINE},  {3, TOOL_SIZE_MEDIUM},
                                                                       {4, TOOL_SIZE_THICK}, {5, TOOL_SIZE_VERY_THICK}};

std::string ButtonConfigGui::toolSizeToLabel(ToolSize size) {
    switch (size) {
        case TOOL_SIZE_NONE:
            return _("Thickness - don't change");
        case TOOL_SIZE_VERY_FINE:
            return _("Very thin");
        case TOOL_SIZE_FINE:
            return _("Thin");
        case TOOL_SIZE_MEDIUM:
            return _("Medium");
        case TOOL_SIZE_THICK:
            return _("Thick");
        case TOOL_SIZE_VERY_THICK:
            return _("Very thick");
        default:
            return "";
    }
}

ButtonConfigGui::ButtonConfigGui(GladeSearchpath* gladeSearchPath, GtkBox* box, Settings* settings, unsigned int button,
                                 bool withDevice):
        settings(settings), button(button), withDevice(withDevice), iconNameHelper(settings) {
    Builder builder(gladeSearchPath, UI_FILE);

    gtk_box_append(box, builder.get(UI_WIDGET_NAME));  // box takes ownership of it all!

    this->cbDevice = builder.get("labelDevice");
    this->cbDisableDrawing = builder.get("cbDisableDrawing");

    if (withDevice) {
        xoj::util::dropdown::append(GTK_DROP_DOWN(this->cbDevice), _("No device"));

        this->deviceList = DeviceListHelper::getDeviceList(this->settings, true);
        for (InputDevice const& dev: this->deviceList) {
            std::string txt = dev.getName() + " (" + dev.getType() + ")";
            xoj::util::dropdown::append(GTK_DROP_DOWN(this->cbDevice), txt);
        }
    } else {
        gtk_widget_set_visible(builder.get("lbDevice"), false);
        gtk_widget_set_visible(this->cbDevice, false);
        gtk_widget_set_visible(this->cbDisableDrawing, false);
    }

    // Entry i of cbTool is toolEntries[i] (icon name, tool); the drop down shows the icon next to the label
    std::vector<std::string> toolLabels;
    auto addTypeCB = [&](const char* icon, const char* name, ToolType action) {
        toolEntries.emplace_back(iconNameHelper.iconName(icon), action);
        toolLabels.emplace_back(name);
    };

    addTypeCB("transparent", _("Tool - don't change"), TOOL_NONE);
    addTypeCB("tool-pencil", _("Pen"), TOOL_PEN);
    addTypeCB("tool-eraser", _("Eraser"), TOOL_ERASER);
    addTypeCB("tool-highlighter", _("Highlighter"), TOOL_HIGHLIGHTER);
    addTypeCB("tool-text", _("Text"), TOOL_TEXT);
    addTypeCB("tool-image", _("Insert image"), TOOL_IMAGE);
    addTypeCB("laser-pointer", _("Laser Pointer - Pen"), TOOL_LASER_POINTER_PEN);
    addTypeCB("laser-pointer", _("Laser Pointer - Highlighter"), TOOL_LASER_POINTER_HIGHLIGHTER);
    addTypeCB("vertical-space", _("Vertical space"), TOOL_VERTICAL_SPACE);
    addTypeCB("select-lasso", _("Select region"), TOOL_SELECT_REGION);
    addTypeCB("select-rect", _("Select rectangle"), TOOL_SELECT_RECT);
    addTypeCB("select-multilayer-lasso", _("Select multi layer region"), TOOL_SELECT_MULTILAYER_REGION);
    addTypeCB("select-multilayer-rect", _("Select multi layer rect"), TOOL_SELECT_MULTILAYER_RECT);
    addTypeCB("hand", _("Hand"), TOOL_HAND);
    addTypeCB("floating-toolbox", _("Floating Toolbox (experimental)"), TOOL_FLOATING_TOOLBOX);
    addTypeCB("select-pdf-text-ht", _("Select Text from pdf"), TOOL_SELECT_PDF_TEXT_LINEAR);
    addTypeCB("select-pdf-text-area", _("Select Area Text from pdf"), TOOL_SELECT_PDF_TEXT_RECT);

    this->cbTool = builder.get("cbTool");
    xoj::util::dropdown::setItems(GTK_DROP_DOWN(this->cbTool), toolLabels);
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
                         GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
                         gtk_box_append(GTK_BOX(box), gtk_image_new());
                         gtk_box_append(GTK_BOX(box), gtk_label_new(nullptr));
                         gtk_list_item_set_child(item, box);
                     }),
                     nullptr);
    g_signal_connect(factory, "bind", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer d) {
                         auto* self = static_cast<ButtonConfigGui*>(d);
                         GtkWidget* box = gtk_list_item_get_child(item);
                         GtkWidget* image = gtk_widget_get_first_child(box);
                         GtkWidget* label = gtk_widget_get_next_sibling(image);
                         gtk_label_set_text(GTK_LABEL(label), gtk_string_object_get_string(
                                                                      GTK_STRING_OBJECT(gtk_list_item_get_item(item))));
                         guint pos = gtk_list_item_get_position(item);
                         if (pos < self->toolEntries.size()) {
                             gtk_image_set_from_icon_name(GTK_IMAGE(image), self->toolEntries[pos].first.c_str());
                         }
                     }),
                     this);
    gtk_drop_down_set_factory(GTK_DROP_DOWN(this->cbTool), factory);
    g_object_unref(factory);
    xoj::util::dropdown::connectChanged(GTK_DROP_DOWN(this->cbTool), cbSelectCallback, this);

    this->cbThickness = builder.get("cbThickness");
    for (auto const& size: toolSizeIndexMap) {  // ordered by index
        xoj::util::dropdown::append(GTK_DROP_DOWN(cbThickness), toolSizeToLabel(size.second));
    }
    xoj::util::dropdown::setActive(GTK_DROP_DOWN(cbThickness), 0);

    this->colorButton = builder.get("colorButton");

    this->cbColor = builder.get("cbColor");
    // 0: don't change
    xoj::util::dropdown::append(GTK_DROP_DOWN(this->cbColor), _("Color - don't change"));
    // 1: custom color
    xoj::util::dropdown::append(GTK_DROP_DOWN(this->cbColor), _("Custom color"));
    xoj::util::dropdown::setActive(GTK_DROP_DOWN(this->cbColor), 0);
    xoj::util::dropdown::connectChanged(GTK_DROP_DOWN(cbColor), cbColorCallback, this);

    this->cbDrawingType = builder.get("cbDrawingType");
    // DRAWING_TYPE_DONT_CHANGE
    xoj::util::dropdown::append(GTK_DROP_DOWN(this->cbDrawingType), _("Drawing Type - don't change"));
    // DRAWING_TYPE_DEFAULT
    xoj::util::dropdown::append(GTK_DROP_DOWN(this->cbDrawingType), _("Normal drawing"));
    // DRAWING_TYPE_RULER
    xoj::util::dropdown::append(GTK_DROP_DOWN(this->cbDrawingType), _("Draw Line"));
    // DRAWING_TYPE_RECTANGLE
    xoj::util::dropdown::append(GTK_DROP_DOWN(this->cbDrawingType), _("Draw Rectangle"));
    // DRAWING_TYPE_ELLIPSE
    xoj::util::dropdown::append(GTK_DROP_DOWN(this->cbDrawingType), _("Draw Ellipse"));
    // DRAWING_TYPE_ARROW
    xoj::util::dropdown::append(GTK_DROP_DOWN(this->cbDrawingType), _("Draw Arrow"));
    // DRAWING_TYPE_DOUBLE_ARROW
    xoj::util::dropdown::append(GTK_DROP_DOWN(this->cbDrawingType), _("Draw Double Arrow"));
    // DRAWING_TYPE_COORDINATE_SYSTEM
    xoj::util::dropdown::append(GTK_DROP_DOWN(this->cbDrawingType), _("Draw Coordinate System"));
    // DRAWING_TYPE_STROKE_RECOGNIZER
    xoj::util::dropdown::append(GTK_DROP_DOWN(this->cbDrawingType), _("Shape Recognizer"));
    // DRAWING_TYPE_SPLINE
    xoj::util::dropdown::append(GTK_DROP_DOWN(this->cbDrawingType), _("Draw Spline"));


    // Values in glade GUI!
    this->cbEraserType = builder.get("cbEraserType");

    // Possible values are defined in the .glade file
    this->cbStrokeType = builder.get("cbStrokeType");

    loadSettings();
}

void ButtonConfigGui::loadSettings() {
    ButtonConfig* cfg = settings->getButtonConfig(button);

    xoj::util::dropdown::setActive(GTK_DROP_DOWN(cbTool), 0);
    for (size_t i = 0; i < toolEntries.size(); i++) {
        if (toolEntries[i].second == cfg->action) {
            xoj::util::dropdown::setActive(GTK_DROP_DOWN(cbTool), static_cast<int>(i));
            break;
        }
    }

    // select thickness in combo box
    xoj::util::dropdown::setActive(GTK_DROP_DOWN(cbThickness), 0);
    for (auto const& size: toolSizeIndexMap) {
        if (size.second == cfg->size) {
            xoj::util::dropdown::setActive(GTK_DROP_DOWN(cbThickness), size.first);
        }
    }

    GdkRGBA color = Util::rgb_to_GdkRGBA(cfg->color.value_or(Colors::black));
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(colorButton), &color);

    xoj::util::dropdown::setActive(GTK_DROP_DOWN(cbColor), cfg->color.has_value() ? 1 : 0);

    xoj::util::dropdown::setActive(GTK_DROP_DOWN(this->cbDrawingType), cfg->drawingType);

    if (cfg->eraserMode == ERASER_TYPE_DEFAULT) {
        xoj::util::dropdown::setActive(GTK_DROP_DOWN(cbEraserType), 1);
    } else if (cfg->eraserMode == ERASER_TYPE_WHITEOUT) {
        xoj::util::dropdown::setActive(GTK_DROP_DOWN(cbEraserType), 2);
    } else if (cfg->eraserMode == ERASER_TYPE_DELETE_STROKE) {
        xoj::util::dropdown::setActive(GTK_DROP_DOWN(cbEraserType), 3);
    } else {
        xoj::util::dropdown::setActive(GTK_DROP_DOWN(cbEraserType), 0);
    }

    xoj::util::dropdown::setActive(GTK_DROP_DOWN(cbStrokeType), cfg->strokeType);

    if (withDevice) {
        xoj::util::dropdown::setActive(GTK_DROP_DOWN(cbDevice), 0);

        int i = 1;
        for (InputDevice const& dev: this->deviceList) {
            if (cfg->device == dev.getName()) {
                xoj::util::dropdown::setActive(GTK_DROP_DOWN(cbDevice), i);
                break;
            }

            ++i;
        }

        gtk_check_button_set_active(GTK_CHECK_BUTTON(cbDisableDrawing), cfg->disableDrawing);
    }

    enableDisableTools();
}

void ButtonConfigGui::saveSettings() {
    int toolIndex = xoj::util::dropdown::getActive(GTK_DROP_DOWN(cbTool));
    auto action = toolIndex >= 0 && static_cast<size_t>(toolIndex) < toolEntries.size() ?
                          toolEntries[static_cast<size_t>(toolIndex)].second :
                          TOOL_NONE;
    ButtonConfig* cfg = settings->getButtonConfig(button);
    cfg->action = action;

    int thickness = xoj::util::dropdown::getActive(GTK_DROP_DOWN(cbThickness));
    if (toolSizeIndexMap.count(thickness) == 0) {
        cfg->size = TOOL_SIZE_NONE;
    } else {
        cfg->size = toolSizeIndexMap[thickness];
    }

    if (xoj::util::dropdown::getActive(GTK_DROP_DOWN(cbColor)) != 1) {
        cfg->color = std::nullopt;
    } else {
        GdkRGBA color;
        color = *gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(colorButton));
        cfg->color = Util::GdkRGBA_to_argb(color);
    }

    cfg->drawingType = static_cast<DrawingType>(xoj::util::dropdown::getActive(GTK_DROP_DOWN(this->cbDrawingType)));

    int eraserMode = xoj::util::dropdown::getActive(GTK_DROP_DOWN(this->cbEraserType));

    if (eraserMode == 1) {
        cfg->eraserMode = ERASER_TYPE_DEFAULT;
    } else if (eraserMode == 2) {
        cfg->eraserMode = ERASER_TYPE_WHITEOUT;
    } else if (eraserMode == 3) {
        cfg->eraserMode = ERASER_TYPE_DELETE_STROKE;
    } else {
        cfg->eraserMode = ERASER_TYPE_NONE;
    }

    cfg->strokeType = static_cast<StrokeType>(xoj::util::dropdown::getActive(GTK_DROP_DOWN(this->cbStrokeType)));

    if (this->withDevice) {
        size_t dev = static_cast<size_t>(xoj::util::dropdown::getActive(GTK_DROP_DOWN(cbDevice)) - 1);
        cfg->device = (dev < 0 || this->deviceList.size() <= dev) ? "" : this->deviceList[dev].getName();
        cfg->disableDrawing = gtk_check_button_get_active(GTK_CHECK_BUTTON(cbDisableDrawing));
    }

    settings->customSettingsChanged();
}

void ButtonConfigGui::cbSelectCallback(GtkDropDown*, gpointer gui) {
    static_cast<ButtonConfigGui*>(gui)->enableDisableTools();
}

void ButtonConfigGui::cbColorCallback(GtkDropDown*, gpointer gui) {
    static_cast<ButtonConfigGui*>(gui)->enableDisableTools();
}

void ButtonConfigGui::enableDisableTools() {
    int toolIndex = xoj::util::dropdown::getActive(GTK_DROP_DOWN(cbTool));
    if (toolIndex < 0 || static_cast<size_t>(toolIndex) >= toolEntries.size()) {
        return;
    }
    auto action = toolEntries[static_cast<size_t>(toolIndex)].second;

    gtk_widget_set_visible(cbThickness, action != TOOL_NONE && xoj::tool::hasCapability(action, TOOL_CAP_SIZE));
    bool hasColor = action != TOOL_NONE && xoj::tool::hasCapability(action, TOOL_CAP_COLOR);
    gtk_widget_set_visible(cbColor, hasColor);
    gtk_widget_set_visible(colorButton, hasColor);
    gtk_widget_set_sensitive(colorButton, xoj::util::dropdown::getActive(GTK_DROP_DOWN(cbColor)) == 1);
    gtk_widget_set_visible(cbDrawingType, action != TOOL_NONE && xoj::tool::hasCapability(action, TOOL_CAP_RECTANGLE));
    gtk_widget_set_visible(cbEraserType, action == TOOL_ERASER);
    gtk_widget_set_visible(cbStrokeType, action == TOOL_PEN);
}
