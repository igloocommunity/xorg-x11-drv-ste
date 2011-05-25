#!/bin/sh

export CROSS_COMPILE=${CROSS_COMPILE:=}

make distclean

export CPPFLAGS="-I/usr/src/linux-headers-`uname -r`/include -I/usr/include/libdrm"
./configure --prefix=/usr

make
