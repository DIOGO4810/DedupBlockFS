#include "metaindex.h"
#include "glib.h"
#include <stdio.h>
#include <stdlib.h>

guint hash512_hash(gconstpointer key) {
  const uint64_t *data = key;

  uint64_t folded = data[0] ^ data[1] ^ data[2] ^ data[3] ^ data[4] ^ data[5] ^
                    data[6] ^ data[7];

  return (guint)(folded ^ (folded >> 32));
}

gboolean hash512_equal(gconstpointer a, gconstpointer b) {
  return memcmp(a, b, HASH_SIZE) == 0;
}
guint blockIndice_hash(gconstpointer key) {
  const BlockIndice *b = key;

  guint h1 = g_str_hash(b->path);
  guint h2 = g_int64_hash(&b->offset);

  return h1 ^ (h2 << 1);
}

gboolean blockIndice_equal(gconstpointer a, gconstpointer b) {
  const BlockIndice *x = a;
  const BlockIndice *y = b;

  return (x->offset == y->offset) && (g_str_equal(x->path, y->path));
}
Index *index_init() {

  // allocate memory for the index
  Index *index = malloc(sizeof(Index));

  // GHashTable initialization
  index->file_to_hash = g_hash_table_new_full(
      blockIndice_hash, blockIndice_equal, g_free, g_free);
  index->hash_to_FileInfo =
      g_hash_table_new_full(hash512_hash, hash512_equal, g_free, g_free);
  index->file_to_sizes =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  index->empty_blocks_set = malloc(sizeof(GSList));

  // mutex variable initialization
  // int pthread_mutex_init(pthread_mutex_t *mutex,const pthread_mutexattr_t
  // *attr); attr can be used to define non-default attributes (e.g., recursive
  // lock)
  pthread_mutex_init(&index->mutex, NULL);

  return index;
}

// Note: remember that memory allocation and copying must be done here
// int index_add(Index *index, char *key, Filemeta meta) {
//
//   int res = -1;
//
//   char *nkey = strdup(key);
//   if (nkey == NULL)
//     return res;
//
//   Filemeta *value = malloc(sizeof(Filemeta));
//   if (value == NULL)
//     return res;
//
//   value->sizeLogical = meta.sizeLogical;
//   value->sizeFisico = meta.sizeFisico;
//
//   pthread_mutex_lock(&index->mutex);
//   if (g_hash_table_insert(index->htable, nkey, value) == 1)
//     res = 0;
//   pthread_mutex_unlock(&index->mutex);
//
//   return res;
// }
//
// int index_get(Index *index, char *key, Filemeta *meta) {
//
//   int res = -1;
//
//   pthread_mutex_lock(&index->mutex);
//   Filemeta *value = g_hash_table_lookup(index->htable, key);
//   if (value != NULL) {
//     res = 0;
//     meta->sizeLogical = value->sizeLogical;
//     meta->sizeFisico = value->sizeFisico;
//   }
//   pthread_mutex_unlock(&index->mutex);
//
//   return res;
// }
//
// int index_remove(Index *index, char *key) {
//
//   int res = -1;
//
//   pthread_mutex_lock(&index->mutex);
//   Filemeta *value = g_hash_table_lookup(index->htable, key);
//   if (value != NULL) {
//     if (g_hash_table_remove(index->htable, key) == 1)
//       res = 0;
//   }
//   pthread_mutex_unlock(&index->mutex);
//
//   return res;
// }
//
void index_destroy(Index *index) {

  // destroy hashtable
  g_hash_table_destroy(index->file_to_sizes);
  g_hash_table_destroy(index->hash_to_FileInfo);
  g_hash_table_destroy(index->file_to_hash);
  g_slist_free_full((index->empty_blocks_set), free);

  // destroy mutex and cond variables
  pthread_mutex_destroy(&index->mutex);

  free(index);
}
