/*  Header-only CBOR (RFC 8949) codec for ANSI C89, arena allocated.  Written
    by Kamila Szewczyk (k@iczelia.net), released to the public domain (0BSD).

      #define MCBOR_IMPLEMENTATION
      #include "minicbor.h"

    Optional knobs:
      MCBOR_ARENA_BLOCK_SIZE   arena block size (4096)
      MCBOR_SMALL_ALLOC_LIMIT  arena/heap cutoff (1024)
      MCBOR_MAX_DEPTH          nesting limit (128)
      MCBOR_MALLOC, MCBOR_FREE, MCBOR_REALLOC

    MCBOR_FREE has to accept NULL and MCBOR_REALLOC a NULL pointer, the way
    free and realloc do.  Freestanding save for the diagnostic printer, which
    wants sprintf and sscanf.  IEEE-754 binary32/binary64 are assumed and
    checked at compile time.  */

#ifndef MINICBOR_H
#define MINICBOR_H

#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MCBOR_ARENA_BLOCK_SIZE
#define MCBOR_ARENA_BLOCK_SIZE 4096
#endif
#ifndef MCBOR_SMALL_ALLOC_LIMIT
#define MCBOR_SMALL_ALLOC_LIMIT 1024
#endif
#ifndef MCBOR_MAX_DEPTH
#define MCBOR_MAX_DEPTH 128
#endif
#if !defined(MCBOR_MALLOC) || !defined(MCBOR_FREE) || !defined(MCBOR_REALLOC)
#include <stdlib.h>
#endif
#ifndef MCBOR_MALLOC
#define MCBOR_MALLOC malloc
#endif
#ifndef MCBOR_FREE
#define MCBOR_FREE free
#endif
#ifndef MCBOR_REALLOC
#define MCBOR_REALLOC realloc
#endif

#if defined(_MSC_VER) || defined(__BORLANDC__)
  typedef unsigned __int64 mcbor_uint64_t;  typedef __int64 mcbor_int64_t;
#else
  typedef unsigned long long mcbor_uint64_t;  typedef long long mcbor_int64_t;
#endif
typedef unsigned char mcbor_uint8_t;  typedef unsigned short mcbor_uint16_t;
typedef unsigned int mcbor_uint32_t;  typedef int mcbor_int32_t;

typedef char mcbor_assert_u64[sizeof(mcbor_uint64_t) == 8 ? 1 : -1];
typedef char mcbor_assert_u32[sizeof(mcbor_uint32_t) == 4 ? 1 : -1];
typedef char mcbor_assert_f32[sizeof(float) == 4 ? 1 : -1];
typedef char mcbor_assert_f64[sizeof(double) == 8 ? 1 : -1];

#define MCBOR_OK                0
#define MCBOR_ERR_TRUNCATED     -1   /* Unexpected end of input */
#define MCBOR_ERR_MALFORMED     -2   /* Malformed CBOR */
#define MCBOR_ERR_RESERVED      -3   /* Reserved additional info value */
#define MCBOR_ERR_BREAK_OUTSIDE -4   /* Break outside indefinite */
#define MCBOR_ERR_INVALID_UTF8  -5   /* Invalid UTF-8 in text string */
#define MCBOR_ERR_OVERFLOW      -6   /* Size computation out of range */
#define MCBOR_ERR_NO_MEMORY     -7   /* Allocation failed */
#define MCBOR_ERR_MAP_KEY       -8   /* Break after map key (no value) */
#define MCBOR_ERR_BUFFER_FULL   -9   /* Output buffer full */
#define MCBOR_ERR_INVALID_ARG   -10  /* Invalid argument */
#define MCBOR_ERR_DEPTH         -11  /* Nesting limit exceeded */

/*  Major types.  */
typedef enum {
  MCBOR_TYPE_UINT, MCBOR_TYPE_NEGINT, MCBOR_TYPE_BYTES, MCBOR_TYPE_TEXT,
  MCBOR_TYPE_ARRAY, MCBOR_TYPE_MAP, MCBOR_TYPE_TAG, MCBOR_TYPE_SIMPLE,
  MCBOR_TYPE_FALSE, MCBOR_TYPE_TRUE, MCBOR_TYPE_NULL, MCBOR_TYPE_UNDEFINED,
  MCBOR_TYPE_FLOAT16, MCBOR_TYPE_FLOAT32, MCBOR_TYPE_FLOAT64, MCBOR_TYPE_BREAK
} mcbor_type_t;

#define MCBOR_FLAG_NONE      0x00
#define MCBOR_FLAG_MANAGED   0x01  /* Struct belongs to an arena */
#define MCBOR_FLAG_STOLEN    0x02  /* Payload is the caller's to free */
#define MCBOR_FLAG_HEAP      0x04  /* Struct came off the arena heap list */
#define MCBOR_FLAG_ARENA     0x08  /* Struct came out of an arena block */
#define MCBOR_FLAG_DATA_HEAP 0x10  /* Payload came off the arena heap list */

#define MCBOR_DECODE_STRICT        0x01  /* Require shortest-form arguments */
#define MCBOR_DECODE_VALIDATE_UTF8 0x02  /* Validate UTF-8 strings */

#define MCBOR_ENCODE_CANONICAL   0x01  /* Use canonical/deterministic encoding */
#define MCBOR_ENCODE_PREFERRED   0x02  /* Shrink floats to the shortest form */
#define MCBOR_ENCODE_ALLOW_INDEF 0x04  /* Allow indefinite length encoding */

typedef struct mcbor_value mcbor_value_t;  typedef struct mcbor_arena mcbor_arena_t;
typedef struct mcbor_arena_block mcbor_arena_block_t;
typedef struct mcbor_decoder mcbor_decoder_t;  typedef struct mcbor_encoder mcbor_encoder_t;

struct mcbor_arena_block {
  unsigned char * data;  size_t size, used;  mcbor_arena_block_t * next;
};

struct mcbor_arena {
  mcbor_arena_block_t * head, * current;  size_t total_allocated, block_size;
  void * (* malloc_fn)(size_t);  void (* free_fn)(void *);
  void * (* realloc_fn)(void *, size_t);
  void ** heap_ptrs;  size_t heap_count, heap_capacity;  /*  Oversized ones.  */
};

struct mcbor_value {
  mcbor_type_t type;  mcbor_uint8_t flags;  mcbor_arena_t * arena;
  union {
    mcbor_uint64_t uint_val;
    mcbor_uint64_t negint_val;  /*  n, where the value is -1-n.  */
    struct { unsigned char * data;  size_t len; } bytes;  /*  BYTES, TEXT.  */
    struct { mcbor_value_t * items;  size_t count, capacity; } array;
    /*  Keys and values share one block: values == keys + capacity.  */
    struct { mcbor_value_t * keys, * values;  size_t count, capacity; } map;
    struct { mcbor_uint64_t tag_number;  mcbor_value_t * content; } tag;
    double float_val;  mcbor_uint8_t simple_val;
  } data;
};

struct mcbor_decoder {
  const unsigned char * data;  size_t len, pos;
  mcbor_arena_t * arena;  unsigned int flags;
  int error_code;  const char * error_msg;  size_t error_pos;
};

struct mcbor_encoder {
  unsigned char * buffer;  size_t capacity, len;
  mcbor_arena_t * arena;  /*  Allocator hooks for a dynamic buffer, if any.  */
  unsigned int flags;  int error_code, dynamic;
};

void mcbor_arena_init(mcbor_arena_t * arena);
void mcbor_arena_init_custom(mcbor_arena_t * arena, void * (* malloc_fn)(size_t),
                             void (* free_fn)(void *),
                             void * (* realloc_fn)(void *, size_t));
void * mcbor_arena_alloc(mcbor_arena_t * arena, size_t size, mcbor_uint8_t * flags);
void mcbor_arena_destroy(mcbor_arena_t * arena);
void mcbor_arena_reset(mcbor_arena_t * arena);

int mcbor_steal(mcbor_value_t * value, mcbor_arena_t * arena);
int mcbor_steal_deep(mcbor_value_t * value, mcbor_arena_t * arena);
void mcbor_free_stolen(mcbor_value_t * value);
int mcbor_is_stolen(const mcbor_value_t * value);
int mcbor_is_heap(const mcbor_value_t * value);

void mcbor_decoder_init(mcbor_decoder_t * dec, const unsigned char * data,
                        size_t len, mcbor_arena_t * arena);
void mcbor_decoder_set_flags(mcbor_decoder_t * dec, unsigned int flags);
int mcbor_decode(mcbor_decoder_t * dec, mcbor_value_t * out);
int mcbor_decode_stream(mcbor_decoder_t * dec,
                        int (* callback)(mcbor_value_t * value, void * ctx), void * ctx);
size_t mcbor_decoder_remaining(const mcbor_decoder_t * dec);
int mcbor_decoder_error(const mcbor_decoder_t * dec);
const char * mcbor_decoder_error_msg(const mcbor_decoder_t * dec);
size_t mcbor_decoder_error_pos(const mcbor_decoder_t * dec);

void mcbor_encoder_init(mcbor_encoder_t * enc, unsigned char * buffer, size_t capacity);
void mcbor_encoder_init_dynamic(mcbor_encoder_t * enc, mcbor_arena_t * arena);
void mcbor_encoder_set_flags(mcbor_encoder_t * enc, unsigned int flags);
int mcbor_encode(mcbor_encoder_t * enc, const mcbor_value_t * value);
int mcbor_encode_uint(mcbor_encoder_t * enc, mcbor_uint64_t value);
int mcbor_encode_negint(mcbor_encoder_t * enc, mcbor_uint64_t n);
int mcbor_encode_int(mcbor_encoder_t * enc, mcbor_int64_t value);
int mcbor_encode_bytes(mcbor_encoder_t * enc, const unsigned char * data, size_t len);
int mcbor_encode_text(mcbor_encoder_t * enc, const char * str, size_t len);
int mcbor_encode_text_cstr(mcbor_encoder_t * enc, const char * str);
int mcbor_encode_float16(mcbor_encoder_t * enc, double value);
int mcbor_encode_float32(mcbor_encoder_t * enc, float value);
int mcbor_encode_float64(mcbor_encoder_t * enc, double value);
int mcbor_encode_float(mcbor_encoder_t * enc, double value);
int mcbor_encode_bool(mcbor_encoder_t * enc, int value);
int mcbor_encode_null(mcbor_encoder_t * enc);
int mcbor_encode_undefined(mcbor_encoder_t * enc);
int mcbor_encode_simple(mcbor_encoder_t * enc, mcbor_uint8_t value);
int mcbor_encode_array_start(mcbor_encoder_t * enc, size_t count);
int mcbor_encode_array_start_indef(mcbor_encoder_t * enc);
int mcbor_encode_map_start(mcbor_encoder_t * enc, size_t count);
int mcbor_encode_map_start_indef(mcbor_encoder_t * enc);
int mcbor_encode_tag(mcbor_encoder_t * enc, mcbor_uint64_t tag_number);
int mcbor_encode_break(mcbor_encoder_t * enc);
const unsigned char * mcbor_encoder_data(const mcbor_encoder_t * enc);
size_t mcbor_encoder_len(const mcbor_encoder_t * enc);
int mcbor_encoder_error(const mcbor_encoder_t * enc);
void mcbor_encoder_destroy(mcbor_encoder_t * enc);

mcbor_value_t * mcbor_uint_new(mcbor_arena_t * arena, mcbor_uint64_t value);
mcbor_value_t * mcbor_negint_new(mcbor_arena_t * arena, mcbor_uint64_t n);
mcbor_value_t * mcbor_int_new(mcbor_arena_t * arena, mcbor_int64_t value);
mcbor_value_t * mcbor_bytes_new(mcbor_arena_t * arena, const unsigned char * data, size_t len);
mcbor_value_t * mcbor_text_new(mcbor_arena_t * arena, const char * str, size_t len);
mcbor_value_t * mcbor_text_cstr_new(mcbor_arena_t * arena, const char * str);
mcbor_value_t * mcbor_array_new(mcbor_arena_t * arena, size_t capacity);
mcbor_value_t * mcbor_map_new(mcbor_arena_t * arena, size_t capacity);
mcbor_value_t * mcbor_tag_new(mcbor_arena_t * arena, mcbor_uint64_t tag_number,
                              mcbor_value_t * content);
mcbor_value_t * mcbor_float_new(mcbor_arena_t * arena, double value);
mcbor_value_t * mcbor_bool_new(mcbor_arena_t * arena, int value);
mcbor_value_t * mcbor_null_new(mcbor_arena_t * arena);
mcbor_value_t * mcbor_undefined_new(mcbor_arena_t * arena);
mcbor_value_t * mcbor_simple_new(mcbor_arena_t * arena, mcbor_uint8_t value);
int mcbor_array_append(mcbor_arena_t * arena, mcbor_value_t * arr, mcbor_value_t * item);
int mcbor_map_put(mcbor_arena_t * arena, mcbor_value_t * map, mcbor_value_t * key,
                  mcbor_value_t * value);
mcbor_value_t * mcbor_map_get(const mcbor_value_t * map, const mcbor_value_t * key);
mcbor_value_t * mcbor_map_get_text(const mcbor_value_t * map, const char * key);

int mcbor_is_uint(const mcbor_value_t * v);      int mcbor_is_negint(const mcbor_value_t * v);
int mcbor_is_int(const mcbor_value_t * v);       int mcbor_is_bytes(const mcbor_value_t * v);
int mcbor_is_text(const mcbor_value_t * v);      int mcbor_is_array(const mcbor_value_t * v);
int mcbor_is_map(const mcbor_value_t * v);       int mcbor_is_tag(const mcbor_value_t * v);
int mcbor_is_float(const mcbor_value_t * v);     int mcbor_is_bool(const mcbor_value_t * v);
int mcbor_is_null(const mcbor_value_t * v);      int mcbor_is_simple(const mcbor_value_t * v);
int mcbor_is_undefined(const mcbor_value_t * v);

mcbor_uint64_t mcbor_get_uint(const mcbor_value_t * v);
mcbor_int64_t mcbor_get_int(const mcbor_value_t * v);
const unsigned char * mcbor_get_bytes(const mcbor_value_t * v, size_t * len);
const char * mcbor_get_text(const mcbor_value_t * v, size_t * len);
size_t mcbor_array_len(const mcbor_value_t * v);
mcbor_value_t * mcbor_array_get(const mcbor_value_t * v, size_t index);
size_t mcbor_map_len(const mcbor_value_t * v);
mcbor_uint64_t mcbor_get_tag_number(const mcbor_value_t * v);
mcbor_value_t * mcbor_get_tag_content(const mcbor_value_t * v);
double mcbor_get_float(const mcbor_value_t * v);
int mcbor_get_bool(const mcbor_value_t * v);
mcbor_uint8_t mcbor_get_simple(const mcbor_value_t * v);

/*  Both report the length excluding the terminator; mcbor_to_diagnostic gives
    MCBOR_ERR_BUFFER_FULL when it had to truncate.  */
int mcbor_to_diagnostic(const mcbor_value_t * value, char * buf, size_t buflen);
size_t mcbor_diagnostic_len(const mcbor_value_t * value);

#ifdef MCBOR_IMPLEMENTATION

#include <stdio.h>  /*  sprintf, sscanf: diagnostics only.  */

#define MCBOR_SIZE_MAX ((size_t) -1)

static mcbor_uint16_t mcbor_be16(const unsigned char * p) {
  return (mcbor_uint16_t)(((mcbor_uint16_t) p[0] << 8) | (mcbor_uint16_t) p[1]);
}

static mcbor_uint32_t mcbor_be32(const unsigned char * p) {
  return ((mcbor_uint32_t) p[0] << 24) | ((mcbor_uint32_t) p[1] << 16) |
         ((mcbor_uint32_t) p[2] << 8) | (mcbor_uint32_t) p[3];
}

static mcbor_uint64_t mcbor_be64(const unsigned char * p) {
  return ((mcbor_uint64_t) mcbor_be32(p) << 32) | (mcbor_uint64_t) mcbor_be32(p + 4);
}

static void mcbor_write_be16(unsigned char * p, mcbor_uint16_t v) {
  p[0] = (unsigned char)(v >> 8);  p[1] = (unsigned char)(v & 0xFF);
}

static void mcbor_write_be32(unsigned char * p, mcbor_uint32_t v) {
  p[0] = (unsigned char)(v >> 24);          p[1] = (unsigned char)((v >> 16) & 0xFF);
  p[2] = (unsigned char)((v >> 8) & 0xFF);  p[3] = (unsigned char)(v & 0xFF);
}

static void mcbor_write_be64(unsigned char * p, mcbor_uint64_t v) {
  mcbor_write_be32(p, (mcbor_uint32_t)(v >> 32));
  mcbor_write_be32(p + 4, (mcbor_uint32_t)(v & 0xFFFFFFFFUL));
}

/*  Floats, built out of bit patterns to keep math.h out of the picture.  */

static double mcbor_bits_to_double(mcbor_uint64_t bits) {
  double d;  memcpy(&d, &bits, sizeof d);  return d;
}

static double mcbor_inf(void) {
  return mcbor_bits_to_double((mcbor_uint64_t) 0x7FF00000UL << 32);
}

static double mcbor_nan(void) {
  return mcbor_bits_to_double((mcbor_uint64_t) 0x7FF80000UL << 32);
}

static int mcbor_is_nan(double v) { return v != v; }
static int mcbor_is_inf(double v) { return v == mcbor_inf() || v == -mcbor_inf(); }

static int mcbor_signbit(double v) {
  mcbor_uint64_t bits;  memcpy(&bits, &v, sizeof bits);  return (int)(bits >> 63);
}

/*  m * 2^e, exact over the range a half float can name.  */
static double mcbor_scale2(double m, int e) {
  double s = 1.0;  int i;
  if (e < 0) for (i = 0; i < -e; i++) s *= 0.5;
  else       for (i = 0; i < e; i++)  s *= 2.0;
  return m * s;
}

static double mcbor_decode_half(const unsigned char * p) {
  unsigned int h = ((unsigned int) p[0] << 8) | p[1];
  unsigned int exp = (h >> 10) & 0x1f, mant = h & 0x3ff;
  double v;
  if (exp == 0)       v = mcbor_scale2((double) mant, -24);
  else if (exp != 31) v = mcbor_scale2((double)(mant + 1024), (int) exp - 25);
  else                v = mant ? mcbor_nan() : mcbor_inf();
  return (h & 0x8000) ? -v : v;
}

/*  Half precision holds 1.m * 2^exp for exp in [-14,15] with ten mantissa
    bits, and k * 2^-24 for k in [1,1023] below that.  */
static int mcbor_can_encode_half(double val) {
  mcbor_uint32_t bits, mant;  float f32;  int exp;
  if (mcbor_is_nan(val) || mcbor_is_inf(val) || val == 0.0) return 1;
  f32 = (float) val;
  if ((double) f32 != val) return 0;
  memcpy(&bits, &f32, sizeof bits);
  exp = (int)((bits >> 23) & 0xFF) - 127;  mant = bits & 0x7FFFFFUL;
  if (exp > 15) return 0;
  if (exp >= -14) return (mant & 0x1FFFUL) == 0;
  if (exp >= -24) return ((mant | 0x800000UL) & ((1UL << (-exp - 1)) - 1)) == 0;
  return 0;
}

static int mcbor_can_encode_single(double val) {
  float f32 = (float) val;  return (double) f32 == val;
}

static int mcbor_encode_half(unsigned char * out, double val) {
  mcbor_uint32_t bits, mant;  mcbor_uint16_t h;  float f32;  int exp, sign;
  if (!mcbor_can_encode_half(val)) return -1;
  sign = mcbor_signbit(val);
  if (mcbor_is_nan(val)) { mcbor_write_be16(out, 0x7E00);  return 0; }
  if (mcbor_is_inf(val))
    { mcbor_write_be16(out, (mcbor_uint16_t)(sign ? 0xFC00 : 0x7C00));  return 0; }
  if (val == 0.0)
    { mcbor_write_be16(out, (mcbor_uint16_t)(sign ? 0x8000 : 0x0000));  return 0; }
  f32 = (float)(sign ? -val : val);
  memcpy(&bits, &f32, sizeof bits);
  exp = (int)((bits >> 23) & 0xFF) - 127;  mant = bits & 0x7FFFFFUL;
  if (exp >= -14) h = (mcbor_uint16_t)(((mcbor_uint32_t)(exp + 15) << 10) | (mant >> 13));
  else            h = (mcbor_uint16_t)((mant | 0x800000UL) >> (-exp - 1));  /*  Subnormal.  */
  mcbor_write_be16(out, (mcbor_uint16_t)(h | (sign << 15)));
  return 0;
}

/*  Arena.  Every payload it hands out has to suit the strictest type in it.  */

typedef union { void * p;  double d;  mcbor_uint64_t u;  long l; } mcbor_align_t;
#define MCBOR_ALIGN sizeof(mcbor_align_t)

void mcbor_arena_init(mcbor_arena_t * arena) {
  mcbor_arena_init_custom(arena, MCBOR_MALLOC, MCBOR_FREE, MCBOR_REALLOC);
}

void mcbor_arena_init_custom(mcbor_arena_t * arena, void * (* malloc_fn)(size_t),
                             void (* free_fn)(void *),
                             void * (* realloc_fn)(void *, size_t)) {
  if (!arena) return;
  memset(arena, 0, sizeof(*arena));
  arena->block_size = MCBOR_ARENA_BLOCK_SIZE;
  arena->malloc_fn = malloc_fn;  arena->free_fn = free_fn;  arena->realloc_fn = realloc_fn;
}

static mcbor_arena_block_t * mcbor_arena_new_block(mcbor_arena_t * arena, size_t min_size) {
  mcbor_arena_block_t * block;
  size_t size = arena->block_size < min_size ? min_size : arena->block_size;
  block = (mcbor_arena_block_t *) arena->malloc_fn(sizeof(mcbor_arena_block_t));
  if (!block) return NULL;
  block->data = (unsigned char *) arena->malloc_fn(size);
  if (!block->data) { arena->free_fn(block);  return NULL; }
  block->size = size;  block->used = 0;  block->next = NULL;
  return block;
}

static int mcbor_arena_track_heap(mcbor_arena_t * arena, void * ptr) {
  size_t want;  void ** grown;
  if (arena->heap_count >= arena->heap_capacity) {
    want = arena->heap_capacity ? arena->heap_capacity * 2 : 8;
    if (want <= arena->heap_capacity || want > MCBOR_SIZE_MAX / sizeof(void *)) return -1;
    grown = (void **) arena->realloc_fn(arena->heap_ptrs, want * sizeof(void *));
    if (!grown) return -1;
    arena->heap_ptrs = grown;  arena->heap_capacity = want;
  }
  arena->heap_ptrs[arena->heap_count++] = ptr;
  return 0;
}

void * mcbor_arena_alloc(mcbor_arena_t * arena, size_t size, mcbor_uint8_t * flags) {
  mcbor_arena_block_t * block, * prev = NULL;  size_t aligned;  void * ptr;
  if (!arena) return NULL;
  if (!size) size = 1;
  if (size >= MCBOR_SMALL_ALLOC_LIMIT) {
    if (!(ptr = arena->malloc_fn(size))) return NULL;
    if (mcbor_arena_track_heap(arena, ptr) < 0) { arena->free_fn(ptr);  return NULL; }
    if (flags) *flags = MCBOR_FLAG_MANAGED | MCBOR_FLAG_HEAP;
    arena->total_allocated += size;
    return ptr;
  }
  aligned = (size + (MCBOR_ALIGN - 1)) & ~(size_t)(MCBOR_ALIGN - 1);
  /*  Blocks past `current` are ones mcbor_arena_reset recycled; walking to the
      end of the chain is also what keeps a new block from overwriting a `next`
      link and orphaning everything behind it.  */
  for (block = arena->current; block; prev = block, block = block->next)
    if (block->size - block->used >= aligned) break;
  if (!block) {
    if (!(block = mcbor_arena_new_block(arena, aligned))) return NULL;
    if (prev) prev->next = block;  else arena->head = block;
  }
  arena->current = block;
  ptr = block->data + block->used;  block->used += aligned;
  if (flags) *flags = MCBOR_FLAG_MANAGED | MCBOR_FLAG_ARENA;
  arena->total_allocated += aligned;
  return ptr;
}

void mcbor_arena_destroy(mcbor_arena_t * arena) {
  mcbor_arena_block_t * block, * next;  size_t i;
  if (!arena) return;
  for (block = arena->head; block; block = next)
    { next = block->next;  arena->free_fn(block->data);  arena->free_fn(block); }
  for (i = 0; i < arena->heap_count; i++) arena->free_fn(arena->heap_ptrs[i]);
  arena->free_fn(arena->heap_ptrs);
  arena->head = arena->current = NULL;  arena->heap_ptrs = NULL;
  arena->total_allocated = arena->heap_count = arena->heap_capacity = 0;
}

void mcbor_arena_reset(mcbor_arena_t * arena) {
  mcbor_arena_block_t * block;  size_t i;
  if (!arena) return;
  for (block = arena->head; block; block = block->next) block->used = 0;
  arena->current = arena->head;
  for (i = 0; i < arena->heap_count; i++) arena->free_fn(arena->heap_ptrs[i]);
  arena->heap_count = arena->total_allocated = 0;
}

/*  Object stealing.  */

int mcbor_is_stolen(const mcbor_value_t * value) {
  return value && (value->flags & MCBOR_FLAG_STOLEN);
}

int mcbor_is_heap(const mcbor_value_t * value) {
  return value && (value->flags & MCBOR_FLAG_HEAP);
}

static void mcbor_untrack(mcbor_arena_t * arena, void * ptr) {
  size_t i;
  for (i = 0; i < arena->heap_count; i++)
    if (arena->heap_ptrs[i] == ptr) { arena->heap_ptrs[i] = NULL;  return; }
}

/*  Hand `ptr` over to the caller: unhook it from the arena when it is a heap
    block of its own, copy it out otherwise.  */
static void * mcbor_take(mcbor_arena_t * arena, void * ptr, size_t n, int on_heap) {
  void * out;
  if (on_heap && arena) { mcbor_untrack(arena, ptr);  return ptr; }
  if (!(out = MCBOR_MALLOC(n))) return NULL;
  memcpy(out, ptr, n);
  return out;
}

/*  Moves the payload of `value` out of the arena; children keep pointing into
    it, so a container wants mcbor_steal_deep.  The struct itself stays where it
    is -- copy it out by hand if the arena is going away.  */
int mcbor_steal(mcbor_value_t * value, mcbor_arena_t * arena) {
  mcbor_arena_t * a;  mcbor_value_t * cells;  void * p;  size_t n, i;  int on_heap;
  if (!value) return MCBOR_ERR_INVALID_ARG;
  if (value->flags & MCBOR_FLAG_STOLEN) return MCBOR_OK;
  a = value->arena ? value->arena : arena;
  on_heap = value->flags & MCBOR_FLAG_DATA_HEAP;
  switch (value->type) {
    case MCBOR_TYPE_BYTES:  case MCBOR_TYPE_TEXT:
      n = value->data.bytes.len;
      if (!n || !value->data.bytes.data) { value->data.bytes.data = NULL;  break; }
      if (!(p = mcbor_take(a, value->data.bytes.data, n, on_heap))) return MCBOR_ERR_NO_MEMORY;
      value->data.bytes.data = (unsigned char *) p;
      break;
    case MCBOR_TYPE_ARRAY:
      n = value->data.array.count;
      if (!n || !value->data.array.items) { value->data.array.items = NULL;  break; }
      if (n > MCBOR_SIZE_MAX / sizeof(mcbor_value_t)) return MCBOR_ERR_OVERFLOW;
      p = mcbor_take(a, value->data.array.items, n * sizeof(mcbor_value_t), on_heap);
      if (!p) return MCBOR_ERR_NO_MEMORY;
      value->data.array.items = (mcbor_value_t *) p;
      if (!on_heap) value->data.array.capacity = n;
      break;
    case MCBOR_TYPE_MAP:
      /*  Keys and values share a block; copying it whole keeps them adjacent
          whichever way the capacity ran.  */
      n = value->data.map.count;
      if (!n || !value->data.map.keys)
        { value->data.map.keys = value->data.map.values = NULL;  break; }
      if (n > MCBOR_SIZE_MAX / (2 * sizeof(mcbor_value_t))) return MCBOR_ERR_OVERFLOW;
      cells = (mcbor_value_t *) MCBOR_MALLOC(2 * n * sizeof(mcbor_value_t));
      if (!cells) return MCBOR_ERR_NO_MEMORY;
      for (i = 0; i < n; i++)
        { cells[i] = value->data.map.keys[i];  cells[n + i] = value->data.map.values[i]; }
      value->data.map.keys = cells;  value->data.map.values = cells + n;
      value->data.map.capacity = n;
      break;
    case MCBOR_TYPE_TAG:
      if (!value->data.tag.content) break;
      if (!(p = MCBOR_MALLOC(sizeof(mcbor_value_t)))) return MCBOR_ERR_NO_MEMORY;
      memcpy(p, value->data.tag.content, sizeof(mcbor_value_t));
      value->data.tag.content = (mcbor_value_t *) p;
      break;
    default: break;
  }
  value->flags = (mcbor_uint8_t)((value->flags &
    ~(MCBOR_FLAG_MANAGED | MCBOR_FLAG_ARENA | MCBOR_FLAG_DATA_HEAP)) | MCBOR_FLAG_STOLEN);
  return MCBOR_OK;
}

static int mcbor_steal_nested(mcbor_value_t * value, mcbor_arena_t * arena, int depth) {
  int result;  size_t i;
  if (!value) return MCBOR_ERR_INVALID_ARG;
  if (depth > MCBOR_MAX_DEPTH) return MCBOR_ERR_DEPTH;
  if ((result = mcbor_steal(value, arena)) != MCBOR_OK) return result;
  switch (value->type) {
    case MCBOR_TYPE_ARRAY:
      for (i = 0; i < value->data.array.count; i++)
        if ((result = mcbor_steal_nested(&value->data.array.items[i], arena, depth + 1)))
          return result;
      break;
    case MCBOR_TYPE_MAP:
      for (i = 0; i < value->data.map.count; i++) {
        if ((result = mcbor_steal_nested(&value->data.map.keys[i], arena, depth + 1)))
          return result;
        if ((result = mcbor_steal_nested(&value->data.map.values[i], arena, depth + 1)))
          return result;
      }
      break;
    case MCBOR_TYPE_TAG:
      if (value->data.tag.content)
        return mcbor_steal_nested(value->data.tag.content, arena, depth + 1);
      break;
    default: break;
  }
  return MCBOR_OK;
}

int mcbor_steal_deep(mcbor_value_t * value, mcbor_arena_t * arena) {
  return mcbor_steal_nested(value, arena, 0);
}

void mcbor_free_stolen(mcbor_value_t * value) {
  size_t i;
  if (!value || !(value->flags & MCBOR_FLAG_STOLEN)) return;
  value->flags &= (mcbor_uint8_t) ~MCBOR_FLAG_STOLEN;
  switch (value->type) {
    case MCBOR_TYPE_BYTES:  case MCBOR_TYPE_TEXT:
      MCBOR_FREE(value->data.bytes.data);
      value->data.bytes.data = NULL;  value->data.bytes.len = 0;
      break;
    case MCBOR_TYPE_ARRAY:
      for (i = 0; i < value->data.array.count; i++)
        mcbor_free_stolen(&value->data.array.items[i]);
      MCBOR_FREE(value->data.array.items);
      value->data.array.items = NULL;
      value->data.array.count = value->data.array.capacity = 0;
      break;
    case MCBOR_TYPE_MAP:
      for (i = 0; i < value->data.map.count; i++)
        { mcbor_free_stolen(&value->data.map.keys[i]);
          mcbor_free_stolen(&value->data.map.values[i]); }
      MCBOR_FREE(value->data.map.keys);
      value->data.map.keys = value->data.map.values = NULL;
      value->data.map.count = value->data.map.capacity = 0;
      break;
    case MCBOR_TYPE_TAG:
      if (value->data.tag.content) {
        mcbor_free_stolen(value->data.tag.content);
        MCBOR_FREE(value->data.tag.content);
        value->data.tag.content = NULL;
      }
      break;
    default: break;
  }
}

/*  Decoder.  */

void mcbor_decoder_init(mcbor_decoder_t * dec, const unsigned char * data,
                        size_t len, mcbor_arena_t * arena) {
  memset(dec, 0, sizeof(*dec));
  dec->data = data;  dec->len = data ? len : 0;  dec->arena = arena;
}

void mcbor_decoder_set_flags(mcbor_decoder_t * dec, unsigned int flags) { dec->flags = flags; }
size_t mcbor_decoder_remaining(const mcbor_decoder_t * dec) { return dec->len - dec->pos; }
int mcbor_decoder_error(const mcbor_decoder_t * dec) { return dec->error_code; }
const char * mcbor_decoder_error_msg(const mcbor_decoder_t * dec) { return dec->error_msg; }
size_t mcbor_decoder_error_pos(const mcbor_decoder_t * dec) { return dec->error_pos; }

static int mcbor_fail(mcbor_decoder_t * dec, int code, const char * msg, size_t pos) {
  dec->error_code = code;  dec->error_msg = msg;  dec->error_pos = pos;
  return -1;
}

static int mcbor_read(mcbor_decoder_t * dec, size_t n, const unsigned char ** out) {
  if (n > dec->len - dec->pos)  /*  Subtraction: pos + n is free to wrap.  */
    return mcbor_fail(dec, MCBOR_ERR_TRUNCATED, "Unexpected end of input", dec->pos);
  *out = dec->data + dec->pos;  dec->pos += n;
  return 0;
}

static int mcbor_argument(mcbor_decoder_t * dec, mcbor_uint8_t ai, mcbor_uint64_t * value) {
  const unsigned char * p;  mcbor_uint64_t least;  size_t at = dec->pos - 1;
  if (ai < 24) { *value = ai;  return 0; }
  switch (ai) {
    case 24:
      if (mcbor_read(dec, 1, &p) < 0) return -1;
      *value = p[0];  least = 24;  break;
    case 25:
      if (mcbor_read(dec, 2, &p) < 0) return -1;
      *value = mcbor_be16(p);  least = 0x100;  break;
    case 26:
      if (mcbor_read(dec, 4, &p) < 0) return -1;
      *value = mcbor_be32(p);  least = 0x10000;  break;
    case 27:
      if (mcbor_read(dec, 8, &p) < 0) return -1;
      *value = mcbor_be64(p);  least = (mcbor_uint64_t) 1 << 32;  break;
    default:
      return mcbor_fail(dec, MCBOR_ERR_RESERVED, "Reserved additional info value", at);
  }
  if ((dec->flags & MCBOR_DECODE_STRICT) && *value < least)
    return mcbor_fail(dec, MCBOR_ERR_MALFORMED, "Argument is not in shortest form", at);
  return 0;
}

static int mcbor_utf8_ok(const unsigned char * s, size_t len) {
  static const mcbor_uint32_t least[3] = { 0x80UL, 0x800UL, 0x10000UL };
  size_t i = 0, j, n;  mcbor_uint32_t cp;
  while (i < len) {
    unsigned char c = s[i];
    if (c < 0x80) { i++;  continue; }
    if ((c & 0xE0) == 0xC0)      { n = 1;  cp = c & 0x1FU; }
    else if ((c & 0xF0) == 0xE0) { n = 2;  cp = c & 0x0FU; }
    else if ((c & 0xF8) == 0xF0) { n = 3;  cp = c & 0x07U; }
    else return 0;
    if (len - i - 1 < n) return 0;
    for (j = 1; j <= n; j++) {
      if ((s[i + j] & 0xC0) != 0x80) return 0;
      cp = (cp << 6) | (s[i + j] & 0x3FU);
    }
    if (cp < least[n - 1]) return 0;  /*  Overlong.  */
    if (cp > 0x10FFFFUL || (cp >= 0xD800UL && cp <= 0xDFFFUL)) return 0;
    i += n + 1;
  }
  return 1;
}

static int mcbor_decode_value(mcbor_decoder_t * dec, mcbor_value_t * out,
                              int breakable, int depth);

/*  Item cells for a container.  `n` is bounded by the input length already,
    which is what keeps the byte count from wrapping.  */
static mcbor_value_t * mcbor_cells(mcbor_decoder_t * dec, size_t n, mcbor_uint8_t * flags) {
  mcbor_value_t * cells;
  if (n > MCBOR_SIZE_MAX / sizeof(mcbor_value_t))
    { mcbor_fail(dec, MCBOR_ERR_OVERFLOW, "Item count out of range", dec->pos);  return NULL; }
  cells = (mcbor_value_t *) mcbor_arena_alloc(dec->arena, n * sizeof(mcbor_value_t), flags);
  if (!cells) mcbor_fail(dec, MCBOR_ERR_NO_MEMORY, "Out of memory", dec->pos);
  return cells;
}

static mcbor_uint8_t mcbor_data_flags(mcbor_uint8_t alloc_flags) {
  return (mcbor_uint8_t)((alloc_flags & MCBOR_FLAG_HEAP) ? MCBOR_FLAG_DATA_HEAP
                                                         : MCBOR_FLAG_NONE);
}

/*  Walks the chunks of an indefinite-length string up to the break, copying
    them into `dst` when it is given.  Chunks are definite-length strings of the
    enclosing major type and nothing else.  */
static int mcbor_indef_chunks(mcbor_decoder_t * dec, mcbor_uint8_t mt,
                              unsigned char * dst, size_t * total) {
  const unsigned char * p;  mcbor_uint64_t len;  mcbor_uint8_t ib;  size_t sum = 0;
  for (;;) {
    if (mcbor_read(dec, 1, &p) < 0) return -1;
    if ((ib = *p) == 0xFF) break;
    if ((ib >> 5) != mt || (ib & 0x1f) == 31)
      return mcbor_fail(dec, MCBOR_ERR_MALFORMED,
                        "Bad chunk in indefinite-length string", dec->pos - 1);
    if (mcbor_argument(dec, (mcbor_uint8_t)(ib & 0x1f), &len) < 0) return -1;
    if (len > (mcbor_uint64_t)(dec->len - dec->pos))
      return mcbor_fail(dec, MCBOR_ERR_TRUNCATED, "Unexpected end of input", dec->pos);
    if (mcbor_read(dec, (size_t) len, &p) < 0) return -1;
    if (mt == 3 && (dec->flags & MCBOR_DECODE_VALIDATE_UTF8) && !mcbor_utf8_ok(p, (size_t) len))
      return mcbor_fail(dec, MCBOR_ERR_INVALID_UTF8, "Invalid UTF-8 in text string",
                        dec->pos - (size_t) len);
    if (dst && len) memcpy(dst + sum, p, (size_t) len);
    sum += (size_t) len;  /*  Bounded by the input, so it cannot wrap.  */
  }
  *total = sum;
  return 0;
}

static int mcbor_decode_indef_str(mcbor_decoder_t * dec, mcbor_value_t * out, int is_text) {
  size_t start = dec->pos, end, total;
  mcbor_uint8_t flags, mt = is_text ? 3 : 2;  unsigned char * buffer;
  out->type = is_text ? MCBOR_TYPE_TEXT : MCBOR_TYPE_BYTES;
  if (mcbor_indef_chunks(dec, mt, NULL, &total) < 0) return -1;
  if (!(out->data.bytes.len = total)) return 0;
  buffer = (unsigned char *) mcbor_arena_alloc(dec->arena, total, &flags);
  if (!buffer) return mcbor_fail(dec, MCBOR_ERR_NO_MEMORY, "Out of memory", dec->pos);
  end = dec->pos;  dec->pos = start;
  if (mcbor_indef_chunks(dec, mt, buffer, &total) < 0) return -1;
  dec->pos = end;
  out->data.bytes.data = buffer;  out->flags = mcbor_data_flags(flags);
  return 0;
}

static int mcbor_decode_indef_array(mcbor_decoder_t * dec, mcbor_value_t * out, int depth) {
  mcbor_value_t * items = NULL, * grown, item;
  size_t count = 0, capacity = 0, want, i;  mcbor_uint8_t flags;
  out->type = MCBOR_TYPE_ARRAY;
  for (;;) {
    if (mcbor_decode_value(dec, &item, 1, depth + 1) < 0) { MCBOR_FREE(items);  return -1; }
    if (item.type == MCBOR_TYPE_BREAK) break;
    if (count == capacity) {
      want = capacity ? capacity * 2 : 4;
      if (want > MCBOR_SIZE_MAX / sizeof(mcbor_value_t)) { MCBOR_FREE(items);
        return mcbor_fail(dec, MCBOR_ERR_OVERFLOW, "Item count out of range", dec->pos); }
      grown = (mcbor_value_t *) MCBOR_REALLOC(items, want * sizeof(mcbor_value_t));
      if (!grown) { MCBOR_FREE(items);
        return mcbor_fail(dec, MCBOR_ERR_NO_MEMORY, "Out of memory", dec->pos); }
      items = grown;  capacity = want;
    }
    items[count++] = item;
  }
  if (count) {
    if (!(out->data.array.items = mcbor_cells(dec, count, &flags)))
      { MCBOR_FREE(items);  return -1; }
    for (i = 0; i < count; i++) out->data.array.items[i] = items[i];
    out->flags = mcbor_data_flags(flags);
  }
  out->data.array.count = out->data.array.capacity = count;
  MCBOR_FREE(items);
  return 0;
}

static int mcbor_decode_indef_map(mcbor_decoder_t * dec, mcbor_value_t * out, int depth) {
  mcbor_value_t * pairs = NULL, * grown, * cells, key, val;
  size_t count = 0, capacity = 0, want, i;  mcbor_uint8_t flags;
  out->type = MCBOR_TYPE_MAP;
  for (;;) {
    if (mcbor_decode_value(dec, &key, 1, depth + 1) < 0) { MCBOR_FREE(pairs);  return -1; }
    if (key.type == MCBOR_TYPE_BREAK) break;
    if (mcbor_decode_value(dec, &val, 0, depth + 1) < 0) { MCBOR_FREE(pairs);  return -1; }
    if (count == capacity) {
      want = capacity ? capacity * 2 : 4;
      if (want > MCBOR_SIZE_MAX / (2 * sizeof(mcbor_value_t))) { MCBOR_FREE(pairs);
        return mcbor_fail(dec, MCBOR_ERR_OVERFLOW, "Item count out of range", dec->pos); }
      grown = (mcbor_value_t *) MCBOR_REALLOC(pairs, 2 * want * sizeof(mcbor_value_t));
      if (!grown) { MCBOR_FREE(pairs);
        return mcbor_fail(dec, MCBOR_ERR_NO_MEMORY, "Out of memory", dec->pos); }
      pairs = grown;  capacity = want;
    }
    pairs[2 * count] = key;  pairs[2 * count + 1] = val;  count++;
  }
  if (count) {
    if (!(cells = mcbor_cells(dec, 2 * count, &flags))) { MCBOR_FREE(pairs);  return -1; }
    for (i = 0; i < count; i++)
      { cells[i] = pairs[2 * i];  cells[count + i] = pairs[2 * i + 1]; }
    out->data.map.keys = cells;  out->data.map.values = cells + count;
    out->flags = mcbor_data_flags(flags);
  }
  out->data.map.count = out->data.map.capacity = count;
  MCBOR_FREE(pairs);
  return 0;
}

static int mcbor_decode_value(mcbor_decoder_t * dec, mcbor_value_t * out,
                              int breakable, int depth) {
  const unsigned char * p;  mcbor_uint8_t ib, mt, ai, flags;
  mcbor_uint64_t val;  mcbor_value_t * cells;  size_t i, n, room;
  mcbor_uint32_t f32_bits;  float f32;
  memset(out, 0, sizeof(mcbor_value_t));
  out->arena = dec->arena;
  if (depth > MCBOR_MAX_DEPTH)
    return mcbor_fail(dec, MCBOR_ERR_DEPTH, "Nesting limit exceeded", dec->pos);
  if (mcbor_read(dec, 1, &p) < 0) return -1;
  ib = *p;  mt = (mcbor_uint8_t)(ib >> 5);  ai = (mcbor_uint8_t)(ib & 0x1f);

  if (ai == 31) switch (mt) {
    case 2:  case 3: return mcbor_decode_indef_str(dec, out, mt == 3);
    case 4: return mcbor_decode_indef_array(dec, out, depth);
    case 5: return mcbor_decode_indef_map(dec, out, depth);
    case 7:
      if (!breakable)
        return mcbor_fail(dec, MCBOR_ERR_BREAK_OUTSIDE,
                          "Break outside indefinite-length item", dec->pos - 1);
      out->type = MCBOR_TYPE_BREAK;
      return 0;
    default:
      return mcbor_fail(dec, MCBOR_ERR_MALFORMED,
                        "Indefinite length not allowed for this type", dec->pos - 1);
  }

  /*  Float arguments are bit patterns; the cases below read them.  */
  if (mt == 7 && ai >= 25 && ai <= 27) val = 0;
  else if (mcbor_argument(dec, ai, &val) < 0) return -1;
  room = dec->len - dec->pos;

  switch (mt) {
    case 0: out->type = MCBOR_TYPE_UINT;  out->data.uint_val = val;  break;
    case 1: out->type = MCBOR_TYPE_NEGINT;  out->data.negint_val = val;  break;

    case 2:  case 3:
      out->type = mt == 3 ? MCBOR_TYPE_TEXT : MCBOR_TYPE_BYTES;
      if (val > (mcbor_uint64_t) room)
        return mcbor_fail(dec, MCBOR_ERR_TRUNCATED, "Unexpected end of input", dec->pos);
      if (mcbor_read(dec, (n = (size_t) val), &p) < 0) return -1;
      if (mt == 3 && (dec->flags & MCBOR_DECODE_VALIDATE_UTF8) && !mcbor_utf8_ok(p, n))
        return mcbor_fail(dec, MCBOR_ERR_INVALID_UTF8,
                          "Invalid UTF-8 in text string", dec->pos - n);
      if (n) {
        out->data.bytes.data = (unsigned char *) mcbor_arena_alloc(dec->arena, n, &flags);
        if (!out->data.bytes.data)
          return mcbor_fail(dec, MCBOR_ERR_NO_MEMORY, "Out of memory", dec->pos);
        memcpy(out->data.bytes.data, p, n);
        out->flags = mcbor_data_flags(flags);
      }
      out->data.bytes.len = n;
      break;

    case 4:
      /*  An item is a byte at the very least, so a count past what is left is a
          lie, and bounding it that way keeps the cell count in range.  */
      out->type = MCBOR_TYPE_ARRAY;
      if (val > (mcbor_uint64_t) room)
        return mcbor_fail(dec, MCBOR_ERR_TRUNCATED, "Unexpected end of input", dec->pos);
      if ((n = (size_t) val) != 0) {
        if (!(out->data.array.items = mcbor_cells(dec, n, &flags))) return -1;
        out->flags = mcbor_data_flags(flags);
        for (i = 0; i < n; i++)
          if (mcbor_decode_value(dec, &out->data.array.items[i], 0, depth + 1) < 0) return -1;
      }
      out->data.array.count = out->data.array.capacity = n;
      break;

    case 5:
      out->type = MCBOR_TYPE_MAP;
      if (val > (mcbor_uint64_t)(room / 2))
        return mcbor_fail(dec, MCBOR_ERR_TRUNCATED, "Unexpected end of input", dec->pos);
      if ((n = (size_t) val) != 0) {
        if (!(cells = mcbor_cells(dec, 2 * n, &flags))) return -1;
        out->data.map.keys = cells;  out->data.map.values = cells + n;
        out->flags = mcbor_data_flags(flags);
        for (i = 0; i < n; i++) {
          if (mcbor_decode_value(dec, &out->data.map.keys[i], 0, depth + 1) < 0) return -1;
          if (mcbor_decode_value(dec, &out->data.map.values[i], 0, depth + 1) < 0) return -1;
        }
      }
      out->data.map.count = out->data.map.capacity = n;
      break;

    case 6:
      out->type = MCBOR_TYPE_TAG;  out->data.tag.tag_number = val;
      if (!(out->data.tag.content = mcbor_cells(dec, 1, &flags))) return -1;
      out->flags = mcbor_data_flags(flags);
      return mcbor_decode_value(dec, out->data.tag.content, 0, depth + 1);

    case 7: switch (ai) {
      case 20: out->type = MCBOR_TYPE_FALSE;  break;
      case 21: out->type = MCBOR_TYPE_TRUE;  break;
      case 22: out->type = MCBOR_TYPE_NULL;  break;
      case 23: out->type = MCBOR_TYPE_UNDEFINED;  break;
      case 24:
        if (val < 32)
          return mcbor_fail(dec, MCBOR_ERR_MALFORMED,
                            "Invalid simple value encoding", dec->pos - 2);
        out->type = MCBOR_TYPE_SIMPLE;  out->data.simple_val = (mcbor_uint8_t) val;
        break;
      case 25:
        if (mcbor_read(dec, 2, &p) < 0) return -1;
        out->type = MCBOR_TYPE_FLOAT16;  out->data.float_val = mcbor_decode_half(p);
        break;
      case 26:
        if (mcbor_read(dec, 4, &p) < 0) return -1;
        out->type = MCBOR_TYPE_FLOAT32;  f32_bits = mcbor_be32(p);
        memcpy(&f32, &f32_bits, sizeof f32);
        out->data.float_val = (double) f32;
        break;
      case 27:
        if (mcbor_read(dec, 8, &p) < 0) return -1;
        out->type = MCBOR_TYPE_FLOAT64;
        out->data.float_val = mcbor_bits_to_double(mcbor_be64(p));
        break;
      default:
        if (ai >= 20)
          return mcbor_fail(dec, MCBOR_ERR_RESERVED, "Reserved simple value", dec->pos - 1);
        out->type = MCBOR_TYPE_SIMPLE;  out->data.simple_val = ai;
        break;
    } break;

    default:
      return mcbor_fail(dec, MCBOR_ERR_MALFORMED, "Invalid major type", dec->pos - 1);
  }
  return 0;
}

int mcbor_decode(mcbor_decoder_t * dec, mcbor_value_t * out) {
  if (!dec || !out) return MCBOR_ERR_INVALID_ARG;
  if (dec->error_code != MCBOR_OK) return -1;  /*  Errors stick.  */
  return mcbor_decode_value(dec, out, 0, 0);
}

/*  A callback verdict other than zero stops the walk and is handed back as is,
    so keep those clear of the MCBOR_ERR_ codes.  */
int mcbor_decode_stream(mcbor_decoder_t * dec,
                        int (* callback)(mcbor_value_t * value, void * ctx), void * ctx) {
  mcbor_value_t value;  int result;
  if (!dec || !callback) return MCBOR_ERR_INVALID_ARG;
  while (dec->pos < dec->len) {
    if ((result = mcbor_decode(dec, &value)) < 0) return result;
    if ((result = callback(&value, ctx)) != 0) return result;
  }
  return 0;
}

/*  Encoder.  */

void mcbor_encoder_init(mcbor_encoder_t * enc, unsigned char * buffer, size_t capacity) {
  memset(enc, 0, sizeof(*enc));
  enc->buffer = buffer;  enc->capacity = buffer ? capacity : 0;
  enc->flags = MCBOR_ENCODE_PREFERRED;
}

void mcbor_encoder_init_dynamic(mcbor_encoder_t * enc, mcbor_arena_t * arena) {
  memset(enc, 0, sizeof(*enc));
  enc->arena = arena;  enc->flags = MCBOR_ENCODE_PREFERRED;  enc->dynamic = 1;
}

void mcbor_encoder_set_flags(mcbor_encoder_t * enc, unsigned int flags) { enc->flags = flags; }
const unsigned char * mcbor_encoder_data(const mcbor_encoder_t * enc) { return enc->buffer; }
size_t mcbor_encoder_len(const mcbor_encoder_t * enc) { return enc->len; }
int mcbor_encoder_error(const mcbor_encoder_t * enc) { return enc->error_code; }

void mcbor_encoder_destroy(mcbor_encoder_t * enc) {
  if (enc->dynamic && enc->buffer) {
    if (enc->arena) enc->arena->free_fn(enc->buffer);  else MCBOR_FREE(enc->buffer);
    enc->buffer = NULL;  enc->capacity = enc->len = 0;
  }
}

static int mcbor_enc_fail(mcbor_encoder_t * enc, int code) {
  enc->error_code = code;  return -1;
}

static int mcbor_encoder_ensure(mcbor_encoder_t * enc, size_t needed) {
  size_t want;  unsigned char * grown;
  if (enc->error_code != MCBOR_OK) return -1;  /*  Errors stick.  */
  if (needed <= enc->capacity - enc->len) return 0;
  if (!enc->dynamic) return mcbor_enc_fail(enc, MCBOR_ERR_BUFFER_FULL);
  if (needed > MCBOR_SIZE_MAX - enc->len) return mcbor_enc_fail(enc, MCBOR_ERR_OVERFLOW);
  want = enc->capacity ? enc->capacity : 64;
  while (want < enc->len + needed) {
    if (want > MCBOR_SIZE_MAX / 2) { want = enc->len + needed;  break; }
    want *= 2;
  }
  grown = (unsigned char *)(enc->arena ? enc->arena->realloc_fn(enc->buffer, want)
                                       : MCBOR_REALLOC(enc->buffer, want));
  if (!grown) return mcbor_enc_fail(enc, MCBOR_ERR_NO_MEMORY);
  enc->buffer = grown;  enc->capacity = want;
  return 0;
}

static int mcbor_encode_head(mcbor_encoder_t * enc, mcbor_uint8_t mt, mcbor_uint64_t val) {
  mcbor_uint8_t ib = (mcbor_uint8_t)(mt << 5);
  if (val < 24) {
    if (mcbor_encoder_ensure(enc, 1) < 0) return -1;
    enc->buffer[enc->len++] = (unsigned char)(ib | (mcbor_uint8_t) val);
  } else if (val <= 0xFF) {
    if (mcbor_encoder_ensure(enc, 2) < 0) return -1;
    enc->buffer[enc->len++] = (unsigned char)(ib | 24);
    enc->buffer[enc->len++] = (unsigned char) val;
  } else if (val <= 0xFFFF) {
    if (mcbor_encoder_ensure(enc, 3) < 0) return -1;
    enc->buffer[enc->len++] = (unsigned char)(ib | 25);
    mcbor_write_be16(enc->buffer + enc->len, (mcbor_uint16_t) val);  enc->len += 2;
  } else if (val <= 0xFFFFFFFFUL) {
    if (mcbor_encoder_ensure(enc, 5) < 0) return -1;
    enc->buffer[enc->len++] = (unsigned char)(ib | 26);
    mcbor_write_be32(enc->buffer + enc->len, (mcbor_uint32_t) val);  enc->len += 4;
  } else {
    if (mcbor_encoder_ensure(enc, 9) < 0) return -1;
    enc->buffer[enc->len++] = (unsigned char)(ib | 27);
    mcbor_write_be64(enc->buffer + enc->len, val);  enc->len += 8;
  }
  return 0;
}

int mcbor_encode_uint(mcbor_encoder_t * enc, mcbor_uint64_t value) {
  return mcbor_encode_head(enc, 0, value);
}

int mcbor_encode_negint(mcbor_encoder_t * enc, mcbor_uint64_t n) {
  return mcbor_encode_head(enc, 1, n);
}

/*  -1 - value stays in range for every negative value, INT64_MIN too.  */
int mcbor_encode_int(mcbor_encoder_t * enc, mcbor_int64_t value) {
  return value >= 0 ? mcbor_encode_uint(enc, (mcbor_uint64_t) value)
                    : mcbor_encode_negint(enc, (mcbor_uint64_t)(-1 - value));
}

static int mcbor_encode_str(mcbor_encoder_t * enc, mcbor_uint8_t mt,
                            const unsigned char * data, size_t len) {
  if (len && !data) return mcbor_enc_fail(enc, MCBOR_ERR_INVALID_ARG);
  if (mcbor_encode_head(enc, mt, (mcbor_uint64_t) len) < 0) return -1;
  if (len) {
    if (mcbor_encoder_ensure(enc, len) < 0) return -1;
    memcpy(enc->buffer + enc->len, data, len);  enc->len += len;
  }
  return 0;
}

int mcbor_encode_bytes(mcbor_encoder_t * enc, const unsigned char * data, size_t len) {
  return mcbor_encode_str(enc, 2, data, len);
}

int mcbor_encode_text(mcbor_encoder_t * enc, const char * str, size_t len) {
  return mcbor_encode_str(enc, 3, (const unsigned char *) str, len);
}

int mcbor_encode_text_cstr(mcbor_encoder_t * enc, const char * str) {
  if (!str) return mcbor_enc_fail(enc, MCBOR_ERR_INVALID_ARG);
  return mcbor_encode_text(enc, str, strlen(str));
}

int mcbor_encode_float16(mcbor_encoder_t * enc, double value) {
  unsigned char half[2];
  if (mcbor_encode_half(half, value) < 0) return mcbor_enc_fail(enc, MCBOR_ERR_INVALID_ARG);
  if (mcbor_encoder_ensure(enc, 3) < 0) return -1;
  enc->buffer[enc->len++] = 0xF9;
  enc->buffer[enc->len++] = half[0];  enc->buffer[enc->len++] = half[1];
  return 0;
}

int mcbor_encode_float32(mcbor_encoder_t * enc, float value) {
  mcbor_uint32_t bits;
  if (mcbor_encoder_ensure(enc, 5) < 0) return -1;
  enc->buffer[enc->len++] = 0xFA;
  memcpy(&bits, &value, sizeof bits);
  mcbor_write_be32(enc->buffer + enc->len, bits);  enc->len += 4;
  return 0;
}

int mcbor_encode_float64(mcbor_encoder_t * enc, double value) {
  mcbor_uint64_t bits;
  if (mcbor_encoder_ensure(enc, 9) < 0) return -1;
  enc->buffer[enc->len++] = 0xFB;
  memcpy(&bits, &value, sizeof bits);
  mcbor_write_be64(enc->buffer + enc->len, bits);  enc->len += 8;
  return 0;
}

int mcbor_encode_float(mcbor_encoder_t * enc, double value) {
  if (enc->flags & MCBOR_ENCODE_PREFERRED) {
    if (mcbor_can_encode_half(value)) return mcbor_encode_float16(enc, value);
    if (mcbor_can_encode_single(value)) return mcbor_encode_float32(enc, (float) value);
  }
  return mcbor_encode_float64(enc, value);
}

static int mcbor_encode_byte(mcbor_encoder_t * enc, unsigned char b) {
  if (mcbor_encoder_ensure(enc, 1) < 0) return -1;
  enc->buffer[enc->len++] = b;
  return 0;
}

int mcbor_encode_bool(mcbor_encoder_t * enc, int value) {
  return mcbor_encode_byte(enc, value ? 0xF5 : 0xF4);
}

int mcbor_encode_null(mcbor_encoder_t * enc) { return mcbor_encode_byte(enc, 0xF6); }
int mcbor_encode_undefined(mcbor_encoder_t * enc) { return mcbor_encode_byte(enc, 0xF7); }
int mcbor_encode_array_start_indef(mcbor_encoder_t * enc) { return mcbor_encode_byte(enc, 0x9F); }
int mcbor_encode_map_start_indef(mcbor_encoder_t * enc) { return mcbor_encode_byte(enc, 0xBF); }
int mcbor_encode_break(mcbor_encoder_t * enc) { return mcbor_encode_byte(enc, 0xFF); }

/*  20 to 31 are the ones the head already spends on false, null and friends.  */
int mcbor_encode_simple(mcbor_encoder_t * enc, mcbor_uint8_t value) {
  if (value >= 20 && value < 32) return mcbor_enc_fail(enc, MCBOR_ERR_INVALID_ARG);
  if (value < 20) return mcbor_encode_byte(enc, (unsigned char)(0xE0 | value));
  if (mcbor_encoder_ensure(enc, 2) < 0) return -1;
  enc->buffer[enc->len++] = 0xF8;  enc->buffer[enc->len++] = value;
  return 0;
}

int mcbor_encode_array_start(mcbor_encoder_t * enc, size_t count) {
  return mcbor_encode_head(enc, 4, (mcbor_uint64_t) count);
}

int mcbor_encode_map_start(mcbor_encoder_t * enc, size_t count) {
  return mcbor_encode_head(enc, 5, (mcbor_uint64_t) count);
}

int mcbor_encode_tag(mcbor_encoder_t * enc, mcbor_uint64_t tag_number) {
  return mcbor_encode_head(enc, 6, tag_number);
}

/*  Without MCBOR_ENCODE_PREFERRED a float goes back out at the width it came in
    at, as long as that width can still name it.  */
static int mcbor_encode_typed_float(mcbor_encoder_t * enc, const mcbor_value_t * value) {
  double v = value->data.float_val;
  if (enc->flags & MCBOR_ENCODE_PREFERRED) return mcbor_encode_float(enc, v);
  if (value->type == MCBOR_TYPE_FLOAT16 && mcbor_can_encode_half(v))
    return mcbor_encode_float16(enc, v);
  if (value->type != MCBOR_TYPE_FLOAT64 && mcbor_can_encode_single(v))
    return mcbor_encode_float32(enc, (float) v);
  return mcbor_encode_float64(enc, v);
}

static int mcbor_encode_value(mcbor_encoder_t * enc, const mcbor_value_t * value, int depth) {
  size_t i;
  if (!value) return mcbor_enc_fail(enc, MCBOR_ERR_INVALID_ARG);
  if (depth > MCBOR_MAX_DEPTH) return mcbor_enc_fail(enc, MCBOR_ERR_DEPTH);
  switch (value->type) {
    case MCBOR_TYPE_UINT: return mcbor_encode_uint(enc, value->data.uint_val);
    case MCBOR_TYPE_NEGINT: return mcbor_encode_negint(enc, value->data.negint_val);
    case MCBOR_TYPE_BYTES:
      return mcbor_encode_bytes(enc, value->data.bytes.data, value->data.bytes.len);
    case MCBOR_TYPE_TEXT:
      return mcbor_encode_text(enc, (const char *) value->data.bytes.data,
                               value->data.bytes.len);
    case MCBOR_TYPE_ARRAY:
      if (mcbor_encode_array_start(enc, value->data.array.count) < 0) return -1;
      for (i = 0; i < value->data.array.count; i++)
        if (mcbor_encode_value(enc, &value->data.array.items[i], depth + 1) < 0) return -1;
      return 0;
    case MCBOR_TYPE_MAP:
      if (mcbor_encode_map_start(enc, value->data.map.count) < 0) return -1;
      for (i = 0; i < value->data.map.count; i++) {
        if (mcbor_encode_value(enc, &value->data.map.keys[i], depth + 1) < 0) return -1;
        if (mcbor_encode_value(enc, &value->data.map.values[i], depth + 1) < 0) return -1;
      }
      return 0;
    case MCBOR_TYPE_TAG:
      if (mcbor_encode_tag(enc, value->data.tag.tag_number) < 0) return -1;
      return mcbor_encode_value(enc, value->data.tag.content, depth + 1);
    case MCBOR_TYPE_FALSE: return mcbor_encode_bool(enc, 0);
    case MCBOR_TYPE_TRUE: return mcbor_encode_bool(enc, 1);
    case MCBOR_TYPE_NULL: return mcbor_encode_null(enc);
    case MCBOR_TYPE_UNDEFINED: return mcbor_encode_undefined(enc);
    case MCBOR_TYPE_SIMPLE: return mcbor_encode_simple(enc, value->data.simple_val);
    case MCBOR_TYPE_BREAK: return mcbor_encode_break(enc);
    case MCBOR_TYPE_FLOAT16:  case MCBOR_TYPE_FLOAT32:  case MCBOR_TYPE_FLOAT64:
      return mcbor_encode_typed_float(enc, value);
    default: return mcbor_enc_fail(enc, MCBOR_ERR_INVALID_ARG);
  }
}

int mcbor_encode(mcbor_encoder_t * enc, const mcbor_value_t * value) {
  return mcbor_encode_value(enc, value, 0);
}

/*  Value construction.  */

static mcbor_value_t * mcbor_value_new(mcbor_arena_t * arena, mcbor_type_t type) {
  mcbor_uint8_t flags;
  mcbor_value_t * v = (mcbor_value_t *) mcbor_arena_alloc(arena, sizeof(mcbor_value_t), &flags);
  if (!v) return NULL;
  memset(v, 0, sizeof(mcbor_value_t));
  v->type = type;  v->flags = flags;  v->arena = arena;
  return v;
}

mcbor_value_t * mcbor_uint_new(mcbor_arena_t * arena, mcbor_uint64_t value) {
  mcbor_value_t * v = mcbor_value_new(arena, MCBOR_TYPE_UINT);
  if (v) v->data.uint_val = value;
  return v;
}

mcbor_value_t * mcbor_negint_new(mcbor_arena_t * arena, mcbor_uint64_t n) {
  mcbor_value_t * v = mcbor_value_new(arena, MCBOR_TYPE_NEGINT);
  if (v) v->data.negint_val = n;
  return v;
}

mcbor_value_t * mcbor_int_new(mcbor_arena_t * arena, mcbor_int64_t value) {
  return value >= 0 ? mcbor_uint_new(arena, (mcbor_uint64_t) value)
                    : mcbor_negint_new(arena, (mcbor_uint64_t)(-1 - value));
}

static mcbor_value_t * mcbor_str_new(mcbor_arena_t * arena, mcbor_type_t type,
                                     const unsigned char * data, size_t len) {
  mcbor_uint8_t flags;  mcbor_value_t * v;
  if (len && !data) return NULL;
  if (!(v = mcbor_value_new(arena, type))) return NULL;
  if (len) {
    v->data.bytes.data = (unsigned char *) mcbor_arena_alloc(arena, len, &flags);
    if (!v->data.bytes.data) return NULL;
    memcpy(v->data.bytes.data, data, len);
    v->flags |= mcbor_data_flags(flags);
  }
  v->data.bytes.len = len;
  return v;
}

mcbor_value_t * mcbor_bytes_new(mcbor_arena_t * arena, const unsigned char * data, size_t len) {
  return mcbor_str_new(arena, MCBOR_TYPE_BYTES, data, len);
}

mcbor_value_t * mcbor_text_new(mcbor_arena_t * arena, const char * str, size_t len) {
  return mcbor_str_new(arena, MCBOR_TYPE_TEXT, (const unsigned char *) str, len);
}

mcbor_value_t * mcbor_text_cstr_new(mcbor_arena_t * arena, const char * str) {
  return str ? mcbor_text_new(arena, str, strlen(str)) : NULL;
}

mcbor_value_t * mcbor_array_new(mcbor_arena_t * arena, size_t capacity) {
  mcbor_uint8_t flags;  mcbor_value_t * v;
  if (capacity > MCBOR_SIZE_MAX / sizeof(mcbor_value_t)) return NULL;
  if (!(v = mcbor_value_new(arena, MCBOR_TYPE_ARRAY))) return NULL;
  if (capacity) {
    v->data.array.items = (mcbor_value_t *)
      mcbor_arena_alloc(arena, capacity * sizeof(mcbor_value_t), &flags);
    if (!v->data.array.items) return NULL;
    v->flags |= mcbor_data_flags(flags);
  }
  v->data.array.capacity = capacity;
  return v;
}

mcbor_value_t * mcbor_map_new(mcbor_arena_t * arena, size_t capacity) {
  mcbor_uint8_t flags;  mcbor_value_t * v, * cells;
  if (capacity > MCBOR_SIZE_MAX / (2 * sizeof(mcbor_value_t))) return NULL;
  if (!(v = mcbor_value_new(arena, MCBOR_TYPE_MAP))) return NULL;
  if (capacity) {
    cells = (mcbor_value_t *)
      mcbor_arena_alloc(arena, 2 * capacity * sizeof(mcbor_value_t), &flags);
    if (!cells) return NULL;
    v->data.map.keys = cells;  v->data.map.values = cells + capacity;
    v->flags |= mcbor_data_flags(flags);
  }
  v->data.map.capacity = capacity;
  return v;
}

mcbor_value_t * mcbor_tag_new(mcbor_arena_t * arena, mcbor_uint64_t tag_number,
                              mcbor_value_t * content) {
  mcbor_value_t * v = mcbor_value_new(arena, MCBOR_TYPE_TAG);
  if (v) { v->data.tag.tag_number = tag_number;  v->data.tag.content = content; }
  return v;
}

mcbor_value_t * mcbor_float_new(mcbor_arena_t * arena, double value) {
  mcbor_value_t * v = mcbor_value_new(arena, MCBOR_TYPE_FLOAT64);
  if (v) v->data.float_val = value;
  return v;
}

mcbor_value_t * mcbor_simple_new(mcbor_arena_t * arena, mcbor_uint8_t value) {
  mcbor_value_t * v = mcbor_value_new(arena, MCBOR_TYPE_SIMPLE);
  if (v) v->data.simple_val = value;
  return v;
}

mcbor_value_t * mcbor_bool_new(mcbor_arena_t * arena, int value) {
  return mcbor_value_new(arena, value ? MCBOR_TYPE_TRUE : MCBOR_TYPE_FALSE);
}

mcbor_value_t * mcbor_null_new(mcbor_arena_t * arena) {
  return mcbor_value_new(arena, MCBOR_TYPE_NULL);
}

mcbor_value_t * mcbor_undefined_new(mcbor_arena_t * arena) {
  return mcbor_value_new(arena, MCBOR_TYPE_UNDEFINED);
}

int mcbor_array_append(mcbor_arena_t * arena, mcbor_value_t * arr, mcbor_value_t * item) {
  mcbor_value_t * items;  mcbor_uint8_t flags;  size_t capacity, i;
  if (!arr || arr->type != MCBOR_TYPE_ARRAY || !item) return MCBOR_ERR_INVALID_ARG;
  if (arr->flags & MCBOR_FLAG_STOLEN) return MCBOR_ERR_INVALID_ARG;
  if (!arena) arena = arr->arena;
  if (arr->data.array.count == arr->data.array.capacity) {
    capacity = arr->data.array.capacity ? arr->data.array.capacity * 2 : 4;
    if (capacity > MCBOR_SIZE_MAX / sizeof(mcbor_value_t)) return MCBOR_ERR_OVERFLOW;
    items = (mcbor_value_t *) mcbor_arena_alloc(arena, capacity * sizeof(mcbor_value_t), &flags);
    if (!items) return MCBOR_ERR_NO_MEMORY;
    for (i = 0; i < arr->data.array.count; i++) items[i] = arr->data.array.items[i];
    arr->data.array.items = items;  arr->data.array.capacity = capacity;
    arr->flags = (mcbor_uint8_t)((arr->flags & ~MCBOR_FLAG_DATA_HEAP) | mcbor_data_flags(flags));
  }
  arr->data.array.items[arr->data.array.count++] = *item;
  return MCBOR_OK;
}

int mcbor_map_put(mcbor_arena_t * arena, mcbor_value_t * map, mcbor_value_t * key,
                  mcbor_value_t * value) {
  mcbor_value_t * cells;  mcbor_uint8_t flags;  size_t capacity, i;
  if (!map || map->type != MCBOR_TYPE_MAP || !key || !value) return MCBOR_ERR_INVALID_ARG;
  if (map->flags & MCBOR_FLAG_STOLEN) return MCBOR_ERR_INVALID_ARG;
  if (!arena) arena = map->arena;
  if (map->data.map.count == map->data.map.capacity) {
    capacity = map->data.map.capacity ? map->data.map.capacity * 2 : 4;
    if (capacity > MCBOR_SIZE_MAX / (2 * sizeof(mcbor_value_t))) return MCBOR_ERR_OVERFLOW;
    cells = (mcbor_value_t *)
      mcbor_arena_alloc(arena, 2 * capacity * sizeof(mcbor_value_t), &flags);
    if (!cells) return MCBOR_ERR_NO_MEMORY;
    for (i = 0; i < map->data.map.count; i++)
      { cells[i] = map->data.map.keys[i];  cells[capacity + i] = map->data.map.values[i]; }
    map->data.map.keys = cells;  map->data.map.values = cells + capacity;
    map->data.map.capacity = capacity;
    map->flags = (mcbor_uint8_t)((map->flags & ~MCBOR_FLAG_DATA_HEAP) | mcbor_data_flags(flags));
  }
  map->data.map.keys[map->data.map.count] = *key;
  map->data.map.values[map->data.map.count] = *value;
  map->data.map.count++;
  return MCBOR_OK;
}

/*  Entry order counts: two maps that differ only in order are not equal.  */
static int mcbor_values_equal(const mcbor_value_t * a, const mcbor_value_t * b, int depth) {
  size_t i;
  if (!a || !b || a->type != b->type || depth > MCBOR_MAX_DEPTH) return 0;
  switch (a->type) {
    case MCBOR_TYPE_UINT: return a->data.uint_val == b->data.uint_val;
    case MCBOR_TYPE_NEGINT: return a->data.negint_val == b->data.negint_val;
    case MCBOR_TYPE_BYTES:  case MCBOR_TYPE_TEXT:
      if (a->data.bytes.len != b->data.bytes.len) return 0;
      return !a->data.bytes.len ||
             !memcmp(a->data.bytes.data, b->data.bytes.data, a->data.bytes.len);
    case MCBOR_TYPE_ARRAY:
      if (a->data.array.count != b->data.array.count) return 0;
      for (i = 0; i < a->data.array.count; i++)
        if (!mcbor_values_equal(&a->data.array.items[i], &b->data.array.items[i], depth + 1))
          return 0;
      return 1;
    case MCBOR_TYPE_MAP:
      if (a->data.map.count != b->data.map.count) return 0;
      for (i = 0; i < a->data.map.count; i++) {
        if (!mcbor_values_equal(&a->data.map.keys[i], &b->data.map.keys[i], depth + 1)) return 0;
        if (!mcbor_values_equal(&a->data.map.values[i], &b->data.map.values[i], depth + 1))
          return 0;
      }
      return 1;
    case MCBOR_TYPE_TAG:
      if (a->data.tag.tag_number != b->data.tag.tag_number) return 0;
      return mcbor_values_equal(a->data.tag.content, b->data.tag.content, depth + 1);
    case MCBOR_TYPE_SIMPLE: return a->data.simple_val == b->data.simple_val;
    case MCBOR_TYPE_FLOAT16:  case MCBOR_TYPE_FLOAT32:  case MCBOR_TYPE_FLOAT64:
      return a->data.float_val == b->data.float_val;
    case MCBOR_TYPE_FALSE:  case MCBOR_TYPE_TRUE:
    case MCBOR_TYPE_NULL:   case MCBOR_TYPE_UNDEFINED: return 1;
    default: return 0;
  }
}

mcbor_value_t * mcbor_map_get(const mcbor_value_t * map, const mcbor_value_t * key) {
  size_t i;
  if (!map || map->type != MCBOR_TYPE_MAP || !key) return NULL;
  for (i = 0; i < map->data.map.count; i++)
    if (mcbor_values_equal(&map->data.map.keys[i], key, 0)) return &map->data.map.values[i];
  return NULL;
}

mcbor_value_t * mcbor_map_get_text(const mcbor_value_t * map, const char * key) {
  const mcbor_value_t * k;  size_t i, len;
  if (!map || map->type != MCBOR_TYPE_MAP || !key) return NULL;
  len = strlen(key);
  for (i = 0; i < map->data.map.count; i++) {
    k = &map->data.map.keys[i];
    if (k->type == MCBOR_TYPE_TEXT && k->data.bytes.len == len &&
        (!len || !memcmp(k->data.bytes.data, key, len)))
      return &map->data.map.values[i];
  }
  return NULL;
}

/*  Value access.  */

int mcbor_is_uint(const mcbor_value_t * v) { return v && v->type == MCBOR_TYPE_UINT; }
int mcbor_is_negint(const mcbor_value_t * v) { return v && v->type == MCBOR_TYPE_NEGINT; }
int mcbor_is_bytes(const mcbor_value_t * v) { return v && v->type == MCBOR_TYPE_BYTES; }
int mcbor_is_text(const mcbor_value_t * v) { return v && v->type == MCBOR_TYPE_TEXT; }
int mcbor_is_array(const mcbor_value_t * v) { return v && v->type == MCBOR_TYPE_ARRAY; }
int mcbor_is_map(const mcbor_value_t * v) { return v && v->type == MCBOR_TYPE_MAP; }
int mcbor_is_tag(const mcbor_value_t * v) { return v && v->type == MCBOR_TYPE_TAG; }
int mcbor_is_null(const mcbor_value_t * v) { return v && v->type == MCBOR_TYPE_NULL; }
int mcbor_is_simple(const mcbor_value_t * v) { return v && v->type == MCBOR_TYPE_SIMPLE; }
int mcbor_is_undefined(const mcbor_value_t * v) { return v && v->type == MCBOR_TYPE_UNDEFINED; }
int mcbor_get_bool(const mcbor_value_t * v) { return v && v->type == MCBOR_TYPE_TRUE; }

int mcbor_is_int(const mcbor_value_t * v) {
  return v && (v->type == MCBOR_TYPE_UINT || v->type == MCBOR_TYPE_NEGINT);
}

int mcbor_is_bool(const mcbor_value_t * v) {
  return v && (v->type == MCBOR_TYPE_TRUE || v->type == MCBOR_TYPE_FALSE);
}

int mcbor_is_float(const mcbor_value_t * v) {
  return v && (v->type == MCBOR_TYPE_FLOAT16 || v->type == MCBOR_TYPE_FLOAT32 ||
               v->type == MCBOR_TYPE_FLOAT64);
}

mcbor_uint64_t mcbor_get_uint(const mcbor_value_t * v) {
  return mcbor_is_uint(v) ? v->data.uint_val : 0;
}

/*  Negative integers reach -2^64; the ones past what an int64 can name are
    pinned at its floor.  */
mcbor_int64_t mcbor_get_int(const mcbor_value_t * v) {
  if (!v) return 0;
  if (v->type == MCBOR_TYPE_UINT) return (mcbor_int64_t) v->data.uint_val;
  if (v->type != MCBOR_TYPE_NEGINT) return 0;
  if (v->data.negint_val >= ((mcbor_uint64_t) 1 << 63))
    return -((mcbor_int64_t)(((mcbor_uint64_t) 1 << 63) - 1)) - 1;
  return -1 - (mcbor_int64_t) v->data.negint_val;
}

const unsigned char * mcbor_get_bytes(const mcbor_value_t * v, size_t * len) {
  if (!mcbor_is_bytes(v)) { if (len) *len = 0;  return NULL; }
  if (len) *len = v->data.bytes.len;
  return v->data.bytes.data;
}

const char * mcbor_get_text(const mcbor_value_t * v, size_t * len) {
  if (!mcbor_is_text(v)) { if (len) *len = 0;  return NULL; }
  if (len) *len = v->data.bytes.len;
  return (const char *) v->data.bytes.data;
}

mcbor_value_t * mcbor_array_get(const mcbor_value_t * v, size_t index) {
  if (!mcbor_is_array(v) || index >= v->data.array.count) return NULL;
  return &v->data.array.items[index];
}

size_t mcbor_array_len(const mcbor_value_t * v) {
  return mcbor_is_array(v) ? v->data.array.count : 0;
}

size_t mcbor_map_len(const mcbor_value_t * v) {
  return mcbor_is_map(v) ? v->data.map.count : 0;
}

mcbor_uint64_t mcbor_get_tag_number(const mcbor_value_t * v) {
  return mcbor_is_tag(v) ? v->data.tag.tag_number : 0;
}

mcbor_value_t * mcbor_get_tag_content(const mcbor_value_t * v) {
  return mcbor_is_tag(v) ? v->data.tag.content : NULL;
}

double mcbor_get_float(const mcbor_value_t * v) {
  return mcbor_is_float(v) ? v->data.float_val : 0.0;
}

mcbor_uint8_t mcbor_get_simple(const mcbor_value_t * v) {
  return mcbor_is_simple(v) ? v->data.simple_val : 0;
}

/*  Diagnostic notation.  `len` keeps counting past the end of the buffer so
    that a truncated run still reports how much room the output wants.  */

typedef struct { char * buf;  size_t cap, len; } mcbor_diag_t;

static void mcbor_diag_put(mcbor_diag_t * d, const char * s, size_t n) {
  size_t room;
  if (d->buf && d->len < d->cap) {
    room = d->cap - d->len - 1;  /*  Leave the terminator its byte.  */
    if (room > n) room = n;
    memcpy(d->buf + d->len, s, room);
  }
  d->len += n;
}

static void mcbor_diag_str(mcbor_diag_t * d, const char * s) { mcbor_diag_put(d, s, strlen(s)); }
static void mcbor_diag_char(mcbor_diag_t * d, char c) { mcbor_diag_put(d, &c, 1); }

static void mcbor_diag_uint(mcbor_diag_t * d, mcbor_uint64_t val) {
  char tmp[20];  int i = 0;
  if (!val) { mcbor_diag_char(d, '0');  return; }
  while (val) { tmp[i++] = (char)('0' + (int)(val % 10));  val /= 10; }
  while (i > 0) mcbor_diag_char(d, tmp[--i]);
}

static void mcbor_diag_negint(mcbor_diag_t * d, mcbor_uint64_t n) {
  mcbor_diag_char(d, '-');
  /*  -1-n for the largest n is the one value that will not fit back in.  */
  if (n == (mcbor_uint64_t) -1) mcbor_diag_str(d, "18446744073709551616");
  else mcbor_diag_uint(d, n + 1);
}

static void mcbor_diag_float(mcbor_diag_t * d, double val) {
  char tmp[48];  const char * e;  double back;  int digits, exp10 = 0, prec;
  if (mcbor_is_nan(val)) { mcbor_diag_str(d, "NaN");  return; }
  if (mcbor_is_inf(val))
    { mcbor_diag_str(d, mcbor_signbit(val) ? "-Infinity" : "Infinity");  return; }
  for (digits = 1; digits < 17; digits++) {  /*  Fewest that read back equal.  */
    sprintf(tmp, "%.*e", digits - 1, val);
    if (sscanf(tmp, "%lf", &back) == 1 && back == val) break;
  }
  e = strchr(tmp, 'e');
  if (e) sscanf(e + 1, "%d", &exp10);
  /*  %g turns exponential once the exponent reaches the precision, which for a
      short mantissa would print 100000 as 1e+05.  */
  prec = (exp10 >= digits && exp10 <= 16) ? exp10 + 1 : digits;
  sprintf(tmp, "%.*g", prec, val);
  if (!strpbrk(tmp, ".eE")) strcat(tmp, ".0");  /*  Tell floats from integers.  */
  mcbor_diag_str(d, tmp);
}

static void mcbor_diag_hex(mcbor_diag_t * d, unsigned char b) {
  static const char hex[] = "0123456789abcdef";
  mcbor_diag_char(d, hex[b >> 4]);  mcbor_diag_char(d, hex[b & 0xf]);
}

static void mcbor_diag_bytes(mcbor_diag_t * d, const unsigned char * data, size_t len) {
  size_t i;
  mcbor_diag_str(d, "h'");
  for (i = 0; i < len; i++) mcbor_diag_hex(d, data[i]);
  mcbor_diag_char(d, '\'');
}

static void mcbor_diag_text(mcbor_diag_t * d, const unsigned char * data, size_t len) {
  size_t i;
  mcbor_diag_char(d, '"');
  for (i = 0; i < len; i++) {
    unsigned char c = data[i];
    if (c == '"')       mcbor_diag_str(d, "\\\"");
    else if (c == '\\') mcbor_diag_str(d, "\\\\");
    else if (c == '\n') mcbor_diag_str(d, "\\n");
    else if (c == '\r') mcbor_diag_str(d, "\\r");
    else if (c == '\t') mcbor_diag_str(d, "\\t");
    else if (c < 0x20)  { mcbor_diag_str(d, "\\u00");  mcbor_diag_hex(d, c); }
    else                mcbor_diag_char(d, (char) c);
  }
  mcbor_diag_char(d, '"');
}

static void mcbor_diag_value(mcbor_diag_t * d, const mcbor_value_t * value, int depth) {
  size_t i;
  if (!value) { mcbor_diag_str(d, "null");  return; }
  if (depth > MCBOR_MAX_DEPTH) { mcbor_diag_str(d, "...");  return; }
  switch (value->type) {
    case MCBOR_TYPE_UINT: mcbor_diag_uint(d, value->data.uint_val);  break;
    case MCBOR_TYPE_NEGINT: mcbor_diag_negint(d, value->data.negint_val);  break;
    case MCBOR_TYPE_BYTES:
      mcbor_diag_bytes(d, value->data.bytes.data, value->data.bytes.len);  break;
    case MCBOR_TYPE_TEXT:
      mcbor_diag_text(d, value->data.bytes.data, value->data.bytes.len);  break;
    case MCBOR_TYPE_ARRAY:
      mcbor_diag_char(d, '[');
      for (i = 0; i < value->data.array.count; i++) {
        if (i) mcbor_diag_str(d, ", ");
        mcbor_diag_value(d, &value->data.array.items[i], depth + 1);
      }
      mcbor_diag_char(d, ']');
      break;
    case MCBOR_TYPE_MAP:
      mcbor_diag_char(d, '{');
      for (i = 0; i < value->data.map.count; i++) {
        if (i) mcbor_diag_str(d, ", ");
        mcbor_diag_value(d, &value->data.map.keys[i], depth + 1);
        mcbor_diag_str(d, ": ");
        mcbor_diag_value(d, &value->data.map.values[i], depth + 1);
      }
      mcbor_diag_char(d, '}');
      break;
    case MCBOR_TYPE_TAG:
      mcbor_diag_uint(d, value->data.tag.tag_number);
      mcbor_diag_char(d, '(');
      mcbor_diag_value(d, value->data.tag.content, depth + 1);
      mcbor_diag_char(d, ')');
      break;
    case MCBOR_TYPE_SIMPLE:
      mcbor_diag_str(d, "simple(");
      mcbor_diag_uint(d, value->data.simple_val);
      mcbor_diag_char(d, ')');
      break;
    case MCBOR_TYPE_FLOAT16:  case MCBOR_TYPE_FLOAT32:  case MCBOR_TYPE_FLOAT64:
      mcbor_diag_float(d, value->data.float_val);  break;
    case MCBOR_TYPE_FALSE: mcbor_diag_str(d, "false");  break;
    case MCBOR_TYPE_TRUE: mcbor_diag_str(d, "true");  break;
    case MCBOR_TYPE_NULL: mcbor_diag_str(d, "null");  break;
    case MCBOR_TYPE_UNDEFINED: mcbor_diag_str(d, "undefined");  break;
    case MCBOR_TYPE_BREAK: mcbor_diag_str(d, "break");  break;
    default: mcbor_diag_str(d, "<unknown>");  break;
  }
}

int mcbor_to_diagnostic(const mcbor_value_t * value, char * buf, size_t buflen) {
  mcbor_diag_t d;
  if (!buf || !buflen) return MCBOR_ERR_INVALID_ARG;
  d.buf = buf;  d.cap = buflen;  d.len = 0;
  mcbor_diag_value(&d, value, 0);
  buf[d.len < buflen ? d.len : buflen - 1] = '\0';
  return d.len < buflen ? MCBOR_OK : MCBOR_ERR_BUFFER_FULL;
}

size_t mcbor_diagnostic_len(const mcbor_value_t * value) {
  mcbor_diag_t d;
  d.buf = NULL;  d.cap = d.len = 0;
  mcbor_diag_value(&d, value, 0);
  return d.len;
}

#endif /* MCBOR_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* MINICBOR_H */
