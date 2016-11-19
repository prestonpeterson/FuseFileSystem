#!/bin/bash
###
# Name: Preston Peterson
# Lab/task: Project 1 Task 5
# Date: 11/18/16
###
set -x
fusermount -u dir
rmdir dir
mkdir dir
gcc -Wall t5.c log.c `pkg-config fuse --cflags --libs` -o t5
./t5 dir dir
