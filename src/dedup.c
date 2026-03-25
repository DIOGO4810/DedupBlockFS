
#include <stddef.h>
#define FUSE_USE_VERSION 31

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse3/fuse.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __FreeBSD__
#include <sys/socket.h>
#include <sys/un.h>
#endif
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include "dedup.h"
#include "hashing.h"
#include "metaindex.h"
#include "passthrough_helpers.h"

int master_read(int fdMaster, off_t offset, char *buff) {
  return pread(fdMaster, buff, BLOCK_SIZE, offset);
}

int read_block(int fdMaster, const char *path, off_t block_index, char *buff,
               Index *index) {
  BlockIndice blockIndice;
  blockIndice.path = (char *)path;
  blockIndice.offset = block_index;

  int res = 0;
  unsigned char *hash = g_hash_table_lookup(index->file_to_hash, &blockIndice);

  if (hash != NULL) {
    // block already exists, we can read from the hash table
    FileInfo *fileInfo = g_hash_table_lookup(index->hash_to_FileInfo, hash);
    if (fileInfo == NULL) {
      return -1;
    }
    // now I can read from the master file at fileInfo.offset and return the
    // data to the user
    res = master_read(fdMaster, fileInfo->offset, buff);
  } else {
    return -1; // block does not exist, we can read from the original file
  }

  return res;
}

int read_dedup(Index *index, const char *path, char *buf, size_t size,
               off_t offset, int masterFd) {
  ssize_t total_read = 0;
  size_t num_blocks = size / BLOCK_SIZE;
  off_t start_block = offset / BLOCK_SIZE;
  int res = 0;
  for (size_t i = 0; i < num_blocks; i++) {
    char buff[BLOCK_SIZE];
    res = read_block(masterFd, path, start_block + i, buff, index);
    if (res == -1) {
      return res;
    }
    memcpy(buf + i * BLOCK_SIZE, buff, BLOCK_SIZE);
    total_read += BLOCK_SIZE;
  }
  return total_read;
}

int write_dedup(Index *index, const char *path, const char *buf, size_t size,
                off_t offset, int masterFd, size_t sizeMaster) {
  size_t num_blocks = size / BLOCK_SIZE;
  off_t start_block = offset / BLOCK_SIZE;
  int res = 0;
  char hash_bytes[HASH_SIZE] = {0};
  unsigned char block[BLOCK_SIZE] = {0};

  for (int i = 0; i < num_blocks; i++) {

    memcpy(block, buf + i * BLOCK_SIZE, BLOCK_SIZE);
    hash(block, hash_bytes);

    BlockIndice *block_index = malloc(sizeof(BlockIndice));
    block_index->path = strdup(path);
    block_index->offset = start_block + i;

    char *hash_copy = malloc(HASH_SIZE);
    memcpy(hash_copy, hash_bytes, HASH_SIZE);
    g_hash_table_insert(index->file_to_hash, block_index, hash_copy);

    FileInfo *info = g_hash_table_lookup(index->hash_to_FileInfo, hash_bytes);
    if (info == NULL) {
      printf("MISS\n");
      off_t masterOffset = sizeMaster;
      if (index->empty_blocks_set) {
        void *offset_pointer = index->empty_blocks_set->data;
        if (offset_pointer) {
          masterOffset = *(size_t *)offset_pointer;
          index->empty_blocks_set = g_slist_delete_link(
              index->empty_blocks_set, index->empty_blocks_set);
        }
      }

      info = malloc(sizeof(FileInfo));
      info->counter = 1;
      info->offset = masterOffset;

      size_t bytes_written = pwrite(masterFd, block, BLOCK_SIZE, masterOffset);

      if (bytes_written == -1)
        return -errno;

      res += bytes_written;
      sizeMaster += BLOCK_SIZE;

      char *hash_copy2 = malloc(HASH_SIZE);
      memcpy(hash_copy2, hash_bytes, HASH_SIZE);
      g_hash_table_insert(index->hash_to_FileInfo, hash_copy2, info);

    } else {
      printf("HIT\n");
      info->counter++;
      res += BLOCK_SIZE;
    }
  }

  size_t *logical_size = g_hash_table_lookup(index->file_to_sizes, path);
  if (logical_size == NULL) {
    logical_size = malloc(sizeof(size_t));
    *logical_size = res;
    g_hash_table_insert(index->file_to_sizes, strdup(path), logical_size);
  } else {
    *logical_size += res;
  }

  return res;
}
