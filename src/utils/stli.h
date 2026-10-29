#ifndef _STATIC_LIST_UTILS_H_
#define _STATIC_LIST_UTILS_H_

#include "utils/defines.h"
#include <stddef.h>
#include <webgpu/webgpu.h>

typedef enum {
  StaticListStatus_Success,
  StaticListStatus_UnfoundEntry,
  StaticListStatus_MaxCapacity,
  StaticListStatus_OutOfRange,
  StaticListStatus_EmptyList,
  StaticListStatus_DuplicateEntry,
} StaticListStatus;

#define STATIC_LIST(Type, Capacity)                                            \
  struct {                                                                     \
    Type entries[Capacity];                                                    \
    size_t count;                                                              \
    size_t capacity;                                                           \
  }

EXTERN_C_BEGIN

StaticListStatus stli_create(size_t *, size_t *, const size_t, const char *);

StaticListStatus stli_insert(void *, size_t, size_t *, size_t, const void *,
                             const char *);
StaticListStatus stli_remove(void *, size_t *, size_t, const void *,
                             const char *);

StaticListStatus stli_shift(void *, size_t *, size_t, const char *);

StaticListStatus stli_empty(void *, size_t *, size_t, const char *);

StaticListStatus stli_remove_at_index(void *, size_t *, size_t, size_t,
                                      const char *);

void *stli_new_entry(void *, size_t, size_t *, size_t, const char *);

void *stli_find(void *, size_t, size_t, const void *, size_t *, const char *);

EXTERN_C_END

/* ---------- helpers: the only place that knows the argument order ----------
 */

#define STLI__TSIZE(l) sizeof((l).entries[0])
#define STLI__CAP(l) (sizeof((l).entries) / sizeof((l).entries[0]))
#define STLI__LABEL(l) #l

/* ---------- wrappers: pass only the list ---------- */

/* stli_create(size_t *capacity, size_t *count, size_t capacity_value, label) */
#define STLI_CREATE(l)                                                         \
  stli_create(&(l).capacity, &(l).count, STLI__CAP(l), STLI__LABEL(l))

/* stli_insert(entries, capacity, count*, type_size, item, label) */
#define STLI_INSERT(l, item_ptr)                                               \
  stli_insert((l).entries, STLI__CAP(l), &(l).count, STLI__TSIZE(l),           \
              (item_ptr), STLI__LABEL(l))

/* stli_remove(entries, count*, type_size, item, label) */
#define STLI_REMOVE(l, item_ptr)                                               \
  stli_remove((l).entries, &(l).count, STLI__TSIZE(l), (item_ptr),             \
              STLI__LABEL(l))

/* stli_shift(entries, count*, type_size, label) */
#define STLI_SHIFT(l)                                                          \
  stli_shift((l).entries, &(l).count, STLI__TSIZE(l), STLI__LABEL(l))

#define STLI_EMPTY(l)                                                          \
  stli_empty((l).entries, &(l).count, STLI__TSIZE(l), STLI__LABEL(l))

/* stli_remove_at_index(entries, count*, type_size, index, label) */
#define STLI_REMOVE_AT(l, index)                                               \
  stli_remove_at_index((l).entries, &(l).count, STLI__TSIZE(l), (index),       \
                       STLI__LABEL(l))

/* typed pointer to the new slot (NULL on failure) */
#define STLI_NEW_ENTRY(l)                                                      \
  ((__typeof__(&(l).entries[0]))stli_new_entry(                                \
      (l).entries, STLI__CAP(l), &(l).count, STLI__TSIZE(l), STLI__LABEL(l)))

/* stli_find(entries, count, type_size, item, size_t *out_index, label)
   returns a typed pointer to the match, index goes to out_index (lvalue) */
#define STLI_FIND(l, item_ptr, out_index)                                      \
  ((__typeof__(&(l).entries[0]))stli_find((l).entries, (l).count,              \
                                          STLI__TSIZE(l), (item_ptr),          \
                                          &(out_index), STLI__LABEL(l)))

#endif
