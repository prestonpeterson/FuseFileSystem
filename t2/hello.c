/**
* Name: Preston Peterson
* Lab/task: Project 1 Task 2
* Date: 11/3/16
**/

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>

static const char *hello_str = "Hello World!\n";
static const char *hello_path = "/first_file";
static const char *goodbye_str = "Goodbye\n";
static const char *goodbye_path = "/second_file";
static const char *unreadable_path = "/third_file";
static char *random_str = "";
static const char *random_path = "/fourth_file";

static int hello_getattr(const char *path, struct stat *stbuf)
{
  printf("GETATTR\n");
 int res = 0;
 memset(stbuf, 0, sizeof(struct stat));
 if(strcmp(path, "/") == 0) {
  stbuf->st_mode = S_IFDIR | 0755;
  stbuf->st_nlink = 2;
 }
 else if(strcmp(path, hello_path) == 0) {
  stbuf->st_mode = S_IFREG | 0444;
  stbuf->st_nlink = 1;
  stbuf->st_size = strlen(hello_str);
 }
 else if(strcmp(path, goodbye_path) == 0) {
  stbuf->st_mode = S_IFREG | 0444;
  stbuf->st_nlink = 1;
  stbuf->st_size = strlen(goodbye_str);
 }
 else if(strcmp(path, unreadable_path) == 0) {
  stbuf->st_mode = S_IFREG | 0000;
  stbuf->st_nlink = 1;
  stbuf->st_size = 0;
 }
 else if (strcmp(path, random_path) == 0) {
  stbuf->st_mode = S_IFREG | 0444;
  stbuf->st_nlink = 1;
  stbuf->st_size = 128 * sizeof(char);
 }
 else
  res = -ENOENT;

 return res;
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
off_t offset, struct fuse_file_info *fi)
{
  printf("READDIR\n");
 (void) offset;
 (void) fi;

 if(strcmp(path, "/") != 0)
 return -ENOENT;

 filler(buf, ".", NULL, 0);
 filler(buf, "..", NULL, 0);
 filler(buf, hello_path + 1, NULL, 0);
 filler(buf, goodbye_path + 1, NULL, 0);
 filler(buf, unreadable_path + 1, NULL, 0);
 filler(buf, random_path + 1, NULL, 0);

 return 0;
}

static int hello_open(const char *path, struct fuse_file_info *fi)
{
  printf("OPEN\n");
 if (strcmp(path, hello_path) != 0 && strcmp(path, goodbye_path) != 0 && strcmp(path, random_path) != 0)
  return -ENOENT;

 if((fi->flags & 3) != O_RDONLY)
  return -EACCES;

 return 0;
}

static int hello_read(const char *path, char *buf, size_t size, off_t offset,
struct fuse_file_info *fi)
{
  printf("READ\n");
 size_t len;
 (void) fi;
 if(strcmp(path, hello_path) == 0) {
   len = strlen(hello_str);
   if (offset < len) {
    if (offset + size > len)
     size = len - offset;
    memcpy(buf, hello_str + offset, size);
   }
   else
    size = 0;
 }
 else if(strcmp(path, goodbye_path) == 0) {
  len = strlen(goodbye_str);
  if (offset < len) {
    if (offset + size > len)
      size = len - offset;
    memcpy(buf, goodbye_str + offset, size);
  }
  else
    size = 0;
 }
 else if(strcmp(path, random_path) == 0) {
  len = rand() % 128;
  random_str = malloc(len * sizeof(char));
  char random_char;
  int i;
  for (i = 0; i < len-1; i++) {
    if (rand() % 10 == 0)
      random_char = ' ';
    else
      random_char = 65 + (rand() % 58);
    random_str[i] = random_char;
  }
  random_str[len-2] = '\0';
  random_str[len-1] = '\n';
  if (offset < len) {
    if (offset + size > len)
      size = len - offset;
    memcpy(buf, random_str + offset, size);
  }
  else
    size = 0;
 }
 printf("end of read\n");
 return size;
}

static struct fuse_operations hello_oper = {
 .getattr = hello_getattr,
 .readdir = hello_readdir,
 .open = hello_open,
 .read = hello_read,
};

int main(int argc, char *argv[])
{
 srand(time(NULL));
 printf("MAIN\n");
 return fuse_main(argc, argv, &hello_oper, NULL);
}
