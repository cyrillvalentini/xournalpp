/*
 * Xournal++
 *
 * Reads the desktop's appearance preferences (accent colour, light/dark preference) from the
 * XDG desktop portal (org.freedesktop.portal.Settings, namespace "org.freedesktop.appearance")
 * and reports changes. Works on GNOME (47+), KDE and any other desktop implementing the portal.
 * Without a portal (Windows, macOS, minimal Linux setups) nothing is reported.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <optional>

#include <gio/gio.h>

#include "util/Color.h"
#include "util/raii/GObjectSPtr.h"

class SystemAppearance final {
public:
    /// @param onChange called (on the main thread) whenever the accent colour or colour scheme changes
    explicit SystemAppearance(std::function<void()> onChange);
    ~SystemAppearance();

    SystemAppearance(const SystemAppearance&) = delete;
    SystemAppearance& operator=(const SystemAppearance&) = delete;

    /// The user's accent colour, if the desktop provides one
    std::optional<Color> getAccentColor() const { return accent; }

    /// True if the desktop asks applications to use a dark style
    bool prefersDark() const { return preferDark; }

private:
    void read(const char* key);
    void apply(const char* key, GVariant* value);
    static void onSettingChanged(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                                 GVariant* params, gpointer self);

    xoj::util::GObjectSPtr<GDBusConnection> bus;
    guint subscription = 0;
    std::function<void()> onChange;

    std::optional<Color> accent;
    bool preferDark = false;
};
