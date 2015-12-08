#!/usr/bin/env bash

umount /dev/mapper/pcache
dmsetup remove -f  pcache
rmmod dm-cache
