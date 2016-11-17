/**
* Name: Preston Peterson
* Lab/task: Project 1 Task 4
* Date: 11/15/16
**/

#include <fuse.h>
#define BLOCK_SIZE 256
#define MAX_NAME_LENGTH 128
#define DATA_SIZE 254
#define INDEX_SIZE 127

typedef char data_t;
typedef unsigned short index_t;

typedef enum
{
   DIR,
   FIL,
   INDEX,
   DATA
} NODE_TYPE;

typedef struct fs_node
{
   char name[MAX_NAME_LENGTH];
   time_t creat_t; // creation time
   time_t access_t; // last access
   time_t mod_t; // last modification
   mode_t access; // access rights for the file
   unsigned short owner; // owner ID
   unsigned short size;
   index_t block_ref; // reference to the data or index block
} FS_NODE;

typedef struct node
{
   NODE_TYPE type;
   union
   {
      FS_NODE fd;
      data_t data[DATA_SIZE];
      index_t index[INDEX_SIZE];
   } content;
} NODE;


// global table
typedef struct open_file_global_type // elements of the hash table (in-memory "directory")
{
   unsigned short fd; // reference to the file descriptor node
   unsigned short data; // reference to the data or index node (depending on the size)
   mode_t access; // access rights for the file
   unsigned short size; // size of the file
   unsigned short reference_count; // reference count
   struct open_file_global_type *next;
} OPEN_FILE_GLOBAL_TYPE;

#define GLOBAL_TABLE_SIZE 65521 // prime number for hashing


// local table

typedef struct open_file_local_type // a node for a local list of open files (per process)
{
   mode_t access_rights; // access rights for this process
   unsigned short global_ref; // reference to the entry for the file in the global table
} OPEN_FILE_LOCAL_TYPE;

#define MAX_OPEN_FILES_PER_PROCESS 16
