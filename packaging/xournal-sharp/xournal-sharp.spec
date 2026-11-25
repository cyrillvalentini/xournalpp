# Xournal#: Xournal++ with a GNOME (libadwaita) look, from https://github.com/cyrillvalentini/xournalpp
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
Version:        1.3.8
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
BuildRequires:  pkgconfig(gtk+-3.0)
BuildRequires:  pkgconfig(poppler-glib)
BuildRequires:  pkgconfig(libxml-2.0)
BuildRequires:  pkgconfig(libzip)
BuildRequires:  pkgconfig(librsvg-2.0)
BuildRequires:  pkgconfig(portaudiocpp)
BuildRequires:  pkgconfig(sndfile)
BuildRequires:  pkgconfig(gtksourceview-4)
BuildRequires:  pkgconfig(zlib)
BuildRequires:  lua-devel
BuildRequires:  qpdf-devel
BuildRequires:  libXi-devel
BuildRequires:  libXext-devel

%description
Xournal# is a build of Xournal++ with a refreshed GNOME look: libadwaita colours
and rounded widgets, symbolic icons, the desktop's accent colour and automatic
light/dark switching. It installs alongside the regular Xournal++ and keeps its
own settings.

%prep
%autosetup -n %{name}-%{version}

%build
cmake -S . -B redhat-build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=%{optdir} \
    -DXOURNALPP_APP_ID=%{app_id} \
    -DXOURNALPP_CONFIG_FOLDER=xournal-sharp
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
* Wed Nov 25 2026 Cyrill Valentini - 1.3.8-2.modernui
- Own app icon: the Xournal++ cursive x on a ruled page with a handwritten "#", plus a symbolic variant

* Wed Nov 25 2026 Cyrill Valentini - 1.3.8-1.modernui
- GNOME (libadwaita) look, symbolic icons, accent colour and dark mode following the desktop
