#!/bin/bash
###
# Name: Preston Peterson
# Lab/task: Project 1 Task 6
# Date: 11/18/16
###
set -x
fusermount -u dir
rmdir dir
mkdir dir
gcc -Wall t6.c log.c `pkg-config fuse --cflags --libs` -o t6
./t6 dir dir
