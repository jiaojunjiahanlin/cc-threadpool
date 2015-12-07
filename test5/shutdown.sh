#!/usr/bin/env bash

umount /dev/mapper/pcache
dmsetup remove pcache
rmmod dm-cache
