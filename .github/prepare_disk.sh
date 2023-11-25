#!/bin/bash

set -eu

KERNEL=6.2.0-31-generic
DISK=$(mktemp -d)

cp networkfs.ko $DISK/
cp networkfs_test $DISK/

export SUPERMIN_KERNEL_VERSION=$KERNEL
export SUPERMIN_KERNEL=/opt/vmlinuz-$KERNEL
export SUPERMIN_MODULES=/opt/kernel-modules/lib/modules/$KERNEL
virt-make-fs --format=raw --type=ext4 $DISK networkfs.img --size=32M

rm -rf $DISK
