#!/usr/bin/env bash

modprobe dm-mod
insmod dm-cache.ko
echo 0 62914560 cache /dev/vdc /dev/vdb | dmsetup create pcache
dmsetup status
mkfs.ext4 /dev/mapper/pcache

