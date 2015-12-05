#!/usr/bin/env bash

umount /dev/mapper/cache
dmsetup remove cache
rmmod dm-cache
