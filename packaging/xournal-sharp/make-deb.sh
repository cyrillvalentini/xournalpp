#!/bin/bash
# Builds the Xournal# .deb in a Debian 13 container from the committed state of this repository
# (uncommitted changes are not included; XOURNAL_SHARP_REF=<commit> builds another one), like make-rpm.sh.
# Needs podman. The first run builds the container image (downloads Debian and the build dependencies).
# Result: ~/debbuild/xournal-sharp_*.deb
set -euo pipefail
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
pkgdir=packaging/xournal-sharp
image=localhost/xournal-sharp-deb-builder:trixie
outdir="${XOURNAL_SHARP_DEB_OUTDIR:-$HOME/debbuild}"

# (Re)build the image; cached as long as debian/control does not change
podman build -t "$image" -f "$pkgdir/Containerfile.deb" "$pkgdir"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
git archive --format=tar --prefix=xournal-sharp/ "${XOURNAL_SHARP_REF:-HEAD}" | tar -x -C "$work"
# The source tree's debian/ belongs to the regular Xournal++ package: use the Xournal# one
rm -rf "$work/xournal-sharp/debian"
cp -r "$work/xournal-sharp/$pkgdir/debian" "$work/xournal-sharp/debian"

podman run --rm --userns=keep-id -v "$work:/build:Z" -w /build/xournal-sharp "$image" \
    dpkg-buildpackage -b -us -uc

mkdir -p "$outdir"
cp "$work"/*.deb "$outdir"/
ls -l "$outdir"/*.deb
