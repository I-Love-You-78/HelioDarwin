#!/bin/bash
set -e
grep -qw hfsplus /proc/filesystems || insmod /home/captain/ios-x676c/hfsplus.ko
mnt=$(mktemp -d)
fsck.hfsplus -fy /home/captain/ios-x676c/ios16-root.img || true
mount -t hfsplus -o loop,force /home/captain/ios-x676c/ios16-root.img "$mnt"
python3 /home/captain/ios-x676c/scripts/sweet-root-tweaks.py "$mnt"
sync
umount "$mnt"
rmdir "$mnt"
echo APPLY_TWEAKS_SUCCESS
