#include "metaindex.h"
#include "glib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

guint hash512_hash(gconstpointer key)
{
  const uint64_t *data = key;

  uint64_t folded = data[0] ^ data[1] ^ data[2] ^ data[3] ^ data[4] ^ data[5] ^
                    data[6] ^ data[7];

  return (guint)(folded ^ (folded >> 32));
}

gboolean hash512_equal(gconstpointer a, gconstpointer b)
{
  return memcmp(a, b, HASH_SIZE) == 0;
}

guint blockIndice_hash(gconstpointer key)
{
  const BlockIndice *b = key;

  guint h1 = g_str_hash(b->path);
  guint h2 = g_int64_hash(&b->offset);

  return h1 ^ (h2 << 1);
}

gboolean blockIndice_equal(gconstpointer a, gconstpointer b)
{
  const BlockIndice *x = a;
  const BlockIndice *y = b;

  return (x->offset == y->offset) && (g_str_equal(x->path, y->path));
}

static void block_indice_free(gpointer data)
{
  BlockIndice *b = data;
  g_free((char *)b->path);
  g_free(b);
}

Index *index_init(void)
{
  Index *index = malloc(sizeof(Index));

  // hash_to_master: key = g_memdup'd hash (freed by g_free),
  //                 value = MasterInfo* (NOT freed by table managed by refcount)
  index->hash_to_master =
      g_hash_table_new_full(hash512_hash, hash512_equal, g_free, NULL);

  // file_to_master: key = BlockIndice* (freed by block_indice_free),
  //                 value = MasterInfo* (NOT freed by table)
  index->file_to_master =
      g_hash_table_new_full(blockIndice_hash, blockIndice_equal,
                            block_indice_free, NULL);

  index->file_to_sizes =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

  index->free_block_list = NULL; // GSList starts empty

  pthread_mutex_init(&index->mutex, NULL);

  return index;
}

void index_destroy(Index *index)
{
  // Free all MasterInfo objects (one per unique block, stored in hash_to_master)
  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, index->hash_to_master);
  while (g_hash_table_iter_next(&iter, &key, &value))
  {
    free((MasterInfo *)value);
  }

  g_hash_table_destroy(index->file_to_master);
  g_hash_table_destroy(index->hash_to_master);
  g_hash_table_destroy(index->file_to_sizes);
  g_slist_free_full(index->free_block_list, free);

  pthread_mutex_destroy(&index->mutex);
  free(index);
}

// --- Logical index: (file, blockIndex) -> MasterInfo* ---

MasterInfo *lookup_by_file_block(Index *index, const char *file,
                                 uint64_t blockIndex)
{
  BlockIndice key = {.path = file, .offset = blockIndex};
  return g_hash_table_lookup(index->file_to_master, &key);
}

void insert_file_block(Index *index, const char *file, uint64_t blockIndex,
                       MasterInfo *info)
{
  BlockIndice *key = g_new(BlockIndice, 1);
  key->path = g_strdup(file);
  key->offset = blockIndex;
  g_hash_table_insert(index->file_to_master, key, info);
}

void remove_file_block(Index *index, const char *file, uint64_t blockIndex)
{
  BlockIndice key = {.path = file, .offset = blockIndex};
  g_hash_table_remove(index->file_to_master, &key);
}

// --- Hash index: hash -> MasterInfo* ---

MasterInfo *lookup_by_hash(Index *index, const unsigned char *hash)
{
  return g_hash_table_lookup(index->hash_to_master, hash);
}

void insert_hash(Index *index, const unsigned char *hash, MasterInfo *info)
{
  unsigned char *key = g_memdup2(hash, HASH_SIZE);
  g_hash_table_insert(index->hash_to_master, key, info);
}

void remove_hash(Index *index, const unsigned char *hash)
{
  g_hash_table_remove(index->hash_to_master, hash);
}
