#include "SystemAppearance.h"

#include <cmath>
#include <cstring>
#include <utility>

#include <glib.h>

namespace {
constexpr auto PORTAL_BUS_NAME = "org.freedesktop.portal.Desktop";
constexpr auto PORTAL_PATH = "/org/freedesktop/portal/desktop";
constexpr auto PORTAL_SETTINGS = "org.freedesktop.portal.Settings";
constexpr auto APPEARANCE_NS = "org.freedesktop.appearance";
constexpr auto KEY_ACCENT = "accent-color";
constexpr auto KEY_SCHEME = "color-scheme";
/// Keep startup snappy if the portal is slow or missing
constexpr int PORTAL_TIMEOUT_MS = 500;

/// Unwraps nested variants ("v" inside "v"), which older portal versions return from Read()
GVariant* unbox(GVariant* v) {
    GVariant* cur = g_variant_ref(v);
    while (g_variant_is_of_type(cur, G_VARIANT_TYPE_VARIANT)) {
        GVariant* inner = g_variant_get_variant(cur);
        g_variant_unref(cur);
        cur = inner;
    }
    return cur;
}
}  // namespace

SystemAppearance::SystemAppearance(std::function<void()> onChange): onChange(std::move(onChange)) {
    GError* err = nullptr;
    bus.reset(g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err), xoj::util::adopt);
    if (!bus) {
        g_debug("SystemAppearance: no session bus (%s)", err ? err->message : "unknown error");
        g_clear_error(&err);
        return;
    }
    read(KEY_ACCENT);
    read(KEY_SCHEME);
    subscription = g_dbus_connection_signal_subscribe(bus.get(), PORTAL_BUS_NAME, PORTAL_SETTINGS, "SettingChanged",
                                                      PORTAL_PATH, APPEARANCE_NS, G_DBUS_SIGNAL_FLAGS_NONE,
                                                      &SystemAppearance::onSettingChanged, this, nullptr);
}

SystemAppearance::~SystemAppearance() {
    if (bus && subscription) {
        g_dbus_connection_signal_unsubscribe(bus.get(), subscription);
    }
}

void SystemAppearance::read(const char* key) {
    GError* err = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(bus.get(), PORTAL_BUS_NAME, PORTAL_PATH, PORTAL_SETTINGS, "ReadOne",
                                                  g_variant_new("(ss)", APPEARANCE_NS, key), G_VARIANT_TYPE("(v)"),
                                                  G_DBUS_CALL_FLAGS_NONE, PORTAL_TIMEOUT_MS, nullptr, &err);
    if (!reply) {
        // Portal versions < 2 only have the deprecated Read(), which wraps the value in an extra variant
        g_clear_error(&err);
        reply = g_dbus_connection_call_sync(bus.get(), PORTAL_BUS_NAME, PORTAL_PATH, PORTAL_SETTINGS, "Read",
                                            g_variant_new("(ss)", APPEARANCE_NS, key), G_VARIANT_TYPE("(v)"),
                                            G_DBUS_CALL_FLAGS_NONE, PORTAL_TIMEOUT_MS, nullptr, &err);
    }
    if (!reply) {
        g_debug("SystemAppearance: cannot read %s from the desktop portal (%s)", key, err ? err->message : "?");
        g_clear_error(&err);
        return;
    }
    GVariant* boxed = nullptr;
    g_variant_get(reply, "(v)", &boxed);
    apply(key, boxed);
    g_variant_unref(boxed);
    g_variant_unref(reply);
}

void SystemAppearance::apply(const char* key, GVariant* raw) {
    GVariant* value = unbox(raw);
    if (std::strcmp(key, KEY_ACCENT) == 0 && g_variant_is_of_type(value, G_VARIANT_TYPE("(ddd)"))) {
        double r = 0, g = 0, b = 0;
        g_variant_get(value, "(ddd)", &r, &g, &b);
        // The portal signals "no accent colour" with values outside [0, 1]
        auto valid = [](double c) { return c >= 0.0 && c <= 1.0; };
        if (valid(r) && valid(g) && valid(b)) {
            auto to8 = [](double c) { return static_cast<uint8_t>(std::lround(c * 255.0)); };
            accent = Color(to8(r), to8(g), to8(b));
        } else {
            accent.reset();
        }
    } else if (std::strcmp(key, KEY_SCHEME) == 0 && g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32)) {
        preferDark = g_variant_get_uint32(value) == 1;  // 0: no preference, 1: dark, 2: light
    }
    g_variant_unref(value);
}

void SystemAppearance::onSettingChanged(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                                        GVariant* params, gpointer data) {
    auto* self = static_cast<SystemAppearance*>(data);
    const gchar* ns = nullptr;
    const gchar* key = nullptr;
    GVariant* value = nullptr;
    g_variant_get(params, "(&s&sv)", &ns, &key, &value);
    if (std::strcmp(ns, APPEARANCE_NS) == 0 && (std::strcmp(key, KEY_ACCENT) == 0 || std::strcmp(key, KEY_SCHEME) == 0)) {
        self->apply(key, value);
        if (self->onChange) {
            self->onChange();
        }
    }
    g_variant_unref(value);
}
