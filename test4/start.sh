#!/usr/bin/env bash

modprobe dm-mod
insmod dm-cache.ko
echo 0 62914560 cache /dev/vdc /dev/vdb | dmsetup create pcache
dmsetup status
mount /dev/mapper/pcache /mnt/dmcache/
cd /mnt/dmcache/


dd if=/dev/zero of=2G.file bs=1G count=2

dmsetup status
