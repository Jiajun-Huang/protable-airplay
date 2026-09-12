#ifndef AIRPLAY_BPLIST_H
#define AIRPLAY_BPLIST_H
#include <stddef.h>
#include <stdint.h>

#define BPLIST_NONE UINT32_MAX
typedef struct {
    const uint8_t *data;
    size_t size, table;
    uint32_t count, root;
    unsigned offset_size, ref_size;
} bplist_t;

int bplist_open(bplist_t *p, const uint8_t *data, size_t size);
uint32_t bplist_get(const bplist_t *p, uint32_t dict, const char *key);
uint32_t bplist_at(const bplist_t *p, uint32_t array, size_t index);
size_t bplist_count(const bplist_t *p, uint32_t object);
int bplist_uint(const bplist_t *p, uint32_t object, uint64_t *value);
int bplist_real(const bplist_t *p, uint32_t object, double *value);
int bplist_bytes(const bplist_t *p, uint32_t object, const uint8_t **data, size_t *size);

/* Bounded writer; references returned by add functions belong to this writer. */
typedef struct {
    uint8_t *data;
    size_t capacity, used;
    uint32_t offsets[128], count;
    int failed;
} bplist_writer_t;
void bplist_writer_init(bplist_writer_t *w, uint8_t *data, size_t capacity);
uint32_t bplist_add_string(bplist_writer_t *w, const char *text);
uint32_t bplist_add_uint(bplist_writer_t *w, uint64_t value);
uint32_t bplist_add_data(bplist_writer_t *w, const uint8_t *data, size_t size);
uint32_t bplist_add_array(bplist_writer_t *w, const uint32_t *refs, size_t count);
/* refs contains alternating key/value references. */
uint32_t bplist_add_dict(bplist_writer_t *w, const uint32_t *refs, size_t pairs);
size_t bplist_finish(bplist_writer_t *w, uint32_t root);
#endif
