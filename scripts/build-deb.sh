#!/bin/bash
# Rebuilds opentermix_*.deb inside a fresh debootstrap chroot for the given
# target, so the resulting package links against exactly that target's own
# Qt6/qtermwidget. This matters because Qt embeds a version-tagged symbol
# (qt_version_tag) in every binary: a package built against a newer Qt6 than
# the target has installed will fail to even start (see the "Runtime
# dependencies" section in README.md).
#
# Supported targets - anything without a native Qt6 build of qtermwidget is
# out of scope (Ubuntu < 25.10 only ships a Qt5 build; see README.md):
#   debian trixie    (and any newer Debian codename: forky, sid, ...)
#   ubuntu resolute   (26.04 - and questing/stonking if you need them too)
#
# Usage:
#   bash scripts/build-deb.sh debian trixie
#   bash scripts/build-deb.sh ubuntu resolute
#
# Run this yourself in a normal terminal (not through an AI assistant's
# non-interactive shell) - it needs sudo interactively for debootstrap/
# mount/chroot, and password prompts only work with a real tty attached.

set -euo pipefail

DISTRO="${1:?usage: $0 <debian|ubuntu> <suite>}"
SUITE="${2:?usage: $0 <debian|ubuntu> <suite>}"

case "$DISTRO" in
    debian)
        MIRROR="http://deb.debian.org/debian"
        DEBOOTSTRAP_EXTRA=()
        ;;
    ubuntu)
        MIRROR="http://archive.ubuntu.com/ubuntu"
        # Most of our build deps (qt6-base-dev, qt6-tools-dev, ...) live in
        # universe on Ubuntu, not main.
        DEBOOTSTRAP_EXTRA=(--components=main,universe)
        ;;
    *)
        echo "Unknown distro '$DISTRO' - expected 'debian' or 'ubuntu'" >&2
        exit 1
        ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
CHROOT="/var/tmp/opentermix-${DISTRO}-${SUITE}-chroot"
OUT="$REPO"

if [ ! -x /usr/sbin/debootstrap ]; then
    echo "debootstrap not found at /usr/sbin/debootstrap (apt install debootstrap)" >&2
    exit 1
fi

if [ ! -d "$CHROOT/etc" ]; then
    echo "==> Bootstrapping $DISTRO $SUITE chroot at $CHROOT (this takes a few minutes)..."
    sudo /usr/sbin/debootstrap "${DEBOOTSTRAP_EXTRA[@]}" "$SUITE" "$CHROOT" "$MIRROR"
else
    echo "==> Reusing existing chroot at $CHROOT"
fi

echo "==> Mounting /proc /sys /dev into chroot"
sudo mount --bind /proc "$CHROOT/proc"
sudo mount --bind /sys "$CHROOT/sys"
sudo mount --bind /dev "$CHROOT/dev"
sudo mount --bind /dev/pts "$CHROOT/dev/pts"

cleanup() {
    echo "==> Unmounting chroot bind mounts"
    sudo umount -l "$CHROOT/dev/pts" 2>/dev/null || true
    sudo umount -l "$CHROOT/dev" 2>/dev/null || true
    sudo umount -l "$CHROOT/sys" 2>/dev/null || true
    sudo umount -l "$CHROOT/proc" 2>/dev/null || true
}
trap cleanup EXIT

echo "==> Syncing repo into chroot"
sudo mkdir -p "$CHROOT/root/opentermix"
sudo rsync -a --delete \
    --exclude=build \
    --exclude=.git \
    --exclude='*.deb' \
    "$REPO/" "$CHROOT/root/opentermix/"

echo "==> Installing build deps (from debian/control) and building inside chroot"
sudo chroot "$CHROOT" /bin/bash -c '
    set -e
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    # Resolve straight from debian/control (including the
    # libqtermwidget-dev|libqtermwidget6-2-dev alternative) instead of a
    # hand-maintained package list, so this never drifts out of sync with
    # debian/control again.
    apt-get build-dep -y /root/opentermix
    cd /root/opentermix
    dpkg-buildpackage -us -uc -b
'

echo "==> Copying built package(s) back out"
sudo find "$CHROOT/root" -maxdepth 1 \( -iname 'opentermix_*.deb' -o -iname 'opentermix_*.buildinfo' -o -iname 'opentermix_*.changes' \) \
    -exec cp -v {} "$OUT/" \;
sudo chown "$(id -u):$(id -g)" "$OUT"/opentermix_*.deb "$OUT"/opentermix_*.buildinfo "$OUT"/opentermix_*.changes 2>/dev/null || true

echo "==> Done. Built package(s):"
ls -la "$OUT"/opentermix_*.deb
