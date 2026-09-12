#include "bplist.h"
#include <string.h>

static uint64_t read_be(const uint8_t *p, unsigned n)
{
    uint64_t v = 0;
    while (n--) v = (v << 8) | *p++;
    return v;
}

int bplist_open(bplist_t *p, const uint8_t *data, size_t size)
{
    if (!p || !data || size < 40 || memcmp(data, "bplist00", 8))
        return -1;
    memset(p, 0, sizeof(*p));
    const uint8_t *tail = data + size - 32;
    uint64_t count = read_be(tail + 8, 8), root = read_be(tail + 16, 8);
    uint64_t table = read_be(tail + 24, 8);
    if (!tail[6] || tail[6] > 8 || !tail[7] || tail[7] > 8 ||
        !count || count > UINT32_MAX || root >= count ||
        table < 8 || table > size - 32 || count > (size - 32 - table) / tail[6])
        return -1;
    p->data = data; p->size = size; p->table = (size_t)table;
    p->count = (uint32_t)count; p->root = (uint32_t)root;
    p->offset_size = tail[6]; p->ref_size = tail[7];
    return 0;
}

static int object(const bplist_t *p, uint32_t ref, unsigned *type,
                  size_t *start, size_t *count)
{
    if (!p || ref >= p->count) return -1;
    uint64_t pos = read_be(p->data + p->table + (size_t)ref * p->offset_size, p->offset_size);
    if (pos < 8 || pos >= p->table) return -1;
    unsigned marker = p->data[pos++];
    *type = marker >> 4;
    uint64_t n = marker & 15;
    if (*type == 1 || *type == 2) {
        if (n > 3) return -1;
        n = UINT64_C(1) << n;
    } else if (n == 15) {
        if (pos >= p->table || (p->data[pos] >> 4) != 1 || (p->data[pos] & 15) > 3)
            return -1;
        unsigned bytes = 1u << (p->data[pos++] & 15);
        if (bytes > p->table - pos) return -1;
        n = read_be(p->data + pos, bytes);
        pos += bytes;
    }
    size_t width = (*type == 6) ? 2 : (*type == 10) ? p->ref_size :
                   (*type == 13) ? 2 * p->ref_size : 1;
    if (n > (p->table - pos) / width) return -1;
    *start = (size_t)pos; *count = (size_t)n;
    return 0;
}

static uint32_t reference(const bplist_t *p, size_t pos)
{
    uint64_t ref = read_be(p->data + pos, p->ref_size);
    return ref < p->count ? (uint32_t)ref : BPLIST_NONE;
}

uint32_t bplist_get(const bplist_t *p, uint32_t dict, const char *key)
{
    unsigned type; size_t pos, n;
    if (!key || object(p, dict, &type, &pos, &n) || type != 13) return BPLIST_NONE;
    for (size_t i = 0; i < n; ++i) {
        size_t kpos, kn; unsigned kt;
        uint32_t k = reference(p, pos + i * p->ref_size);
        if (object(p, k, &kt, &kpos, &kn) || kt != 5) continue;
        if (kn == strlen(key) && !memcmp(p->data + kpos, key, kn))
            return reference(p, pos + (n + i) * p->ref_size);
    }
    return BPLIST_NONE;
}

uint32_t bplist_at(const bplist_t *p, uint32_t array, size_t index)
{
    unsigned type; size_t pos, n;
    if (object(p, array, &type, &pos, &n) || type != 10 || index >= n) return BPLIST_NONE;
    return reference(p, pos + index * p->ref_size);
}

size_t bplist_count(const bplist_t *p, uint32_t ref)
{
    unsigned type; size_t pos, n;
    return object(p, ref, &type, &pos, &n) || (type != 10 && type != 13) ? 0 : n;
}

int bplist_uint(const bplist_t *p, uint32_t ref, uint64_t *value)
{
    unsigned type; size_t pos, n;
    if (!value || object(p, ref, &type, &pos, &n) || type != 1) return -1;
    *value = read_be(p->data + pos, (unsigned)n);
    return 0;
}

int bplist_real(const bplist_t *p, uint32_t ref, double *value)
{
    unsigned type; size_t pos, n;
    if (!value || object(p, ref, &type, &pos, &n)) return -1;
    uint64_t bits = read_be(p->data + pos, (unsigned)n);
    if (type == 1) { *value = (double)bits; return 0; }
    if (type != 2) return -1;
    if (n == 8) memcpy(value, &bits, 8);
    else if (n == 4) { uint32_t small = (uint32_t)bits; float f; memcpy(&f, &small, 4); *value = f; }
    else return -1;
    return 0;
}

int bplist_bytes(const bplist_t *p, uint32_t ref, const uint8_t **data, size_t *size)
{
    unsigned type; size_t pos, n;
    if (!data || !size || object(p, ref, &type, &pos, &n) || (type != 4 && type != 5))
        return -1;
    *data = p->data + pos; *size = n;
    return 0;
}

static void append(bplist_writer_t *w, uint64_t v, unsigned bytes)
{
    if (w->failed || bytes > w->capacity - w->used) { w->failed = 1; return; }
    for (unsigned i = bytes; i; --i) w->data[w->used++] = (uint8_t)(v >> ((i - 1) * 8));
}

void bplist_writer_init(bplist_writer_t *w, uint8_t *data, size_t capacity)
{
    memset(w, 0, sizeof(*w)); w->data = data; w->capacity = capacity;
    if (!data || capacity < 40) { w->failed = 1; return; }
    memcpy(data, "bplist00", 8); w->used = 8;
}

static uint32_t start(bplist_writer_t *w, unsigned type, size_t n)
{
    if (w->failed || w->count == 128 || w->used > UINT32_MAX) { w->failed = 1; return BPLIST_NONE; }
    uint32_t ref = w->count++;
    w->offsets[ref] = (uint32_t)w->used;
    append(w, (type << 4) | (n < 15 ? n : 15), 1);
    if (n >= 15) { append(w, 0x13, 1); append(w, n, 8); }
    return ref;
}

static uint32_t bytes(bplist_writer_t *w, unsigned type, const uint8_t *data, size_t n)
{
    uint32_t ref = start(w, type, n);
    if (w->failed || n > w->capacity - w->used || (n && !data)) { w->failed = 1; return BPLIST_NONE; }
    if (n) memcpy(w->data + w->used, data, n);
    w->used += n;
    return ref;
}

uint32_t bplist_add_string(bplist_writer_t *w, const char *text)
{
    return bytes(w, 5, (const uint8_t *)text, strlen(text));
}
uint32_t bplist_add_data(bplist_writer_t *w, const uint8_t *data, size_t n) { return bytes(w, 4, data, n); }
uint32_t bplist_add_uint(bplist_writer_t *w, uint64_t value)
{
    uint32_t ref = start(w, 1, 3); append(w, value, 8); return ref;
}
uint32_t bplist_add_array(bplist_writer_t *w, const uint32_t *refs, size_t n)
{
    uint32_t ref = start(w, 10, n);
    for (size_t i = 0; i < n; ++i) {
        if (refs[i] >= ref) w->failed = 1;
        append(w, refs[i], 1);
    }
    return ref;
}
uint32_t bplist_add_dict(bplist_writer_t *w, const uint32_t *refs, size_t n)
{
    uint32_t ref = start(w, 13, n);
    for (unsigned side = 0; side < 2; ++side)
        for (size_t i = 0; i < n; ++i) {
            if (refs[i * 2 + side] >= ref) w->failed = 1;
            append(w, refs[i * 2 + side], 1);
        }
    return ref;
}
size_t bplist_finish(bplist_writer_t *w, uint32_t root)
{
    if (root >= w->count || w->failed) return 0;
    size_t table = w->used;
    for (uint32_t i = 0; i < w->count; ++i) append(w, w->offsets[i], 4);
    append(w, 0, 6); append(w, 4, 1); append(w, 1, 1);
    append(w, w->count, 8); append(w, root, 8); append(w, table, 8);
    return w->failed ? 0 : w->used;
}
