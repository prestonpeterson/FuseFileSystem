#!/bin/bash
###
# Name: Preston Peterson
# Lab/task: Project 1 Task 4
# Date: 11/15/16
###
set -x
fusermount -u dir
rmdir dir
mkdir dir
gcc -Wall t4.c log.c `pkg-config fuse --cflags --libs` -o t4
./t4 dir dir
