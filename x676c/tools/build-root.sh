#!/bin/bash
# Build the iOS root image for the X676C in WSL, as root:
#
#   sudo bash build-root.sh [SIZE]        (SIZE default 16G)
#
# topaz's mkiosroot.sh and sweet-root-tweaks.py must sit next to this script
# (copy all three into WSL with LF line endings: a Windows checkout may have
# CRLF, which bash rejects).
# Inputs in ~/ios-x676c (of the user running sudo): ipsw/{OS,SystemOS,AppOS}.dmg
# from the iPhone X 16.7.16 IPSW (tools/fetch-ipsw.py), hfsplus.ko built for
# the running WSL kernel (WSL ships without HFS+), apfs-fuse in ~/.local/bin.
# Output: ~/ios-x676c/ios16-root.img, a journaled case-sensitive HFS+ volume
# made by topaz's tools/mkiosroot.sh plus tools/sweet-root-tweaks.py.
set -euo pipefail

[[ $EUID == 0 ]] || { echo "run with sudo" >&2; exit 1; }
user=${SUDO_USER:?run with sudo from your user}
home=$(getent passwd "$user" | cut -d: -f6)
work=$home/ios-x676c
tools=$(cd "$(dirname "$0")" && pwd)
size=${1:-16G}
img=$work/ios16-root.img

for f in ipsw/OS.dmg ipsw/SystemOS.dmg ipsw/AppOS.dmg hfsplus.ko; do
	[[ -f $work/$f ]] || { echo "missing $work/$f" >&2; exit 1; }
done
[[ ! -e $img ]] || { echo "$img exists: move it away first" >&2; exit 1; }

grep -qw hfsplus /proc/filesystems || insmod "$work/hfsplus.ko"
grep -qw hfsplus /proc/filesystems || { echo "hfsplus did not load" >&2; exit 1; }
# mkiosroot.sh runs apfs-fuse through sudo, whose PATH has no ~/.local/bin
install -m 0755 "$home/.local/bin/apfs-fuse" /usr/local/bin/apfs-fuse

bash "$tools/mkiosroot.sh" "$work/ipsw/OS.dmg" "$work/ipsw/SystemOS.dmg" \
	"$work/ipsw/AppOS.dmg" "$img" "$size"

mnt=$(mktemp -d)
mount -t hfsplus -o loop,force "$img" "$mnt"
trap 'umount "$mnt" 2>/dev/null || true; rmdir "$mnt"' EXIT
python3 "$tools/sweet-root-tweaks.py" "$mnt"
df -h "$mnt"
umount "$mnt"
trap - EXIT
rmdir "$mnt"
fsck.hfsplus -f -n "$img"
chown "$user:" "$img"
echo "OK $img"
