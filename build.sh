#!/bin/sh
CROSS_COMPILE=""

make distclean

export CPPFLAGS="-I/usr/src/linux-headers-2.6.38-1000/include -I/usr/include/libdrm"
./configure

make
