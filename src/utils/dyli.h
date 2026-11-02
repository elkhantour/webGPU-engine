#ifndef _DYNAMIC_LIST_H_
#define _DYNAMIC_LIST_H_

#include "defines.h"
#include <stddef.h>
#include <stdio.h>

#define DYLI_INVALID_INDEX SIZE_MAX
#define DYLI_DEFAULT_EXPAND_THRESHOLD 1

typedef enum {
  DynamicListStatus_Success,
  DynamicListStatus_AllocFail,
  DynamicListStatus_UnfoundEntry,
  DynamicListStatus_UndefError,
  DynamicListStatus_OutOfBound,
  DynamicListStatus_InvalidArgument,
  DynamicListStatus_NotInit,
} DynamicListStatus;

typedef struct {
  void *entries;
  size_t capacity;
  size_t count;
  size_t type_size;
  const char *label;
} DynamicList;

#define DYNAMIC_LIST(Type)                                                     \
  struct {                                                                     \
    Type *entries;                                                             \
    size_t count;                                                              \
    size_t capacity;                                                           \
  }

EXTERN_C_BEGIN

DynamicListStatus dyli_create(void **, size_t *, size_t *, size_t, size_t,
                              const char *);
DynamicListStatus dyli_expand(void **, size_t *, size_t *, size_t, size_t,
                              const char *);
DynamicListStatus dyli_insert(void **, size_t *, size_t *, size_t, void *,
                              size_t, const char *);

DynamicListStatus dyli_append(const void *, const size_t, void **, size_t *,
                              size_t *, size_t, const char *);

DynamicListStatus dyli_replace(const void *, const size_t, void **, size_t *,
                               size_t *, size_t, const char *);

DynamicListStatus dyli_remove(void *, size_t *, size_t, void *, const char *);

DynamicListStatus dyli_remove_at_index(void *, size_t *, size_t, const size_t,
                                       const char *);

DynamicListStatus dyli_empty(void *, size_t *, size_t);

DynamicListStatus dyli_free(void **, size_t *, size_t *);

void *dyli_new_entry(void **, size_t *, size_t *, size_t, const char *);

DynamicListStatus dyli_clone(const void *, const size_t, void **, size_t *,
                             size_t *, size_t, const char *);

DynamicListStatus dyli_find_index(const void *, size_t, size_t, const void *,
                                  size_t *);

EXTERN_C_END

/* ---------- helpers: the only place that knows the argument order ----------
 */

/* entries, count, capacity  -> adjust order here if your functions differ */
#define DYLI__PTRS(l) (void **)&(l).entries, &(l).count, &(l).capacity
#define DYLI__TSIZE(l) sizeof(*(l).entries)
#define DYLI__LABEL(l) #l

/* ---------- wrappers: pass only the list ---------- */

/* dyli_create(void **entries, size_t *count, size_t *capacity,
               size_t type_size, size_t initial_capacity, const char *label) */
#define DYLI_CREATE(l, initial_cap)                                            \
  dyli_create(DYLI__PTRS(l), DYLI__TSIZE(l), (initial_cap), DYLI__LABEL(l))

#define DYLI_EXPAND(l, by)                                                     \
  dyli_expand(DYLI__PTRS(l), DYLI__TSIZE(l), (by), DYLI__LABEL(l))

/* insert: item is a pointer to the element */
#define DYLI_INSERT(l, item_ptr, index)                                        \
  dyli_insert(DYLI__PTRS(l), DYLI__TSIZE(l), (item_ptr), (index),              \
              DYLI__LABEL(l))

/* append: item_ptr + how many items it points to */
#define DYLI_APPEND(l, item_ptr, n)                                            \
  dyli_append((item_ptr), (n), DYLI__PTRS(l), DYLI__TSIZE(l), DYLI__LABEL(l))

#define DYLI_REPLACE(l, item_ptr, n)                                           \
  dyli_replace((item_ptr), (n), DYLI__PTRS(l), DYLI__TSIZE(l), DYLI__LABEL(l))

/* remove: (void *entries, size_t *count, size_t type_size, void *item, label)
 */
#define DYLI_REMOVE(l, item_ptr)                                               \
  dyli_remove((l).entries, &(l).count, DYLI__TSIZE(l), (item_ptr),             \
              DYLI__LABEL(l))

#define DYLI_REMOVE_AT(l, index)                                               \
  dyli_remove_at_index((l).entries, &(l).count, DYLI__TSIZE(l), (index),       \
                       DYLI__LABEL(l))

#define DYLI_EMPTY(l) dyli_empty((l).entries, &(l).count, DYLI__TSIZE(l))

#define DYLI_FREE(l) dyli_free((void **)&(l).entries, &(l).capacity, &(l).count)

/* returns a pointer to the new (uninitialized) slot, cast to the element type
 */
#define DYLI_NEW_ENTRY(l)                                                      \
  ((__typeof__((l).entries))dyli_new_entry(DYLI__PTRS(l), DYLI__TSIZE(l),      \
                                           DYLI__LABEL(l)))

/* clone src -> dst: dyli_clone(src_entries, src_count, &dst...) */
#define DYLI_CLONE(dst, src)                                                   \
  dyli_clone((src).entries, (src).count, DYLI__PTRS(dst), DYLI__TSIZE(dst),    \
             DYLI__LABEL(dst))

#endif
