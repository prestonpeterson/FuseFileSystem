/**
* Name: Preston Peterson
* Lab/task: Project 1 Task 4
* Date: 11/15/16
**/
#define FUSE_USE_VERSION 26
#include <fuse.h>
#include "t4.h"
#include "params.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>

static NODE *memory;
static const int num_of_blocks = 256; // 2^8. TODO: Increase this value
static unsigned char *bitvector;
static int bitvector_len = 0;
static int working_dir = 1; // block number of the index node of the working directory. initialized to 1 to indicate the root directory
OPEN_FILE_GLOBAL_TYPE global_table[GLOBAL_TABLE_SIZE] = {{0}};
OPEN_FILE_LOCAL_TYPE local_table[MAX_OPEN_FILES_PER_PROCESS] = {{0}};

// fs_fullpath taken from http://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/src/bbfs.c
static void fs_fullpath(char fpath[PATH_MAX], const char *path)
{
    strcpy(fpath, FS_DATA->rootdir);
    strncat(fpath, path, PATH_MAX); // ridiculously long paths will
				    // break here
    log_msg("    fs_fullpath:  rootdir = \"%s\", path = \"%s\", fpath = \"%s\"\n",
	    FS_DATA->rootdir, path, fpath);
}

// perfect hash function that will never have collisions
static unsigned int getslot(int key)
 {
   return (unsigned)key % GLOBAL_TABLE_SIZE;
 }

static void *fs_init(struct fuse_conn_info *conn) {
	log_msg("test %s\n", "init");
	// FILE* fp = fopen("dir", "w+");
	// fclose(fp);

	bitvector_len = num_of_blocks / 8;
	printf("Initializing memory and superblock...\n");
	size_t bitvector_size = bitvector_len * sizeof(char);
	bitvector = malloc(bitvector_size);
	memset(bitvector, 0x00, bitvector_size);

	memory = malloc(num_of_blocks * sizeof(NODE));
	FS_NODE fd;
	strcpy(fd.name, "/");
	fd.creat_t = 0;
	fd.access_t = 0;
	fd.mod_t = 0;
	fd.access = S_IFDIR | 0755;
	fd.owner = fuse_get_context()->uid;
	fd.size = 0;
	fd.block_ref = 1;

	NODE superblock;
	superblock.type = DIR;
	superblock.content.fd = fd;
	memory[0] = superblock;
	bitvector[0] = bitvector[0] | 0x80;

	memory[1].type = INDEX;
	memset(memory[1].content.index, 0, INDEX_SIZE-1);
	memory[1].content.index[0] = 1;
	bitvector[0] = bitvector[0] | 0x40;
	log_conn(conn);
	uid_t uid = fuse_get_context()->uid;
	log_msg("current uid = %d\n", uid);

    log_fuse_context(fuse_get_context());
	printf("Initialization complete\n");
	return FS_DATA;
}

static int find_zero_bit(int start_index, int *offset, int *mask) {
	int i;
	*mask = 0x80;
	*offset = 0;
	if (start_index >= bitvector_len)
		start_index = 0;
	for (i = start_index; i < bitvector_len; i++) {
		if ((int)bitvector[i] != 0xFF) {
			// printf("bitvector[%d] = %d\n", i, bitvector[i]);
			while (*mask != 0x00) {
				if ((bitvector[i] & *mask) == 0x00) {
					return i;
				}
				*mask = (*mask) / 2;
				(*offset)++;
			}
		}
	}
	return -1;
}

static int convert_index_to_block_number(int bitvector_index, int index_offset) {
	if (bitvector_index >= bitvector_len)
		return -1; // blockNumber out of bounds
	else
		return (8 * bitvector_index) + index_offset;
}

static int convert_block_number_to_index(int blockNumber, int *mask) {
	int bitvector_index = blockNumber / 8;
	int bitvector_offset = blockNumber % 8;
	*mask = 0x80;
	int i;
	for (i = 0; i < bitvector_offset; i++) {
		*mask /= 2;
	}
	return bitvector_index;
}

static int flip_bit(int byte, int mask) {
	return byte ^ mask;
}

static int fs_mknod(const char *name, mode_t mode, dev_t dev) {
	log_msg("test %s\n", "create_file");
	int mask = 0x00;
	int offset = 0;
	int index = find_zero_bit(0, &offset, &mask);
	int file_offset = offset;
	if (index == -1) {
		// can't create file. no available bits
		log_msg("Can't create file. No blocks available\n");
		return -ENOSPC;
	}

	bitvector[index] = flip_bit(bitvector[index], mask);
	int file_blockNumber = (8 * index) + offset;

	int i;
	int found = 0;
	printf("working_dir = %d\n", working_dir);
	for (i = 1; i < INDEX_SIZE && !found; i++) {
		if (memory[working_dir].content.index[i] == 0) {
			found = 1;
			memory[working_dir].content.index[i] = file_blockNumber;
		}
	}
	if (!found) {
		printf("Can't create file. No blocks available in current directory\n");
		return -ENOSPC;
	}
	FS_DATA->block_num = file_blockNumber;

	memory[file_blockNumber].type = FIL;
	strcpy(memory[file_blockNumber].content.fd.name, name);
	memory[file_blockNumber].content.fd.creat_t = time(NULL);
	memory[file_blockNumber].content.fd.access_t = time(NULL);
	memory[file_blockNumber].content.fd.mod_t = time(NULL);
	mode = S_IFREG | 0777;
	memory[file_blockNumber].content.fd.access = mode;
	memory[file_blockNumber].content.fd.owner = fuse_get_context()->uid;

	// if (mode <= 0) {
	// 	// default access is 744 or rwxr--r--
	// 	memory[file_blockNumber].content.fd.access = S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH;
	// }
	// else {
	// 	memory[file_blockNumber].content.fd.access = access;
	// }
	memory[file_blockNumber].content.fd.size = 0;

	// find available block and create data node for the file
	mask = 0x00;
	int data_index = find_zero_bit(index, &offset, &mask);
	int data_blockNumber = convert_index_to_block_number(data_index, offset);
	// if (data_blockNumber == -1) {
	// 	printf("CREATE_FILE FUNCTION. Error: data node block %d out of bounds.\n", data_blockNumber);
	// 	return -1;
	// }
	bitvector[data_index] = flip_bit(bitvector[data_index], mask);
	memory[data_blockNumber].type = DATA;

	// tell the file where its data node is
	memory[file_blockNumber].content.fd.block_ref = data_blockNumber;
	// int res;
	// log_msg("derp\n");
	// res = open(name, O_CREAT | O_EXCL | O_WRONLY, mode);
	// if (res >= 0)
	// 	res = close(res);
	log_msg("Created file \"%s\" in block %d with data node in block %d. bitvector[%d] offset %d\n", 
		name, file_blockNumber, data_blockNumber, index, file_offset);
	//
    char fpath[PATH_MAX];
    
    log_msg("\nfs_mknod(path=\"%s\", mode=0%3o, dev=%lld)\n",
	  name, mode, dev);
    fs_fullpath(fpath, name);
    
    // On Linux this could just be 'mknod(path, mode, dev)' but this
    // tries to be be more portable by honoring the quote in the Linux
    // mknod man page stating the only portable use of mknod() is to
    // make a fifo, but saying it should never actually be used for
    // that.
 //    if (S_ISREG(mode)) {
	// retstat = log_syscall("open", open(fpath, O_CREAT | O_EXCL | O_WRONLY, mode), 0);
	// if (retstat >= 0)
	//     retstat = log_syscall("close", close(retstat), 0);
 //    } else
	// if (S_ISFIFO(mode))
	//     retstat = log_syscall("mkfifo", mkfifo(fpath, mode), 0);
	// else
	//     retstat = log_syscall("mknod", mknod(fpath, mode, dev), 0);
	//
	return 0;
}

// static int fs_getxattr(const char *a, const char *b, char *c, size_t d) {
// 	// called internally by mknod()
// 	return 0;
// }

// static int fs_setxattr (const char *a, const char *b, const char *c, size_t d, int e) {
// 	return 0;
// }
// static int fs_setattr (const char *a, const char *b, const char *c, size_t d, int e) {
// 	return 0;
// }

// int create_dir(NODE *memory, unsigned char *bitvector, int bitvector_len, char *name, mode_t access) {
static int create_dir(const char *path, mode_t mode) {
	log_msg("test %s\n", "create_dir");
	int mask = 0x00;
	int offset = 0;
	int index = find_zero_bit(0, &offset, &mask);
	if (index == -1) {
		// can't create file. no available bits
		printf("Can't create dir. No blocks available in memory\n");
		return -ENOSPC;
	}
	char fpath[PATH_MAX];
	log_msg("\nfs_mkdir(path=\"%s\", mode=0%3o)\n", path, mode);
	fs_fullpath(fpath, path);

	int dir_offset = offset;
	bitvector[index] = flip_bit(bitvector[index], mask);
	int dir_blockNumber = convert_index_to_block_number(index, offset);

	int i;
	int found = 0;
	printf("working_dir = %d\n", working_dir);
	for (i = 1; i < INDEX_SIZE && !found; i++) {
		if (memory[working_dir].content.index[i] == 0) {
			found = 1;
			memory[working_dir].content.index[i] = dir_blockNumber;
		}
	}
	if (!found) {
		printf("Can't create dir. No blocks available in current directory\n");
		return -ENOSPC;
	}

	memory[dir_blockNumber].type = DIR;
	strcpy(memory[dir_blockNumber].content.fd.name, path);
	memory[dir_blockNumber].content.fd.creat_t = time(NULL);
	memory[dir_blockNumber].content.fd.access_t = time(NULL);
	memory[dir_blockNumber].content.fd.mod_t = time(NULL);
	memory[dir_blockNumber].content.fd.owner = fuse_get_context()->uid;
	if (mode <= 0) {
		// default access is 744 or rwxr--r--
		// memory[dir_blockNumber].content.fd.access = S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH;
		memory[dir_blockNumber].content.fd.access = S_IFDIR | 0775;
	}
	else {
		memory[dir_blockNumber].content.fd.access = S_IFDIR | 0775;
	}
		// memory[dir_blockNumber].content.fd.access = mode;
	memory[dir_blockNumber].content.fd.size = 0;

	// find available block and create index node for the directory
	mask = 0x00;
	int indexnode_index = find_zero_bit(index, &offset, &mask);
	int index_blockNumber = convert_index_to_block_number(indexnode_index, offset);
	// if (index_blockNumber == -1) {
	// 	printf("CREATE_DIR FUNCTION. Error: index node block %d out of bounds.\n", index_blockNumber);
	// 	return -1;
	// }
	bitvector[indexnode_index] = flip_bit(bitvector[indexnode_index], mask);
	memory[index_blockNumber].type = INDEX;
	memset(memory[index_blockNumber].content.index, 0, INDEX_SIZE-1);
	memory[index_blockNumber].content.index[0] = dir_blockNumber;

	// tell directory where its index node is
	memory[dir_blockNumber].content.fd.block_ref = index_blockNumber;
	log_msg("Created directory \"%s\" in block %d with index node in block %d. bitvector[%d] offset %d\n", 
		path, dir_blockNumber, index_blockNumber, index, dir_offset);
	// return dir_blockNumber;
	return 0;
}

static int fs_unlink(const char *name) {
	log_msg("test %s\n", "fs_unlink");

	int i;
	int curr_block_num;
	int found = 0;
	for (i = 1; i < INDEX_SIZE && !found; i++) {
		curr_block_num = memory[working_dir].content.index[i];
		if (memory[curr_block_num].type == FIL) {
			if (strcmp(memory[curr_block_num].content.fd.name, name) == 0) {
				found = 1;
			}
		}
	}
	if (found) {
		// Delete it: 
		// 1) set bitvector bit to 0. 2) set index in parent index node to 0. 3) delete data node(s)

		// 1) set index in bitvector to 0
		int blockNumber = curr_block_num;
		int bitvector_index = blockNumber / 8;
		int bitvector_offset = blockNumber % 8;
		int mask = 0x80;
		int i;
		for (i = 0; i < bitvector_offset; i++) {
			mask /= 2;
		}
		if ((bitvector[bitvector_index] & mask) != 0) {
			bitvector[bitvector_index] = flip_bit(bitvector[bitvector_index], mask);
		}

		// 2) set index in parent index node to 0.
		found = 0;
		for (i = 1; i < INDEX_SIZE && !found; i++) {
			curr_block_num = memory[working_dir].content.index[i];
			if (curr_block_num == blockNumber) {
				found = 1;
				memory[working_dir].content.index[i] = 0;
			}
		}
		if (!found) {
			log_msg("ERROR: FILE NOT FOUND IN PARENT DIRECTORY\n");
		}

		// 3) delete data node
		// TODO: file's index nodes if it has more than 1 data node
		int data_blockNumber = memory[blockNumber].content.fd.block_ref;
		int data_bitvector_index = data_blockNumber / 8;
		int data_bitvector_offset = data_blockNumber % 8;
		mask = 0x80;
		for (i = 0; i < data_bitvector_offset; i++) {
			mask /= 2;
		}
		if ((bitvector[data_bitvector_index] & mask) != 0) {
			bitvector[data_bitvector_index] = flip_bit(bitvector[data_bitvector_index], mask);
		}
		log_msg("Deleted file \"%s\" in block %d with data node in block %d. bitvector[%d] offset %d\n", 
			memory[blockNumber].content.fd.name, blockNumber, data_blockNumber, bitvector_index, bitvector_offset);
		return 0;
	}
	else {
		return -ENOENT;
	}
}

static int fs_rmdir(const char *name) {
	log_msg("test %s\n", "fs_rmdir");
	int i;
	int curr_block_num;
	int found = 0;
	for (i = 1; i < INDEX_SIZE && !found; i++) {
		curr_block_num = memory[working_dir].content.index[i];
		if (memory[curr_block_num].type == DIR &&
			(strcmp(memory[curr_block_num].content.fd.name, name) == 0)) 
		{
				found = 1;
		}
	}
	if (found) {
		// Delete it:

		// set index in bitvector to 0
		int blockNumber = curr_block_num;
		int bitvector_index = blockNumber / 8;
		int bitvector_offset = blockNumber % 8;
		int mask = 0x80;
		int i;
		for (i = 0; i < bitvector_offset; i++) {
			mask /= 2;
		}
		if ((bitvector[bitvector_index] & mask) != 0) {
			bitvector[bitvector_index] = flip_bit(bitvector[bitvector_index], mask);
		}

		if (!(FS_DATA->is_child)) {
			// dir is a first-level parent
			// set index in dir's parent's index array to 0. 
			found = 0;
			int parent_block = memory[curr_block_num].content.index[0];
			for (i = 1; i < INDEX_SIZE && !found; i++) {
				curr_block_num = memory[parent_block].content.index[i];
				if (curr_block_num == blockNumber) {
					found = 1;
					memory[parent_block].content.index[i] = 0;
				}
			}
			if (!found) {
				log_msg("ERROR: FILE NOT FOUND IN PARENT DIRECTORY\n");
			}

			// kill all children of the dir
			int index = memory[blockNumber].content.fd.block_ref;
			int val;
			for (i = 0; i < INDEX_SIZE; i++) {
				val = memory[index].content.index[i];
				if (val > 1) {
					if (memory[val].type == FIL) {
						log_msg("DELETING CHILD FILE IN BLOCK %d\n", val);
						fs_unlink(memory[val].content.fd.name);
					}
					else if (memory[val].type == DIR) {
						log_msg("DELETING PARENT DIRECTORY IN BLOCK %d\n", val);
						FS_DATA->is_child = 1;
						int curr_dir = working_dir;
						working_dir = index;
						fs_rmdir(memory[val].content.fd.name);
						working_dir = curr_dir;
						FS_DATA->is_child = 0;
					}
				}
			}
		}
		else {
			// dir is not a first-level parent
			// kill all children of the subdir
			int index = memory[blockNumber].content.fd.block_ref;
			int val;
			for (i = 0; i < INDEX_SIZE; i++) {
				val = memory[index].content.index[i];
				if (val > 1) {
					if (memory[val].type == FIL) {
						log_msg("DELETING CHILD FILE IN BLOCK %d\n", val);
						fs_unlink(memory[val].content.fd.name);
					}
					else if (memory[val].type == DIR) {
						log_msg("DELETING CHILD DIRECTORY IN BLOCK %d\n", val);
						int curr_dir = working_dir;
						working_dir = index;
						fs_rmdir(memory[val].content.fd.name);
						working_dir = curr_dir;
					}
				}
			}
		}
		log_msg("Deleted directory \"%s\" in block %d and bitvector[%d] offset %d\n", 
		memory[blockNumber].content.fd.name, blockNumber, bitvector_index, bitvector_offset);
		return 0;
	}
	else {
		return -ENOENT;
	}
}

static int fs_getattr(const char *name, struct stat *st) {
	printf("GET_ATTR\n");
	log_msg("test fs_getattr for file %s\n", name);
	uid_t uid = fuse_get_context()->uid;
	log_msg("current uid = %d\n", uid);

	// char fpath[PATH_MAX];
 	// fs_fullpath(fpath, name);
    
    int i;
	int curr_block_num;
	int found = 0;
	for (i = 1; i < INDEX_SIZE && !found; i++) {
		curr_block_num = memory[working_dir].content.index[i];
		if (memory[curr_block_num].type == FIL || memory[curr_block_num].type == DIR) {
			if (strcmp(memory[curr_block_num].content.fd.name, name) == 0) {
				found = 1;
			}
		}
	}
	if (found) {
		// move file info into struct
		if (memory[curr_block_num].type == DIR)
			st->st_mode = S_IFDIR | 0755;
		else
			st->st_mode = S_IFREG | 0777;
		st->st_uid = memory[curr_block_num].content.fd.owner;
		st->st_mode = memory[curr_block_num].content.fd.access;
		st->st_size = memory[curr_block_num].content.fd.size;
		st->st_size = 100;
		st->st_atime = memory[curr_block_num].content.fd.access_t;
		st->st_mtime = memory[curr_block_num].content.fd.mod_t;
		st->st_ctime = memory[curr_block_num].content.fd.creat_t;
		// int retstat = log_syscall("lstat", lstat(fpath, st), 0);
		log_stat(st);
		return 0;
	}
	else {
		return -ENOENT;
	}
}

static int fs_open(const char *path, struct fuse_file_info *fi) {
	log_msg("test %s\n", "fs_open");
	if((fi->flags & O_RDONLY) != O_RDONLY) {
		printf("GG. %d\n", fi->flags);
 	 		return -EACCES;
 	}
  	
    char fpath[PATH_MAX];
    
    log_msg("\nfs_open(path\"%s\", fi=0x%08x)\n",
	    path, fi);
    fs_fullpath(fpath, path);
    // If file is not in open global file table, add it to the table
    int block_num = FS_DATA->block_num;
    int global_table_entry_num = getslot(block_num);
    log_msg("global_table[%d].fd = %d\n", global_table_entry_num, global_table[global_table_entry_num].fd);
    if (global_table[global_table_entry_num].fd == 0) {
    	// file is not yet in global table
    	global_table[global_table_entry_num].fd = block_num;
    	global_table[global_table_entry_num].data = memory[block_num].content.fd.block_ref;
    	global_table[global_table_entry_num].access = memory[block_num].content.fd.access;
    	global_table[global_table_entry_num].size = memory[block_num].content.fd.size;
    	global_table[global_table_entry_num].reference_count = 0;
    }
    int pid = fuse_get_context()->pid;
    log_msg("PROCESS ID is %d\n", pid);
    // add file to local table
    int i;
    int found = 0;
    for (i = 0; i < MAX_OPEN_FILES_PER_PROCESS && !found; i++) {
    	// printf("local_table[%d].global_ref = %d\n", i, local_table[i].global_ref);
    	if (local_table[i].global_ref == 0) {
    		found = 1;
    		local_table[i].global_ref = global_table_entry_num;
    		local_table[i].access_rights = global_table[global_table_entry_num].access;
    	}
    }
    if (!found) {
    	// no space in local file table
    	log_msg("NO SPACE IN LOCAL FILE TABLE for file %s\n", path);
    	FS_DATA->block_num = 0;
    	return -ENOSPC;
    }

    // if the open call succeeds, my retstat is the file descriptor,
    // else it's -errno.  I'm making sure that in that case the saved
    // file descriptor is exactly -1.
    // int retstat = 0;
    // int fd;
    // fd = open(fpath, fi->flags);
 	//   if (fd < 0)
	// retstat = log_error("open");
  	//   fi->fh = fd;
 	//   log_fi(fi);
    FS_DATA->block_num = 0;

	return 0;
}

static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
off_t offset, struct fuse_file_info *fi) {
	 log_msg("test fs_readdir for path %s\n", path);
	
	 int i;
	 int num_of_files = 0;
	 int curr_block_num = 0;
	 for (i = 1; i < INDEX_SIZE; i++) {
	 	curr_block_num = memory[working_dir].content.index[i];
	 	if (curr_block_num > 0 && (memory[curr_block_num].type == FIL || memory[curr_block_num].type == DIR)) {
	 		num_of_files++;
	 		log_msg("found item: %s\n", memory[curr_block_num].content.fd.name);
	 		filler(buf, memory[curr_block_num].content.fd.name + 1, NULL, 0);
	 	}
	 }

	 // printf("\n%2s FILE INFORMATION:\n", "");
	// printf("%2s %-25s ", "", "Type:");
	// if (memory[blockNumber].type == DIR)
	// 	printf("DIR\n");
	// else
	// 	printf("FILE\n");
	// printf("%2s %-25s %-25s\n", "", "Name:", memory[blockNumber].content.fd.name);
	// struct tm *create = localtime(&(memory[blockNumber].content.fd.creat_t));
 //    char s[64];
 //    strftime(s, sizeof(s), "%c", create);
 //    printf("%2s %-25s %-20s\n", "", "Create time:", s);
 //    struct tm *access = localtime(&(memory[blockNumber].content.fd.access_t));
 //    strftime(s, sizeof(s), "%c", access);
 //    printf("%2s %-25s %-20s\n", "", "Last access time:", s);
 //    struct tm *mod = localtime(&(memory[blockNumber].content.fd.mod_t));
 //    strftime(s, sizeof(s), "%c", mod);
 //    printf("%2s %-25s %-20s\n", "", "Last modification time:", s);
 //    printf("%2s %-25s %-20d\n", "", "Access rights:", memory[blockNumber].content.fd.access); 
 //    printf("%2s %-25s %-20d\n", "", "Owner:", memory[blockNumber].content.fd.owner);

	return 0;
}

static int fs_read(const char *path, char *buf, size_t size, off_t offset,
struct fuse_file_info *fi) {
	log_msg("\nfs_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
	    path, buf, size, offset, fi);
	// TODO: actually copy the data string from the file into the buf
	(void) fi;
	int i;
    int found = 0;
    int block_num;
    for (i = 0; i < INDEX_SIZE && !found; i++) {
    	block_num = memory[working_dir].content.index[i];
    	if (block_num > 0 && memory[block_num].type == FIL && strcmp(memory[block_num].content.fd.name, path) == 0) {
    		found = 1;
    		printf("FOUND file %s in block %d\n", path, block_num);
    	}
    }
    if (found) {
    	int data_node = memory[block_num].content.fd.block_ref;
    	printf("memory[%d].content.data = %s\n", data_node, memory[data_node].content.data);
    	strcpy(memory[data_node].content.data, "test_data");
    	
    	printf("memory[%d].content.data = %s\n", data_node, memory[data_node].content.data);
    	int len = strlen(memory[data_node].content.data);
    	len +=1;
    	memcpy(buf, memory[data_node].content.data + offset, len);	
    	// memory[data_node].content.data[len-2] = '\n';
    	// memory[data_node].content.data[len-1] = '\n';
    	buf[len-1] = '\n';
    	printf("buf = %s\n", buf);
    	return strlen(memory[data_node].content.data);
    }
//     unique: 240, opcode: LOOKUP (1), nodeid: 1, insize: 51, pid: 6189
// LOOKUP /first_file
// getattr /first_file
// GETATTR
//    NODEID: 2
//    unique: 240, success, outsize: 144
// unique: 241, opcode: OPEN (14), nodeid: 2, insize: 48, pid: 6189
// open flags: 0x8000 /first_file
// OPEN
//    open[0] flags: 0x8000 /first_file
//    unique: 241, success, outsize: 32
// unique: 242, opcode: READ (15), nodeid: 2, insize: 80, pid: 6189
// read[0] 4096 bytes from 0 flags: 0x8000
// READ
//    read[0] 13 bytes from 0
//    unique: 242, success, outsize: 29
// unique: 243, opcode: GETATTR (3), nodeid: 2, insize: 56, pid: 6189
// getattr /first_file
// GETATTR
//    unique: 243, success, outsize: 120
// unique: 244, opcode: RELEASE (18), nodeid: 2, insize: 64, pid: 0
//    unique: 244, success, outsize: 16


    // char test[100] = "TEST REEEEEAD";
    // printf("test = %s\n", test);
    // memcpy(buf, test, 100);
    // printf("buf = %s\n", buf);

	// if(strcmp(path, hello_path) == 0) {
	//     len = strlen(hello_str);
	//     if (offset < len) {
	// 	    if (offset + size > len)
	// 	    	size = len - offset;
	// 	    memcpy(buf, hello_str + offset, size);
	//     }
	//     else
	//     	size = 0;
	// }
	return size;
}

static int fs_write(const char *path, const char *buf, size_t size, off_t offset,
	     struct fuse_file_info *fi) {
    // if ((fi->flags & O_WRONLY) != O_WRONLY) {
    // 	log_msg("ERROR. FILE %s DOES NOT HAVE WRITE ACCESS\n", path);
    // 	return -EACCES;
    // }
    log_msg("\nfs_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
	    path, buf, size, offset, fi);
    // find file block number
    int i;
    int found = 0;
    int block_num;
    for (i = 0; i < INDEX_SIZE && !found; i++) {
    	block_num = memory[working_dir].content.index[i];
    	if (block_num > 0 && strcmp(memory[block_num].content.fd.name, path) == 0) {
    		found = 1;
    	}
    }
    if (found) {
    	memory[block_num].content.fd.size = size;
    	int data_block_num = memory[block_num].content.fd.block_ref;
    	memcpy(memory[data_block_num].content.data, buf, size);
    }
    else {
    	printf("WRITE FILE NOT FOUND\n");
    }
   
    printf("COPYING BUF = <%s> to %s\n", buf, path);
    
    return size;
}

// static int fs_flush(const char *a, struct fuse_file_info *b) {
// 	printf("FLUSH = %s\n", a);
// 	return 0;
// }

// static int fs_truncate(const char *a, off_t b) {
// 	return 0;
// }

// static int fs_ftruncate(const char *a, off_t b, struct fuse_file_info *c) {
// 	return 0;
// }

// static int fs_opendir(const char *a, struct fuse_file_info *b) {
// 	log_msg("OPENDIR FUNCTION. Opening dir %s\n", a);
	
// 	return 0;
// }

static struct fuse_operations fuse_ops = {
 .getattr = fs_getattr,
 .init = fs_init,
 .mkdir = create_dir,
 .rmdir = fs_rmdir,
 .mknod = fs_mknod,
 .unlink = fs_unlink,
 .open = fs_open,
 .readdir = fs_readdir,
 .read = fs_read,
 // .flush = fs_flush,
 // .truncate = fs_truncate,
 // .ftruncate = fs_ftruncate,
 // .opendir = fs_opendir,
 .write = fs_write,
};

int main(int argc, char *argv[]) {
	// int num_of_blocks = 256; // 2^8
	// unsigned char *bitvector;
	// int bitvector_len = num_of_blocks / 8;
	// NODE *memory;
	// init(&memory, &bitvector, num_of_blocks);
	if (argc < 3) {
		printf("Usage: t4 rootdir mountdir\n");
		exit(-1);
	}
	struct fs_state *fs_data;
	fs_data = malloc(sizeof(struct fs_state));
	
	fs_data->rootdir = realpath(argv[argc-2], NULL);
	fs_data->is_child = 0;
	fs_data->block_num = 0;
	argv[argc-2] = argv[argc-1];
	argv[argc-1] = NULL;
	argc--;
	
	fs_data->logfile = log_open();
	

	



	return fuse_main(argc, argv, &fuse_ops, fs_data);

	// int file1 = create_file(memory, bitvector, bitvector_len, "file1", 0);
	// int file2 = create_file(memory, bitvector, bitvector_len, "file2", 0);
	// int file3 = create_file(memory, bitvector, bitvector_len, "file3", 0);

	// int dir1 = create_dir(memory, bitvector, bitvector_len, "dir1", 0);
	// int dir2 = create_dir(memory, bitvector, bitvector_len, "dir2", 0);
	// int dir3 = create_dir(memory, bitvector, bitvector_len, "dir3", 0);
	// int dir4 = create_dir(memory, bitvector, bitvector_len, "dir4", 0);
	// int dir5 = create_dir(memory, bitvector, bitvector_len, "dir5", 0);
	// int dir6 = create_dir(memory, bitvector, bitvector_len, "dir6", 0);

	// fs_getattr(memory, bitvector, bitvector_len, "", file1);
	// fs_unlink(memory, bitvector, bitvector_len, "", file1);
	// fs_getattr(memory, bitvector, bitvector_len, "", file1);

	// int file4 = create_file(memory, bitvector, bitvector_len, "file4", 0);
	// int dir7 = create_dir(memory, bitvector, bitvector_len, "dir7", 0);
	// fs_getattr(memory, bitvector, bitvector_len, "", dir7);
	// fs_rmdir(memory, bitvector, bitvector_len, "", dir7);
	// fs_getattr(memory, bitvector, bitvector_len, "", dir7);

	free(memory);
    return 0;
}