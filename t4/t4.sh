#!/bin/bash
set -x
fusermount -u dir
rmdir dir
mkdir dir
gcc -Wall t4.c log.c `pkg-config fuse --cflags --libs` -o t4
./t4 -d dir dir
