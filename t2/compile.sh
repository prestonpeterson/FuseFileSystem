#!/bin/sh
set -x #echo on
sudo umount myfs
gcc -Wall hello.c `pkg-config fuse --cflags --libs` -o hello
./hello myfs
ls -l myfs
cat myfs/*