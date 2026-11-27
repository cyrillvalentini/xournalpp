# Xournal#: Xournal++ on GTK 4 and libadwaita, from https://github.com/cyrillvalentini/xournalpp
# (branch xournal-sharp-gtk4; the GTK 3 build lives on the branch modern-ui)
#
# Installs next to the regular xournalpp package without conflicts:
#  - the program lives in /opt/xournal-sharp, with /usr/bin/xournal-sharp as launcher
#  - its own application ID, so it never merges into a running Xournal++ instance
#  - its own settings folder (~/.config/xournal-sharp), first filled with a copy of the Xournal++ settings
#
# Build with packaging/xournal-sharp/make-rpm.sh

%global app_id io.github.cyrillvalentini.XournalSharp
%global optdir /opt/xournal-sharp
%global debug_package %{nil}

Name:           xournal-sharp
# "~gtk4" sorts after 1.3.8 (so it updates the GTK 3 Xournal#) and before a final 1.4.0
Version:        1.4.0~gtk4
Release:        2.modernui%{?dist}
Summary:        Xournal# - handwritten notes and PDF annotation with a GNOME look

License:        GPL-2.0-or-later
URL:            https://github.com/cyrillvalentini/xournalpp
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  gettext
BuildRequires:  help2man
BuildRequires:  desktop-file-utils
BuildRequires:  pkgconfig(gtk4) >= 4.14
BuildRequires:  pkgconfig(libadwaita-1) >= 1.6
BuildRequires:  pkgconfig(poppler-glib)
BuildRequires:  pkgconfig(libxml-2.0)
BuildRequires:  pkgconfig(libzip)
BuildRequires:  pkgconfig(librsvg-2.0)
BuildRequires:  pkgconfig(portaudiocpp)
BuildRequires:  pkgconfig(sndfile)
BuildRequires:  pkgconfig(gtksourceview-5)
BuildRequires:  pkgconfig(zlib)
BuildRequires:  lua-devel
BuildRequires:  qpdf-devel

%description
Xournal# is a build of Xournal++ on GTK 4 and libadwaita with a GNOME look:
Adwaita style, symbolic icons, the desktop's accent colour and automatic
light/dark switching, a sidebar like GNOME's document viewer and GNOME-style
dialogs. It installs alongside the regular Xournal++ and keeps its own settings.

%prep
%autosetup -n %{name}-%{version}

%build
cmake -S . -B redhat-build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=%{optdir} \
    -DXOURNALPP_APP_ID=%{app_id} \
    -DXOURNALPP_CONFIG_FOLDER=xournal-sharp \
    -DXOURNALPP_APP_NAME="Xournal#" \
    -DXOURNALPP_SOURCE_URL=%{url}
cmake --build redhat-build

%install
DESTDIR=%{buildroot} cmake --install redhat-build

# Desktop integration files from the build would only duplicate the regular package's; ship our own instead
rm -rf %{buildroot}%{optdir}/share/applications %{buildroot}%{optdir}/share/mime \
       %{buildroot}%{optdir}/share/metainfo %{buildroot}%{optdir}/share/thumbnailers
rm -f %{buildroot}%{optdir}/bin/xournalpp-thumbnailer

install -d %{buildroot}%{_bindir}
ln -s %{optdir}/bin/xournalpp %{buildroot}%{_bindir}/xournal-sharp

install -Dm644 packaging/xournal-sharp/%{app_id}.svg \
    %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/%{app_id}.svg
install -Dm644 packaging/xournal-sharp/%{app_id}-symbolic.svg \
    %{buildroot}%{_datadir}/icons/hicolor/symbolic/apps/%{app_id}-symbolic.svg
install -Dm644 packaging/xournal-sharp/%{app_id}.desktop \
    %{buildroot}%{_datadir}/applications/%{app_id}.desktop

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/%{app_id}.desktop

%files
%license LICENSE
%{optdir}
%{_bindir}/xournal-sharp
%{_datadir}/applications/%{app_id}.desktop
%{_datadir}/icons/hicolor/scalable/apps/%{app_id}.svg
%{_datadir}/icons/hicolor/symbolic/apps/%{app_id}-symbolic.svg

%changelog
* Fri Nov 27 2026 Cyrill Valentini - 1.4.0~gtk4-2.modernui
- Shown as "Xournal#" (window title, dialogs, About with own icon and repository; Xournal++ credits kept)

* Fri Nov 27 2026 Cyrill Valentini - 1.4.0~gtk4-1.modernui
- Port to GTK 4 and libadwaita (based on the Xournal++ GTK 4 port by bhennion)
- libadwaita window, header bar and alert dialogs; accent colour for the page border and selections
- Sidebar, zoom bar and colour palette in GNOME style; translations of the new strings

* Wed Nov 25 2026 Cyrill Valentini - 1.3.8-2.modernui
- Own app icon: the Xournal++ cursive x on a ruled page with a handwritten "#", plus a symbolic variant

* Wed Nov 25 2026 Cyrill Valentini - 1.3.8-1.modernui
- GNOME (libadwaita) look, symbolic icons, accent colour and dark mode following the desktop
