#!/bin/bash
# Usage: sudo apply_tweaks.sh ROOT_IMAGE [sweet-root-tweaks.py options...]
# Set HFSPLUS_MODULE explicitly if the host needs an out-of-tree HFS+ module.
set -euo pipefail
if [ "$#" -lt 1 ]; then
    echo "Usage: $0 ROOT_IMAGE [root-tweak options...]" >&2
    exit 2
fi
image=$(realpath -- "$1")
shift
[ -f "$image" ] || { echo "Not a root image: $image" >&2; exit 1; }
[ "$(id -u)" = 0 ] || { echo "Run this helper as root" >&2; exit 1; }
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if ! grep -qw hfsplus /proc/filesystems; then
    if [ -n "${HFSPLUS_MODULE:-}" ]; then
        insmod "$HFSPLUS_MODULE"
    else
        modprobe hfsplus
    fi
fi
if [ -n "$(losetup -j "$image")" ]; then
    echo "Root image is already attached to a loop device: $image" >&2
    exit 1
fi
fsck.hfsplus -fy "$image"
mnt=$(mktemp -d)
mounted=0
cleanup() {
    status=$?
    trap - EXIT
    if [ "$mounted" = 1 ]; then
        if ! umount "$mnt"; then
            echo "Could not unmount $mnt; the mount has been retained" >&2
            exit 1
        fi
    fi
    rmdir "$mnt" || status=1
    exit "$status"
}
trap cleanup EXIT
mount -t hfsplus -o loop,force "$image" "$mnt"
mounted=1
python3 "$script_dir/../../tools/sweet-root-tweaks.py" "$mnt" "$@"
sync
umount "$mnt"
mounted=0
echo APPLY_TWEAKS_SUCCESS
