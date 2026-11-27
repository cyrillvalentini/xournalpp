#!/bin/bash
# Builds the Xournal# RPM from the committed state of this repository (uncommitted changes are not included;
# XOURNAL_SHARP_REF=<commit> builds another one).
# The branch decides the toolkit: xournal-sharp-gtk4 builds the GTK 4 / libadwaita version, modern-ui the GTK 3 one.
# Needs rpm-build:  sudo dnf install rpm-build
# Result: ~/rpmbuild/RPMS/x86_64/xournal-sharp-*.rpm
set -euo pipefail
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
spec=packaging/xournal-sharp/xournal-sharp.spec
version=$(rpmspec -q --qf '%{version}\n' "$spec" | head -1)
topdir=$(rpm --eval '%{_topdir}')
mkdir -p "$topdir"/{SOURCES,SPECS,BUILD,RPMS,SRPMS}
git archive --format=tar.gz --prefix="xournal-sharp-$version/" -o "$topdir/SOURCES/xournal-sharp-$version.tar.gz" "${XOURNAL_SHARP_REF:-HEAD}"
rpmbuild -ba "$spec"
