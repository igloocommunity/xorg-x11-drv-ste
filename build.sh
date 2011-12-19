#!/bin/sh

export CROSS_COMPILE=${CROSS_COMPILE:=}

make distclean

headers=$(ls -d -1 /usr/src/linux-headers-*-ux500 | tail -1)

export CPPFLAGS="-I${headers}/include -I/usr/include/libdrm"
./configure --prefix=/usr

make
