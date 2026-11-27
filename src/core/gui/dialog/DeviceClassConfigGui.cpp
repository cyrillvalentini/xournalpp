#include "DeviceClassConfigGui.h"

#include <string>  // for allocator, char_traits

#include <glib.h>  // for g_ascii_strtoll, g_strdu...

#include "control/DeviceListHelper.h"        // for InputDevice
#include "control/settings/Settings.h"       // for Settings
#include "control/settings/SettingsEnums.h"  // for InputDeviceTypeOption
#include "gui/Builder.h"                     // for Builder
#include "util/Assert.h"                     // for xoj_assert
#include "util/DropDown.h"

class GladeSearchpath;

constexpr auto UI_FILE = "settingsDeviceClassConfig.ui";
constexpr auto UI_WIDGET_NAME = "deviceClassBox";

DeviceClassConfigGui::DeviceClassConfigGui(GladeSearchpath* gladeSearchPath, GtkBox* box, Settings* settings,
                                           bool showApplyBtn):
        settings(settings) {
    Builder builder(gladeSearchPath, UI_FILE);
    gtk_box_append(box, builder.get(UI_WIDGET_NAME));  // box takes ownership of it all!

    this->labelDevice = builder.get("labelDevice");
    this->cbDeviceClass = builder.get("cbDeviceClass");
    // The InputDeviceTypeOption of each entry (same order as in settingsDeviceClassConfig.ui)
    xoj::util::dropdown::setIds(GTK_DROP_DOWN(this->cbDeviceClass), {"0", "1", "2", "3", "4"});
    auto* applyBtn = builder.get("btnApply");

    gtk_widget_set_visible(applyBtn, showApplyBtn);
    if (showApplyBtn) {
        g_signal_connect(applyBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) {
                             static_cast<DeviceClassConfigGui*>(d)->saveSettings();
                         }),
                         this);
    }
}

DeviceClassConfigGui::~DeviceClassConfigGui() = default;

void DeviceClassConfigGui::setDevice(const InputDevice& d) {
    this->device = d;
    gtk_label_set_text(GTK_LABEL(this->labelDevice), (device.getName() + " (" + device.getType() + ")").c_str());

    loadSettings();
}

void DeviceClassConfigGui::loadSettings() {
    // Get device class of device if available or
    InputDeviceTypeOption deviceType =
            this->settings->getDeviceClassForDevice(this->device.getName(), this->device.getSource());
    // Use the ID of each option in case the combo box options get rearranged in the future
    xoj::util::dropdown::setActiveId(GTK_DROP_DOWN(this->cbDeviceClass), std::to_string(static_cast<int>(deviceType)));
}

void DeviceClassConfigGui::saveSettings() {
    auto deviceClassId = xoj::util::dropdown::getActiveId(GTK_DROP_DOWN(this->cbDeviceClass));
    xoj_assert(deviceClassId.has_value());
    auto deviceClass = static_cast<InputDeviceTypeOption>(g_ascii_strtoll(deviceClassId->c_str(), nullptr, 10));
    this->settings->setDeviceClassForDevice(this->device.getName(), this->device.getSource(), deviceClass);
}
