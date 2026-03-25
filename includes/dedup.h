#ifndef DEDUP_H
#define DEDUP_H

#include <sys/types.h>

typedef struct index Index;

int master_read(off_t offset, char *buff);
int read_block(const char *path, off_t offset, char *buff, Index *index);

#endif
