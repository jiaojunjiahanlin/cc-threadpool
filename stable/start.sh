#!/usr/bin/env bash

modprobe dm-mod
insmod dm-cache.ko
echo 0 62914560 cache /dev/vdc /dev/vdb 0 32 65536 256 1| dmsetup create pcache
dmsetup status
mkfs.ext4 /dev/mapper/pcache
mount /dev/mapper/pcache /mnt/dmcache/
cd /mnt/dmcache/


dd if=/dev/zero of=2G.file bs=300M count=1

dmsetup status
