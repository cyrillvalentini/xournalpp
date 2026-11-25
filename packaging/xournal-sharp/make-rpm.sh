#!/bin/bash
# Builds the Xournal# RPM from the committed state of this repository (uncommitted changes are not included).
# Needs rpm-build:  sudo dnf install rpm-build
# Result: ~/rpmbuild/RPMS/x86_64/xournal-sharp-*.rpm
set -euo pipefail
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
spec=packaging/xournal-sharp/xournal-sharp.spec
version=$(rpmspec -q --qf '%{version}\n' "$spec" | head -1)
topdir=$(rpm --eval '%{_topdir}')
mkdir -p "$topdir"/{SOURCES,SPECS,BUILD,RPMS,SRPMS}
git archive --format=tar.gz --prefix="xournal-sharp-$version/" -o "$topdir/SOURCES/xournal-sharp-$version.tar.gz" HEAD
rpmbuild -ba "$spec"
