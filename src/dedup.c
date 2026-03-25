
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

#include "metaindex.h"
#include "dedup.h"
#include "passthrough_helpers.h"

int master_read(off_t offset, char *buff)
{
    // TODO: read from master file at offset and fill buff with the data
    //  read from disk at offset
    //  return the data to the user
    return 0; // return the number of bytes read
}

int read_block(const char *path, off_t offset, char *buff, Index *index)
{
    BlockIndice blockIndice;
    blockIndice.path = (char *)path;
    blockIndice.offset = offset;

    int res = 0;
    unsigned char *hash = g_hash_table_lookup(index->file_to_hash, &blockIndice);

    if (hash != NULL)
    {
        // block already exists, we can read from the hash table
        FileInfo *fileInfo = g_hash_table_lookup(index->hash_to_FileInfo, hash);
        if (fileInfo == NULL)
        {
            return -1;
        }
        // now I can read from the master file at fileInfo.offset and return the data to the user
        res = master_read(fileInfo->offset, buff);
    }
    else
    {
        return -1; // block does not exist, we can read from the original file
    }

    return res;
}