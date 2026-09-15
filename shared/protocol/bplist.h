#ifndef AIRPLAY_BPLIST_H
#define AIRPLAY_BPLIST_H
#include <stddef.h>
#include <stdint.h>

#define BPLIST_NONE UINT32_MAX

/* Bounded Apple binary-plist reader and writer used by AirPlay 2 control messages. */

/* Read-only view of one validated binary-plist object table. */
typedef struct
{
    const uint8_t *data;
    size_t size, table;
    uint32_t count, root;
    unsigned offset_size, ref_size;
} bplist_t;

/* Validate a binary plist and initialize a reader view over caller-owned bytes. */
/**
 * @brief bplist_open.
 * @param p Parameter named p.
 * @param data Parameter named data.
 * @param size Parameter named size.
 * @return Function result.
 */
int bplist_open(bplist_t *p, const uint8_t *data, size_t size);
/* Find a value reference by key in a dictionary, or BPLIST_NONE. */
/**
 * @brief bplist_get.
 * @param p Parameter named p.
 * @param dict Parameter named dict.
 * @param key Parameter named key.
 * @return Function result.
 */
uint32_t bplist_get(const bplist_t *p, uint32_t dict, const char *key);
/* Return the referenced array element, or BPLIST_NONE. */
/**
 * @brief bplist_at.
 * @param p Parameter named p.
 * @param array Parameter named array.
 * @param index Parameter named index.
 * @return Function result.
 */
uint32_t bplist_at(const bplist_t *p, uint32_t array, size_t index);
/* Return the number of children in an array or dictionary. */
/**
 * @brief bplist_count.
 * @param p Parameter named p.
 * @param object Parameter named object.
 * @return Function result.
 */
size_t bplist_count(const bplist_t *p, uint32_t object);
/* Read an unsigned integer object. */
/**
 * @brief bplist_uint.
 * @param p Parameter named p.
 * @param object Parameter named object.
 * @param value Parameter named value.
 * @return Function result.
 */
int bplist_uint(const bplist_t *p, uint32_t object, uint64_t *value);
/* Read a floating-point object. */
/**
 * @brief bplist_real.
 * @param p Parameter named p.
 * @param object Parameter named object.
 * @param value Parameter named value.
 * @return Function result.
 */
int bplist_real(const bplist_t *p, uint32_t object, double *value);
/* Return a borrowed view of a data or string object's bytes. */
/**
 * @brief bplist_bytes.
 * @param p Parameter named p.
 * @param object Parameter named object.
 * @param data Parameter named data.
 * @param size Parameter named size.
 * @return Function result.
 */
int bplist_bytes(const bplist_t *p, uint32_t object, const uint8_t **data, size_t *size);

/* Bounded writer; references returned by add functions belong to this writer. */
typedef struct
{
    uint8_t *data;
    size_t capacity, used;
    uint32_t offsets[128], count;
    int failed;
} bplist_writer_t;
/* Initialize a writer over caller-provided bounded storage. */
/**
 * @brief bplist_writer_init.
 * @param w Parameter named w.
 * @param data Parameter named data.
 * @param capacity Parameter named capacity.
 */
void bplist_writer_init(bplist_writer_t *w, uint8_t *data, size_t capacity);
/* Append an ASCII string object and return its writer-local reference. */
/**
 * @brief bplist_add_string.
 * @param w Parameter named w.
 * @param text Parameter named text.
 * @return Function result.
 */
uint32_t bplist_add_string(bplist_writer_t *w, const char *text);
/* Append an unsigned integer object and return its reference. */
/**
 * @brief bplist_add_uint.
 * @param w Parameter named w.
 * @param value Parameter named value.
 * @return Function result.
 */
uint32_t bplist_add_uint(bplist_writer_t *w, uint64_t value);
/* Append a data object and return its reference. */
/**
 * @brief bplist_add_data.
 * @param w Parameter named w.
 * @param data Parameter named data.
 * @param size Parameter named size.
 * @return Function result.
 */
uint32_t bplist_add_data(bplist_writer_t *w, const uint8_t *data, size_t size);
/* Append an array containing count existing references. */
/**
 * @brief bplist_add_array.
 * @param w Parameter named w.
 * @param refs Parameter named refs.
 * @param count Parameter named count.
 * @return Function result.
 */
uint32_t bplist_add_array(bplist_writer_t *w, const uint32_t *refs, size_t count);
/* refs contains alternating key/value references. */
/**
 * @brief bplist_add_dict.
 * @param w Parameter named w.
 * @param refs Parameter named refs.
 * @param pairs Parameter named pairs.
 * @return Function result.
 */
uint32_t bplist_add_dict(bplist_writer_t *w, const uint32_t *refs, size_t pairs);
/* Write the offset table and trailer, returning the finished size or zero. */
/**
 * @brief bplist_finish.
 * @param w Parameter named w.
 * @param root Parameter named root.
 * @return Function result.
 */
size_t bplist_finish(bplist_writer_t *w, uint32_t root);
#endif
