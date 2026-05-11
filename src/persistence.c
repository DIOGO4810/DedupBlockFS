#include "persistence.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static BufferedIOCtx *buffered_io_init(int fd, size_t buffer_size) {
  BufferedIOCtx *ctx = malloc(sizeof(BufferedIOCtx));
  ctx->fd = fd;
  ctx->buffer = malloc(buffer_size);
  ctx->buffer_size = buffer_size;
  ctx->buffer_pos = 0;
  ctx->buffer_end = 0;
  ctx->file_offset = 0;
  return ctx;
}

static void buffered_io_free(BufferedIOCtx *ctx) {
  if (ctx) {
    free(ctx->buffer);
    free(ctx);
  }
}

// Flush buffered writes to disk.
static void flush_write_buffer(BufferedIOCtx *ctx) {
  if (ctx->buffer_pos == 0)
    return;

  ssize_t written =
      pwrite(ctx->fd, ctx->buffer, ctx->buffer_pos, ctx->file_offset);
  if (written == (ssize_t)ctx->buffer_pos) {
    ctx->file_offset += ctx->buffer_pos;
    ctx->buffer_pos = 0;
  }
}

// Refill buffer for reads
static gboolean refill_read_buffer(BufferedIOCtx *ctx) {
  ssize_t nread =
      pread(ctx->fd, ctx->buffer, ctx->buffer_size, ctx->file_offset);
  if (nread <= 0)
    return FALSE;

  ctx->buffer_end = nread;
  ctx->buffer_pos = 0;
  ctx->file_offset += nread;
  return TRUE;
}

// Write data to buffered context, flushing when necessary
static void buffered_write(BufferedIOCtx *ctx, const void *data, size_t size) {
  const void *src = data;
  while (size > 0) {
    size_t available = ctx->buffer_size - ctx->buffer_pos;
    if (available == 0) {
      flush_write_buffer(ctx);
      available = ctx->buffer_size;
    }

    size_t to_copy = (size < available) ? size : available;
    memcpy((char *)ctx->buffer + ctx->buffer_pos, src, to_copy);
    ctx->buffer_pos += to_copy;
    src = (const char *)src + to_copy;
    size -= to_copy;
  }
}

// Read data from buffered context, refilling when necessary
static gboolean buffered_read(BufferedIOCtx *ctx, void *data, size_t size) {
  void *dst = data;
  while (size > 0) {
    size_t available = ctx->buffer_end - ctx->buffer_pos;
    if (available == 0) {
      if (!refill_read_buffer(ctx))
        return FALSE;
      available = ctx->buffer_end - ctx->buffer_pos;
      if (available == 0)
        return FALSE;
    }

    size_t to_copy = (size < available) ? size : available;
    memcpy(dst, (char *)ctx->buffer + ctx->buffer_pos, to_copy);
    ctx->buffer_pos += to_copy;
    dst = (char *)dst + to_copy;
    size -= to_copy;
  }
  return TRUE;
}

typedef struct {
  BufferedIOCtx *io_ctx;
  EncodeFunc encode_key;
  EncodeFunc encode_val;
  gboolean free_encoded_key;
  gboolean free_encoded_val;
} SaveCtx;

typedef struct {
  GHashTable *value_to_index;
  GArray *values;
} IndexedValueCtx;

typedef struct {
  BufferedIOCtx *io_ctx;
  EncodeFunc encode_key;
  gboolean free_encoded_key;
  GHashTable *value_to_index;
} SaveIndexedCtx;

static Bytes encode_size_elem(void *elem) {
  return (Bytes){elem, sizeof(size_t)};
}

static void *decode_size_elem(void *data, int size) {
  if (size != sizeof(size_t))
    return NULL;

  size_t *value = malloc(sizeof(size_t));
  memcpy(value, data, sizeof(size_t));
  return value;
}

// Write element to buffered I/O context
static void write_elem_buffered(BufferedIOCtx *io_ctx, Bytes b) {
  size_t size = b.size;
  buffered_write(io_ctx, &size, sizeof(size));
  buffered_write(io_ctx, b.data, b.size);
}

// Read element from buffered I/O context
static void *read_elem_buffered(BufferedIOCtx *io_ctx, DecodeFunc decode_func) {
  size_t size = 0;
  if (!buffered_read(io_ctx, &size, sizeof(size)))
    return NULL;

  void *buf = malloc(size);
  if (!buffered_read(io_ctx, buf, size)) {
    free(buf);
    return NULL;
  }

  void *elem = decode_func(buf, (int)size);
  free(buf);
  return elem;
}

// Legacy functions for backward compatibility (not buffered)
void write_elem(int fd, Bytes b, off_t *offset) {
  if (pwrite(fd, &b.size, sizeof(b.size), *offset) != sizeof(b.size))
    return;
  *offset += sizeof(b.size);

  if (pwrite(fd, b.data, b.size, *offset) != b.size)
    return;
  *offset += b.size;
}

void *read_elem(int fd, DecodeFunc decode_func, off_t *offset) {
  size_t size = 0;
  if (pread(fd, &size, sizeof(size), *offset) != sizeof(size))
    return NULL;
  *offset += sizeof(size);

  void *buf = malloc(size);
  if (pread(fd, buf, size, *offset) != size) {
    free(buf);
    return NULL;
  }
  *offset += size;

  void *elem = decode_func(buf, (int)size);
  free(buf);
  return elem;
}

void save_entry(void *key, void *value, void *data) {
  SaveCtx *ctx = data;
  Bytes key_bytes = ctx->encode_key(key);
  Bytes val_bytes = ctx->encode_val(value);

  write_elem_buffered(ctx->io_ctx, key_bytes);
  write_elem_buffered(ctx->io_ctx, val_bytes);

  if (ctx->free_encoded_key)
    free(key_bytes.data);
  if (ctx->free_encoded_val)
    free(val_bytes.data);
}

static size_t get_or_create_value_index(GHashTable *value_to_index,
                                        GArray *values, void *value) {
  void *existing = g_hash_table_lookup(value_to_index, value);
  if (existing != NULL)
    return GPOINTER_TO_SIZE(existing) - 1;

  size_t idx = values->len;
  g_array_append_val(values, value);
  g_hash_table_insert(value_to_index, value, GSIZE_TO_POINTER(idx + 1));
  return idx;
}

static void collect_value_index(void *key, void *value, void *data) {
  (void)key;
  IndexedValueCtx *ctx = data;
  get_or_create_value_index(ctx->value_to_index, ctx->values, value);
}

static void save_indexed_entry(void *key, void *value, void *data) {
  SaveIndexedCtx *ctx = data;
  void *stored = g_hash_table_lookup(ctx->value_to_index, value);
  if (stored == NULL)
    return;

  size_t idx = GPOINTER_TO_SIZE(stored) - 1;
  Bytes key_bytes = ctx->encode_key(key);
  write_elem_buffered(ctx->io_ctx, key_bytes);
  write_elem_buffered(ctx->io_ctx, encode_size_elem(&idx));

  if (ctx->free_encoded_key)
    free(key_bytes.data);
}

static void save_values_array(const char *path, GArray *values,
                              EncodeFunc encode_value) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0644);
  if (fd < 0)
    return;

  BufferedIOCtx *io_ctx = buffered_io_init(fd, BUFFER_SIZE);

  int n = values->len;
  buffered_write(io_ctx, &n, sizeof(n));

  for (int i = 0; i < values->len; i++) {
    void *value = g_array_index(values, void *, i);
    write_elem_buffered(io_ctx, encode_value(value));
  }

  flush_write_buffer(io_ctx);
  buffered_io_free(io_ctx);
  close(fd);
}

static GArray *load_values_array(const char *path, DecodeFunc decode_value) {
  int fd = open(path, O_RDONLY | O_CREAT, 0644);
  GArray *values = g_array_new(FALSE, FALSE, sizeof(gpointer));
  if (fd < 0)
    return values;

  BufferedIOCtx *io_ctx = buffered_io_init(fd, BUFFER_SIZE);

  int n = 0;
  if (!buffered_read(io_ctx, &n, sizeof(n))) {
    buffered_io_free(io_ctx);
    close(fd);
    return values;
  }

  for (int i = 0; i < n; i++) {
    void *value = read_elem_buffered(io_ctx, decode_value);
    if (value == NULL)
      break;
    g_array_append_val(values, value);
  }

  buffered_io_free(io_ctx);
  close(fd);
  return values;
}

void ghash_save(const char *path, GHashTable *ht, EncodeFunc encode_key,
                EncodeFunc encode_val, gboolean free_encoded_key,
                gboolean free_encoded_val) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0644);
  if (fd < 0)
    return;

  BufferedIOCtx *io_ctx = buffered_io_init(fd, BUFFER_SIZE);

  int n = g_hash_table_size(ht);
  buffered_write(io_ctx, &n, sizeof(n));

  SaveCtx ctx = {io_ctx, encode_key, encode_val, free_encoded_key,
                 free_encoded_val};
  g_hash_table_foreach(ht, save_entry, &ctx);

  flush_write_buffer(io_ctx);
  buffered_io_free(io_ctx);
  close(fd);
}

GHashTable *ghash_load(const char *path, GHashFunc hash_fn, GEqualFunc equal_fn,
                       DecodeFunc decode_key, DecodeFunc decode_val,
                       GDestroyNotify free_key, GDestroyNotify free_val) {
  int fd = open(path, O_RDONLY | O_CREAT, 0644);
  GHashTable *ht = g_hash_table_new_full(hash_fn, equal_fn, free_key, free_val);
  if (fd < 0)
    return ht;

  BufferedIOCtx *io_ctx = buffered_io_init(fd, BUFFER_SIZE);

  int n = 0;
  if (buffered_read(io_ctx, &n, sizeof(n))) {
    for (int i = 0; i < n; i++) {
      void *key = read_elem_buffered(io_ctx, decode_key);
      void *val = read_elem_buffered(io_ctx, decode_val);
      if (key == NULL || val == NULL)
        break;
      g_hash_table_insert(ht, key, val);
    }
  }

  buffered_io_free(io_ctx);
  close(fd);
  return ht;
}

void ghash_save_indexed_pair(const char *table1_path, const char *table2_path,
                             const char *values_path, GHashTable *table1,
                             GHashTable *table2, IndexedTableSaveConfig config1,
                             IndexedTableSaveConfig config2,
                             EncodeFunc encode_value) {
  GHashTable *value_to_index = g_hash_table_new(g_direct_hash, g_direct_equal);
  GArray *values = g_array_new(FALSE, FALSE, sizeof(gpointer));
  IndexedValueCtx values_ctx = {value_to_index, values};

  g_hash_table_foreach(table1, collect_value_index, &values_ctx);
  g_hash_table_foreach(table2, collect_value_index, &values_ctx);

  save_values_array(values_path, values, encode_value);

  int fd1 = open(table1_path, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0644);
  if (fd1 >= 0) {
    BufferedIOCtx *io_ctx = buffered_io_init(fd1, BUFFER_SIZE);
    int n = g_hash_table_size(table1);
    buffered_write(io_ctx, &n, sizeof(n));
    SaveIndexedCtx ctx = {io_ctx, config1.encode_key, config1.free_encoded_key,
                          value_to_index};
    g_hash_table_foreach(table1, save_indexed_entry, &ctx);
    flush_write_buffer(io_ctx);
    buffered_io_free(io_ctx);
    close(fd1);
  }

  int fd2 = open(table2_path, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0644);
  if (fd2 >= 0) {
    BufferedIOCtx *io_ctx = buffered_io_init(fd2, BUFFER_SIZE);
    int n = g_hash_table_size(table2);
    buffered_write(io_ctx, &n, sizeof(n));
    SaveIndexedCtx ctx = {io_ctx, config2.encode_key, config2.free_encoded_key,
                          value_to_index};
    g_hash_table_foreach(table2, save_indexed_entry, &ctx);
    flush_write_buffer(io_ctx);
    buffered_io_free(io_ctx);
    close(fd2);
  }

  g_array_free(values, TRUE);
  g_hash_table_destroy(value_to_index);
}

IndexedPairTables
ghash_load_indexed_pair(const char *table1_path, const char *table2_path,
                        const char *values_path, IndexedTableLoadConfig config1,
                        IndexedTableLoadConfig config2, DecodeFunc decode_value,
                        GDestroyNotify free_value) {
  IndexedPairTables result = {
      g_hash_table_new_full(config1.hash_fn, config1.equal_fn, config1.free_key,
                            free_value),
      g_hash_table_new_full(config2.hash_fn, config2.equal_fn, config2.free_key,
                            free_value)};

  GArray *values = load_values_array(values_path, decode_value);
  GHashTable *table1_idx =
      ghash_load(table1_path, config1.hash_fn, config1.equal_fn,
                 config1.decode_key, decode_size_elem, NULL, g_free);
  GHashTable *table2_idx =
      ghash_load(table2_path, config2.hash_fn, config2.equal_fn,
                 config2.decode_key, decode_size_elem, NULL, g_free);

  GHashTableIter iter;
  void *key;
  void *value;

  g_hash_table_iter_init(&iter, table1_idx);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    size_t idx = *(size_t *)value;
    if (idx >= values->len) {
      if (config1.free_key != NULL)
        config1.free_key(key);
      continue;
    }

    void *shared_value = g_array_index(values, gpointer, idx);
    g_hash_table_insert(result.table1, key, shared_value);
  }

  g_hash_table_iter_init(&iter, table2_idx);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    size_t idx = *(size_t *)value;
    if (idx >= values->len) {
      if (config2.free_key != NULL)
        config2.free_key(key);
      continue;
    }

    void *shared_value = g_array_index(values, gpointer, idx);
    g_hash_table_insert(result.table2, key, shared_value);
  }

  g_array_free(values, TRUE);
  g_hash_table_destroy(table1_idx);
  g_hash_table_destroy(table2_idx);
  return result;
}

void gslist_save(const char *path, GSList *list, EncodeFunc encode_elem) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0644);
  if (fd < 0)
    return;

  BufferedIOCtx *io_ctx = buffered_io_init(fd, BUFFER_SIZE);

  int n = g_slist_length(list);
  buffered_write(io_ctx, &n, sizeof(n));

  for (GSList *l = list; l; l = l->next) {
    write_elem_buffered(io_ctx, encode_elem(l->data));
  }

  flush_write_buffer(io_ctx);
  buffered_io_free(io_ctx);
  close(fd);
}

GSList *gslist_load(const char *path, DecodeFunc decode_elem) {
  int fd = open(path, O_RDONLY | O_CREAT, 0644);
  if (fd < 0)
    return NULL;

  BufferedIOCtx *io_ctx = buffered_io_init(fd, BUFFER_SIZE);

  int n = 0;
  if (!buffered_read(io_ctx, &n, sizeof(n))) {
    buffered_io_free(io_ctx);
    close(fd);
    return NULL;
  }

  GSList *list = NULL;
  for (int i = 0; i < n; i++) {
    void *elem = read_elem_buffered(io_ctx, decode_elem);
    if (elem == NULL)
      break;
    list = g_slist_append(list, elem);
  }

  buffered_io_free(io_ctx);
  close(fd);
  return list;
}
