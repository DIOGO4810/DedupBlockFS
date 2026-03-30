#ifndef DEDUP_H
#define DEDUP_H

#include <sys/types.h>
#include <stdint.h>

typedef struct index Index;

int read_dedup(Index *index, const char *path, char *buf, size_t size,
               off_t offset, int masterFd);

int write_dedup(Index *index, const char *path, const char *buf, size_t size,
                off_t offset, int masterFd, uint64_t *nextBlockIndex);

void remove_block_dedup(Index *index, const char *path, uint64_t blockIndex);

#endif
