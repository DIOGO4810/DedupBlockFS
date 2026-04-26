#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include <fcntl.h>
#include <glib.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
typedef struct {
  void *data;
  size_t size;
} Bytes;

typedef struct {
  GHashTable *table1;
  GHashTable *table2;
} IndexedPairTables;

typedef Bytes (*EncodeFunc)(void *elem);
typedef void *(*DecodeFunc)(void *data, int size);

void ghash_save(const char *path, GHashTable *ht, EncodeFunc encode_key,
                EncodeFunc encode_val);

GHashTable *ghash_load(const char *path, GHashFunc hash_fn, GEqualFunc equal_fn,
                       DecodeFunc decode_key, DecodeFunc decode_val,
                       GDestroyNotify free_key, GDestroyNotify free_val);

void ghash_save_indexed_pair(const char *table1_path, const char *table2_path,
                             const char *values_path, GHashTable *table1,
                             GHashTable *table2, EncodeFunc encode_key1,
                             EncodeFunc encode_key2, EncodeFunc encode_value);

IndexedPairTables ghash_load_indexed_pair(
    const char *table1_path, const char *table2_path, const char *values_path,
    GHashFunc hash1_fn, GEqualFunc equal1_fn, DecodeFunc decode_key1,
    GDestroyNotify free_key1, GHashFunc hash2_fn, GEqualFunc equal2_fn,
    DecodeFunc decode_key2, GDestroyNotify free_key2, DecodeFunc decode_value,
    GDestroyNotify free_value);

void gslist_save(const char *path, GSList *list, EncodeFunc encode_elem);
GSList *gslist_load(const char *path, DecodeFunc decode_elem);

#endif
