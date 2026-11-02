/*
 * YMLParser.h — single-header YAML 1.2.2 parser (C11).
 *
 * ── Usage ──────────────────────────────────────────────────────────
 *
 *   In ONE .c file before include:
 *     #define YMLPARSER_IMPLEMENTATION
 *     #include "YMLParser.h"
 *
 *   In all other files:
 *     #include "YMLParser.h"
 *
 * ── Requirements ────────────────────────────────────────────────────
 *
 *   Compiler: C11 or newer  (_Thread_local, <stdbool.h>, <stdint.h>)
 *   Flags:    -lm            (HUGE_VAL / NAN from <math.h>)
 *
 * ── Custom allocator ────────────────────────────────────────────────
 *
 *   By default all heap operations go through malloc/realloc/calloc/free.
 *   Replace the global allocator before YMLPARSER_IMPLEMENTATION:
 *
 *     YMLParserSetAllocator((struct _YMLParserAllocator){
 *         .alloc   = my_alloc,    // void* f(size_t, void* ctx, file, line)
 *         .realloc = my_realloc,  // void* f(void*, size_t, void* ctx, file, line)
 *         .calloc  = my_calloc,   // void* f(size_t n, size_t sz, void* ctx, file, line)
 *         .dealloc = my_free,     // void  f(void*, void* ctx, file, line)
 *         .ctx     = &my_ctx,     // forwarded to every call (may be NULL)
 *     });
 *
 *   __FILE__ and __LINE__ are passed for diagnostics; ignore if not needed.
 */

#ifndef YMLPARSER_H
#define YMLPARSER_H

/* ════════════════════════ PUBLIC API ════════════════════════ */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * YMLValueType — type of a stored value.
 *
 *   YML_NULL    no payload
 *   YML_BOOL    bool
 *   YML_INT     int64_t  (integers: 42, 0xFF, 0o17)
 *   YML_FLOAT   double   (floats: 3.14, .inf, .nan)
 *   YML_STRING  const char* (heap-allocated, NUL-terminated, owned by YMLValue)
 *   YML_OBJECT  hm<str, YMLValue>  — access via YMLMapGet / YMLMapForech
 *   YML_ARRAY   da<YMLValue>       — index with value.array[i], length via YMLArrayLen
 *
 * YML_ANY — sentinel only for _YMLOptionals.type (means "skip type check").
 *           Not a valid node type.
 */
typedef enum YMLValueType
{
	YML_ANY = -1,
	YML_NULL = 0,
	YML_BOOL,
	YML_INT,
	YML_FLOAT,
	YML_STRING,
	YML_ARRAY,
	YML_OBJECT,
} YMLValueType;


struct YMLAllocator {
	void* (*alloc)  (size_t len,               void* ctx, const char* FILE, int LINE);
	void* (*realloc)(void* ptr, size_t new_len, void* ctx, const char* FILE, int LINE);
	void* (*calloc) (size_t n, size_t size,     void* ctx, const char* FILE, int LINE);
	void  (*dealloc)(void* ptr,                 void* ctx, const char* FILE, int LINE);
	void* ctx;
};


/*
 * YMLValue — a single node in the parse tree.
 *
 * The type field determines which union member is active:
 *   YML_BOOL   → value.boolean
 *   YML_INT    → value.integer
 *   YML_FLOAT  → value.number
 *   YML_STRING → value.string  (pointer to a NUL-terminated string)
 *   YML_OBJECT → value.object  (hm<str → YMLValue>)
 *   YML_ARRAY  → value.array   (da<YMLValue>, hidden header, see YMLArrayLen)
 *   YML_NULL   → value is unused
 */
typedef struct YMLValue
{
	YMLValueType type;
	union
	{
		bool boolean;
		int64_t integer;
		double number;
		const char *string;
		void *object;			// _hm* — opaque pointer, access via YMLMapGet/YMLMapForech
		struct YMLValue *array; // da<YMLValue> — direct indexing arr[i], length via YMLArrayLen
	} value;
	struct YMLAllocator* allocator; 
} YMLValue;

/*
 * Hidden header of dynamic arrays da<T>.
 * Stored immediately before the first element in memory:
 *   [__da_header][elem0][elem1]...
 * The user holds a pointer to elem0.
 * All da arrays are freed by YMLDestroy / YMLDestroyStream —
 * copy any data you need before destroying the tree.
 */
typedef struct
{
	size_t len;
	size_t cap;
	struct YMLAllocator* alloc;
} __da_header;

/*
 * Returns the number of elements in a da array.
 * Example: for (size_t i = 0; i < YMLArrayLen(arr); i++) { ... arr[i] ... }
 */
#define YMLArrayLen(arr) ((arr) ? ((__da_header *)(arr) - 1)->len : (size_t)0)

struct _YMLOptionals
{
	int *ok;		   // 0 — no error, otherwise error code
	char **error;	   // *error points to a buffer with the error message
	YMLValueType type; // for YMLMapGet: expected type (YML_ANY — skip check)
	char splitter;	   // for YMLMapGet: if non-zero, split key by this char and traverse nested objects
	struct YMLAllocator* allocator;
};

/*
 * Parses a single YAML document and returns the root YMLValue*.
 * If the string contains multiple documents (---), only the first is parsed.
 *
 * Error codes for ok:
 *   1 — syntax error
 *   2 — out of memory
 */
YMLValue *_YMLParse(const char *yml_str, struct _YMLOptionals optionals);

#define YMLParse(yml_str, ...) \
	_YMLParse(yml_str, (struct _YMLOptionals){.ok = NULL, .error = NULL, .type = YML_ANY, __VA_ARGS__})

/*
 * Parses a YAML stream with multiple documents (separated by ---).
 * Returns da<YMLValue*> — an array of root nodes, one per document.
 * Get the length via YMLArrayLen.
 *
 * Example:
 *   YMLValue **docs = YMLParseStream(yml_str);
 *   for (size_t i = 0; i < YMLArrayLen(docs); i++) { ... docs[i] ... }
 *   YMLDestroyStream(docs);
 *
 * Error codes for ok: same as YMLParse.
 */
YMLValue **_YMLParseStream(const char *yml_str, struct _YMLOptionals optionals);

#define YMLParseStream(yml_str, ...) \
	_YMLParseStream(yml_str, (struct _YMLOptionals){.ok = NULL, .error = NULL, .type = YML_ANY, __VA_ARGS__})

/*
 * Frees the memory of a single-document tree (from YMLParse).
 */
void _YMLDestroy(YMLValue *root, struct _YMLOptionals optionals);

#define YMLDestroy(root, ...) \
	_YMLDestroy(root, (struct _YMLOptionals){.ok = NULL, .error = NULL, .type = YML_ANY, __VA_ARGS__})

/*
 * Frees all documents in a stream and the da array itself (from YMLParseStream).
 */
void _YMLDestroyStream(YMLValue **stream, struct _YMLOptionals optionals);

#define YMLDestroyStream(stream, ...) \
	_YMLDestroyStream(stream, (struct _YMLOptionals){.ok = NULL, .error = NULL, .type = YML_ANY, __VA_ARGS__})

/*
 * If the last operation produced an error, prints it to stderr
 * and returns the error code. Otherwise returns 0.
 */
int YMLPrintError(void);

/*
 * Returns a YMLValue* by string key from a YML_OBJECT.
 * If the key is not found — error (ok=1).
 * If .type != YML_ANY and the value type does not match — error (ok=2).
 *
 * Error codes for ok:
 *   1 — key not found
 *   2 — type mismatch
 */
/* hm — YMLValue.value.object pointer (void* = _hm*) */
YMLValue *_YMLMapGet(void *hm, const char *key, struct _YMLOptionals optionals);

#define YMLMapGet(object, key, ...) \
	_YMLMapGet(object, key, (struct _YMLOptionals){.ok = NULL, .error = NULL, .type = YML_ANY, .splitter = 0, __VA_ARGS__})


#define __YML_CTYPE_YML_BOOL    bool
#define __YML_CTYPE_YML_INT     int64_t
#define __YML_CTYPE_YML_FLOAT   double
#define __YML_CTYPE_YML_STRING  const char*
#define __YML_CTYPE_YML_ARRAY   YMLValue*
#define __YML_CTYPE_YML_OBJECT  void*

#define YMLTYPE_2_CTYPE(type) \
	__YML_CTYPE_##type


/*
 * YMLMapGetValue — like YMLMapGet, but returns the C value directly instead of YMLValue*.
 * TYPE must be one of: YML_BOOL, YML_INT, YML_FLOAT, YML_STRING, YML_ARRAY, YML_OBJECT.
 * The return type is resolved at compile time via _Generic:
 *   YML_BOOL   → bool
 *   YML_INT    → int64_t
 *   YML_FLOAT  → double
 *   YML_STRING → const char*
 *   YML_ARRAY  → YMLValue*
 *   YML_OBJECT → void*
 *
 * Sets .type=TYPE automatically; optional args (.ok, .error, .splitter) are forwarded.
 *
 * WARNING: if the key is not found or the type does not match, YMLMapGet returns YMLVallue {.type=YML_NULL, .value=NULL}
 *
 * Examples:
 *   int64_t     age  = YMLMapGetValue(obj, "age",   YML_INT);
 *   const char *name = YMLMapGetValue(obj, "name",  YML_STRING, .ok=&ok);
 *   double      x    = YMLMapGetValue(obj, "score", YML_FLOAT,  .splitter='.');
 */
#define YMLMapGetValue(obj, key, TYPE, ...) \
	_Generic(((YMLTYPE_2_CTYPE(TYPE))0), \
		double:        YMLMapGet(obj, key, .type=TYPE, __VA_ARGS__)->value.number, \
		int64_t:       YMLMapGet(obj, key, .type=TYPE, __VA_ARGS__)->value.integer, \
		bool:          YMLMapGet(obj, key, .type=TYPE, __VA_ARGS__)->value.boolean, \
		const char*:   YMLMapGet(obj, key, .type=TYPE, __VA_ARGS__)->value.string, \
		char*:         YMLMapGet(obj, key, .type=TYPE, __VA_ARGS__)->value.string, \
		void*:         YMLMapGet(obj, key, .type=TYPE, __VA_ARGS__)->value.object, \
		YMLValue*:     YMLMapGet(obj, key, .type=TYPE, __VA_ARGS__)->value.array \
	)

/*
 * Iterator over key-value pairs of a YML_OBJECT.
 * Used internally by the YMLMapForech macro.
 */
/* _hm — opaque pointer to the internal hm structure, _i — current slot index. */
typedef struct
{
	void *_hm;
	size_t _i;
} _YMLMapIter;
/* hm — YMLValue.value.object pointer */
_YMLMapIter _YMLMapIterBegin(void *hm);
bool _YMLMapIterNext(_YMLMapIter *iter, const char **key, YMLValue **value);

/*
 * Iterate over all key-value pairs of an object.
 * key_name and val_name are variable names declared by the macro inside the loop.
 *
 * Example:
 *   YMLMapForech(root->value.object, key, val) {
 *       printf("%s\n", key);
 *   }
 */
#define YMLMapForech(object, key_name, val_name)                                                         \
	for (_YMLMapIter _yml_it_ = _YMLMapIterBegin(object), *_yml_p_ = &_yml_it_; _yml_p_; _yml_p_ = NULL) \
		for (const char *(key_name) = NULL; _yml_p_; _yml_p_ = NULL)                                     \
			for (YMLValue * (val_name) = NULL; _YMLMapIterNext(&_yml_it_, (const char **)&(key_name), &(val_name));)

/* ════════════════════════ WRITER API ════════════════════════════════ */

#include <stdio.h>

/*
 * Options for YMLWrite / YMLWriteBuf.
 *
 * .indent  — spaces per nesting level (default 2)
 * .start   — emit "---" document-start marker (default 0)
 * .ok      — 0 on success, 1 if buffer too small, 2 on OOM
 * .error   — pointer to static error message buffer
 */
struct _YMLWriteOptions
{
	int   *ok;
	char **error;
	int    indent;
	int    start;
	struct YMLAllocator* allocator;
};

/* ── Create ──────────────────────────────────────────────────────── */

/*
 * Allocate an empty YML_OBJECT node.
 * Free with YMLDestroy.
 */
YMLValue *_YMLCreate(struct YMLAllocator* alloc);

#define YMLCreate(...) _YMLCreate(__VA_ARGS__)
	
YMLValue *_YMLCreateArr(struct YMLAllocator* alloc);

#define YMLCreateArr(...) _YMLCreateArr(__VA_ARGS__)

/* ── YMLMapAdd ───────────────────────────────────────────────────── */

/* Add a null value: YMLMapAddNull(obj, "key"); */
void YMLMapAddNull (YMLValue *obj, const char *key);

void _YMLMapAdd_bool (YMLValue *obj, const char *key, bool       val);
void _YMLMapAdd_int  (YMLValue *obj, const char *key, long long  val);
void _YMLMapAdd_float(YMLValue *obj, const char *key, double     val);
void _YMLMapAdd_str  (YMLValue *obj, const char *key, const char *val);
void _YMLMapAdd_node (YMLValue *obj, const char *key, YMLValue   *val);

void _YMLMapAddArr_int  (YMLValue *obj, const char *key, const long long   *arr, size_t len);
void _YMLMapAddArr_float(YMLValue *obj, const char *key, const double      *arr, size_t len);
void _YMLMapAddArr_str  (YMLValue *obj, const char *key, const char *const *arr, size_t len);

/*
 * Add a scalar or nested node. Type is inferred via _Generic.
 *
 *   YMLMapAdd(obj, "n", 42);           → YML_INT
 *   YMLMapAdd(obj, "f", 3.14);         → YML_FLOAT
 *   YMLMapAdd(obj, "s", "hello");      → YML_STRING (strdup'd)
 *   YMLMapAdd(obj, "b", (bool)true);   → YML_BOOL
 *   YMLMapAdd(obj, "sub", node);       → deep-copied YMLValue*
 *
 * Duplicate keys are overwritten (old value freed).
 */
#define YMLMapAdd(obj, key, val) _Generic((val),        \
    _Bool:        _YMLMapAdd_bool,                      \
    int:          _YMLMapAdd_int,                       \
    long:         _YMLMapAdd_int,                       \
    long long:    _YMLMapAdd_int,                       \
    float:        _YMLMapAdd_float,                     \
    double:       _YMLMapAdd_float,                     \
    char *:       _YMLMapAdd_str,                       \
    const char *: _YMLMapAdd_str,                       \
    YMLValue *:   _YMLMapAdd_node                       \
)(obj, key, val)

/*
 * Add a C array as a YML_ARRAY value.
 *
 *   long long nums[] = {1, 2, 3};
 *   YMLMapAddArr(obj, "nums", nums, 3);
 *
 *   const char *tags[] = {"a", "b"};
 *   YMLMapAddArr(obj, "tags", tags, 2);
 */
#define YMLMapAddArr(obj, key, arr, len) _Generic((arr),  \
    long long *:        _YMLMapAddArr_int,                \
    const long long *:  _YMLMapAddArr_int,                \
    double *:           _YMLMapAddArr_float,              \
    const double *:     _YMLMapAddArr_float,              \
    char **:            _YMLMapAddArr_str,                \
    const char **:      _YMLMapAddArr_str,                \
    const char *const*: _YMLMapAddArr_str                 \
)(obj, key, arr, len)

/* ── YMLArrPush ──────────────────────────────────────────────────── */

/* Push a null element: YMLArrPushNull(arr); */
void YMLArrPushNull (YMLValue *arr);

void _YMLArrPush_bool (YMLValue *arr, bool       val);
void _YMLArrPush_int  (YMLValue *arr, long long  val);
void _YMLArrPush_float(YMLValue *arr, double     val);
void _YMLArrPush_str  (YMLValue *arr, const char *val);
void _YMLArrPush_node (YMLValue *arr, YMLValue   *val);

void _YMLArrPushArr_int  (YMLValue *arr, const long long   *c_arr, size_t len);
void _YMLArrPushArr_float(YMLValue *arr, const double      *c_arr, size_t len);
void _YMLArrPushArr_str  (YMLValue *arr, const char *const *c_arr, size_t len);


/*
 * Push a scalar or nested node.
 *
 *   YMLArrPush(arr, 1);
 *   YMLArrPush(arr, "two");
 *   YMLArrPush(arr, (bool)true);
 *   YMLArrPush(arr, node);
 */
#define YMLArrPush(arr, val) _Generic((val),            \
    _Bool:        _YMLArrPush_bool,                     \
    int:          _YMLArrPush_int,                      \
    long:         _YMLArrPush_int,                      \
    long long:    _YMLArrPush_int,                      \
    float:        _YMLArrPush_float,                    \
    double:       _YMLArrPush_float,                    \
    char *:       _YMLArrPush_str,                      \
    const char *: _YMLArrPush_str,                      \
    YMLValue *:   _YMLArrPush_node                      \
)(arr, val)

/*
 * Push a C array as a nested YML_ARRAY element.
 */
#define YMLArrPushArr(arr, c_arr, len) _Generic((c_arr),  \
    long long *:        _YMLArrPushArr_int,               \
    const long long *:  _YMLArrPushArr_int,               \
    double *:           _YMLArrPushArr_float,             \
    const double *:     _YMLArrPushArr_float,             \
    char **:            _YMLArrPushArr_str,               \
    const char **:      _YMLArrPushArr_str,               \
    const char *const*: _YMLArrPushArr_str                \
)(arr, c_arr, len)

/* ── YMLWrite / YMLWriteBuf ──────────────────────────────────────── */

void _YMLWriteStream(YMLValue *obj, FILE *stream, struct _YMLWriteOptions opts);
int  _YMLWriteBuf   (YMLValue *obj, char *buf, size_t cap, struct _YMLWriteOptions opts);

/*
 * Serialize obj to a FILE* stream.
 *
 *   YMLWrite(obj, stdout);
 *   YMLWrite(obj, fp, .indent=4, .start=1);
 */
#define YMLWriteStream(obj, stream, ...) \
	_YMLWriteStream(obj, stream, \
		(struct _YMLWriteOptions){.indent = 2, .start = 0, ##__VA_ARGS__})

/*
 * Serialize obj into a char buffer. Returns bytes written (without NUL).
 * Returns -1 if the buffer is too small (.ok set to 1).
 *
 *   char buf[1024];
 *   int n = YMLWriteBuf(obj, buf, sizeof(buf));
 *   int n = YMLWriteBuf(obj, buf, sizeof(buf), .start=1, .ok=&ok);
 */
#define YMLWriteBuf(obj, buf, cap, ...) \
	_YMLWriteBuf(obj, buf, cap, \
		(struct _YMLWriteOptions){.indent = 2, .start = 0, ##__VA_ARGS__})

#ifdef YMLPARSER_IMPLEMENTATION
/* YML_PRIVATE — hides internal symbols. */
#define YML_PRIVATE static

/* ── src/_allocator.h ──────────────────────────────────────────── */
#ifndef YML_PRIVATE
#	define YML_PRIVATE
#endif


YML_PRIVATE void* _yml_alloc(size_t len, struct YMLAllocator* ctx, const char* FILE, int LINE);
YML_PRIVATE void* _yml_realloc(void* ptr, size_t new_len, struct YMLAllocator* ctx, const char* FILE, int LINE);
YML_PRIVATE void* _yml_calloc(size_t n, size_t size, struct YMLAllocator* ctx, const char* FILE, int LINE);
YML_PRIVATE void  _yml_dealloc(void* ptr, struct YMLAllocator* ctx, const char* FILE, int LINE);

#define YMLALLOC(len, alloc)            (_yml_alloc((len), (alloc), __FILE__, __LINE__))
#define YMLREALLOC(ptr, new_len, alloc) (_yml_realloc((ptr), (new_len), (alloc), __FILE__, __LINE__))
#define YMLCALLOC(n, size, alloc)       (_yml_calloc((n), (size), (alloc), __FILE__, __LINE__))
#define YMLDEALLOC(ptr, alloc)          (_yml_dealloc((ptr), (alloc), __FILE__, __LINE__))

/* ── src/_da.h ─────────────────────────────────────────────────── */
/*
 * _da.h — dynamic array со скрытым заголовком.
 *
 * Компоновка памяти:
 *   [_da_hdr | elem0 | elem1 | ...]
 *                ^
 *                пользователю возвращается указатель сюда
 *
 * _da_hdr содержит len и cap — пользователь видит только len через YMLArrayLen().
 * Массив нельзя изменять снаружи — только читать через [i] и YMLArrayLen.
 * Освобождение: _da_free(da).
 */

#include <stddef.h>

#ifndef YML_PRIVATE
#	define YML_PRIVATE
#endif

typedef struct
{
	size_t len;
	size_t cap;
	struct YMLAllocator* alloc;
} _da_hdr;

/* Выделить новый da для элементов размером elem_size. */
YML_PRIVATE void *_da_new(size_t elem_size, size_t cap, struct YMLAllocator* alloc);

/*
 * Добавить элемент (elem_size байт) в конец da.
 * Возвращает новый указатель на начало массива (может переаллоцироваться).
 * Вызывающий обязан обновить свой указатель.
 */
YML_PRIVATE void *_da_push(void *da, const void *elem, size_t elem_size);

/* Освободить da вместе со скрытым заголовком. */
YML_PRIVATE void _da_free(void *da);

/*
 * Типизированные макросы.
 *
 * da_new(T, cap, alloc) — выделить новый da<T> с ёмкостью cap
 * da_push(da, val)    — добавить val в конец, обновить da
 * da_len(da)          — длина (то же что YMLArrayLen)
 * da_free(da)         — освободить
 *
 * Пример:
 *   int *arr = da_new(int, 8, NULL);
 *   da_push(arr, 42);
 *   for (size_t i = 0; i < da_len(arr); i++) printf("%d\n", arr[i]);
 *   da_free(arr);
 */
#define da_new(T, cap, alloc) ((T *)_da_new(sizeof(T), (cap), (alloc)))
#define da_len(da) (((_da_hdr *)(da) - 1)->len)
#define da_push(da, val)                              \
	do                                                \
	{                                                 \
		__typeof__(val) _v = (val);                   \
		void *_r = _da_push((da), &_v, sizeof(_v));   \
		if (_r) (da) = _r;                            \
	} while (0)
#define da_free(da) _da_free(da)

/* ── src/_hm.h ─────────────────────────────────────────────────── */
/*
 * _hm.h — hash map: строковый ключ → YMLValue.
 *
 * Реализация: открытая адресация с линейным зондированием.
 * Ключи копируются через strdup и освобождаются при hm_free.
 * Значения хранятся по значению (YMLValue).
 *
 * Используется как тип поля YMLValue.value.object (через void*).
 */

#include <stdbool.h>
#include <stddef.h>

#ifndef YML_PRIVATE
#	define YML_PRIVATE
#endif

typedef struct
{
	char *key; /* strdup, NULL = пустой слот */
	YMLValue value;
} _hm_entry;

typedef struct
{
	_hm_entry *entries; /* malloc-массив слотов (не da — управляется вручную) */
	size_t cap;			/* количество слотов (степень двойки) */
	size_t len;			/* количество занятых слотов */
	struct YMLAllocator* alloc;
} _hm;

/* Выделить новую hm. cap — начальное количество слотов (округляется до степени 2). */
YML_PRIVATE _hm *hm_new(size_t cap, struct YMLAllocator* alloc);

/*
 * Вставить или обновить пару key → value.
 * key копируется через strdup.
 * Возвращает false при ошибке памяти.
 */
YML_PRIVATE bool hm_set(_hm *hm, const char *key, YMLValue value);

/*
 * Найти значение по ключу.
 * Возвращает указатель на YMLValue внутри hm (валиден до следующего hm_set/hm_free).
 * Возвращает NULL если ключ не найден.
 *
 * ВНИМАНИЕ: указатель инвалидируется при любом последующем hm_set на той же карте,
 * если он вызывает перераспределение (load > 0.7). Не сохраняйте результат
 * за пределами текущего блока кода.
 */
YML_PRIVATE YMLValue *hm_get(const _hm *hm, const char *key);

/*
 * Итерация: idx — текущий индекс (начинать с 0).
 * Возвращает true и записывает key/value если нашёл следующий занятый слот.
 * При возврате false idx указывает за конец — итерация завершена.
 *
 * Пример:
 *   size_t idx = 0;
 *   const char *key; YMLValue *val;
 *   while (hm_next(hm, &idx, &key, &val)) { ... }
 */
YML_PRIVATE bool hm_next(const _hm *hm, size_t *idx, const char **key, YMLValue **value);

/*
 * Рекурсивно освобождает все YMLValue внутри hm (строки, вложенные hm и da),
 * затем освобождает ключи и саму hm.
 */
YML_PRIVATE void hm_free(_hm *hm);

/* ── src/_lexer.h ──────────────────────────────────────────────── */
/*
 * _lexer.h — лексический анализатор YAML 1.2.2.
 *
 * Вход:  const char* (NUL-terminated YAML-строка)
 * Выход: da<Token> — массив токенов
 *
 * Лексер делает один проход по входу и производит плоский массив токенов.
 * Парсер затем обходит этот массив рекурсивным спуском.
 *
 * Особенности:
 *  - отслеживает flow_depth (0 = блочный контекст, >0 = внутри [] или {})
 *  - ':' является TK_MAP_VAL только если за ним пробел/перевод/EOF (блочный)
 *    или в flow-контексте (любой ':')
 *  - '-' является TK_SEQ_ENTRY только если за ним пробел/перевод (блочный)
 *  - якоря (&name) и алиасы (*name) разворачиваются в парсере, не в лексере
 *  - теги (!!str, !tag, !<uri>) — сохраняется строка тега
 *  - комментарии (#) и незначимые пробелы пропускаются
 *  - строки TK_SCALAR для блочных (| и >) обрабатываются полностью в лексере
 */
#include <stdbool.h>
#include <stddef.h>

#ifndef YML_PRIVATE
#	define YML_PRIVATE
#endif

typedef enum
{
	TK_DOC_START, /* ---                         */
	TK_DOC_END,	  /* ...                         */

	TK_SEQ_ENTRY, /* -<пробел> в блочном контексте */
	TK_MAP_KEY,	  /* ?<пробел> явный ключ         */
	TK_MAP_VAL,	  /* :<пробел|перевод|EOF>        */

	TK_FLOW_SEQ_START, /* [  */
	TK_FLOW_SEQ_END,   /* ]  */
	TK_FLOW_MAP_START, /* {  */
	TK_FLOW_MAP_END,   /* }  */
	TK_FLOW_ENTRY,	   /* ,  */

	TK_SCALAR, /* любой скаляр (plain, '...', "...", |, >) */
	TK_ANCHOR, /* &name  */
	TK_ALIAS,  /* *name  */
	TK_TAG,	   /* !!str / !tag / !<uri> */

	TK_EOF,
} TK_Type;

typedef enum
{
	SCALAR_PLAIN,		  /* foo, 42, true, null         */
	SCALAR_SINGLE_QUOTED, /* 'foo bar'                   */
	SCALAR_DOUBLE_QUOTED, /* "foo\nbar"                  */
	SCALAR_LITERAL,		  /* |  (сохраняет переводы строк) */
	SCALAR_FOLDED,		  /* >  (сворачивает переводы строк) */
} ScalarStyle;

typedef struct
{
	TK_Type type;
	int line; /* 1-based номер строки начала токена */
	int col;  /* 0-based столбец начала токена (= отступ для блочных) */

	/* Заполняется для TK_SCALAR, TK_ANCHOR, TK_ALIAS, TK_TAG.
	 * Указывает в исходную строку — НЕ NUL-terminated!
	 * Длина в value_len. Парсер копирует при необходимости. */
	const char *value;
	size_t value_len;

	ScalarStyle style; /* только для TK_SCALAR */
	bool owns_value;   /* true = value указывает на malloc-буфер лексера, парсер обязан free() */
} Token;

/*
 * Лексировать src и вернуть da<Token>.
 * При синтаксической ошибке:
 *   если error != NULL — записывает сообщение в *error (static буфер)
 *   возвращает NULL
 *
 * Освобождение: da_free() — токены не владеют строками (value указывает в src).
 */
YML_PRIVATE Token *lex(const char *src, const char **error);

/* ── src/_yml_utils.h ──────────────────────────────────────────── */
/*
 * _yml_utils.h — рекурсивное освобождение содержимого YMLValue + утилиты.
 *
 * Единственный источник истины для логики обхода типов при free.
 * Включается в _hm.c и YMLParser.c.
 *
 * hm_free объявлена в _hm.h и определена в _hm.c; её вызов из inline-функции
 * разрешается на этапе компоновки.
 */

#include <stdlib.h>
#include <string.h>

/*
 * yml_strdup — портативная замена strdup (не требует POSIX / _POSIX_C_SOURCE).
 */
static inline char *yml_strdup(const char *s, struct YMLAllocator* alloc)
{
	size_t n = strlen(s) + 1;
	char *copy = (char *)YMLALLOC(n, alloc);
	if (copy)
		memcpy(copy, s, n);
	return copy;
}

/*
 * Рекурсивная глубокая копия YMLValue.
 * Если target_alloc != NULL — все ноды получат этот allocator.
 * Иначе копирует allocator из src.
 * Возвращает heap-аллоцированный YMLValue* или NULL при OOM.
 * Реализация в _yml_utils.c.
 */
YML_PRIVATE YMLValue *yml_deep_copy(const YMLValue *src, struct YMLAllocator *target_alloc);

static inline void yml_value_free_impl(YMLValue *v)
{
	if (!v)
		return;
	switch (v->type)
	{
	case YML_STRING:
		YMLDEALLOC((char *)v->value.string, v->allocator);
		break;
	case YML_ARRAY:
	{
		size_t n = da_len(v->value.array);
		for (size_t i = 0; i < n; i++)
			yml_value_free_impl(&v->value.array[i]);
		da_free(v->value.array);
		break;
	}
	case YML_OBJECT:
		hm_free((_hm *)v->value.object);
		break;
	default:
		break;
	}
}

/* ── src/_da.c ─────────────────────────────────────────────────── */
#include <stdlib.h>
#include <string.h>

YML_PRIVATE void *_da_new(size_t elem_size, size_t cap, struct YMLAllocator* alloc)
{
	if (cap == 0)
		cap = 4;
	_da_hdr *hdr = YMLALLOC(sizeof(_da_hdr) + elem_size * cap, alloc);
	if (!hdr)
		return NULL;
	hdr->len = 0;
	hdr->cap = cap;
	hdr->alloc = alloc;
	return hdr + 1;
}

YML_PRIVATE void *_da_push(void *da, const void *elem, size_t elem_size)
{
	_da_hdr *hdr = (_da_hdr *)da - 1;
	if (hdr->len == hdr->cap)
	{
		size_t new_cap = hdr->cap * 2;
		_da_hdr *new_hdr = YMLREALLOC(hdr, sizeof(_da_hdr) + elem_size * new_cap, hdr->alloc);
		if (!new_hdr)
			return NULL;
		new_hdr->cap = new_cap;
		new_hdr->alloc = hdr->alloc;
		hdr = new_hdr;
	}
	memcpy((char *)(hdr + 1) + elem_size * hdr->len, elem, elem_size);
	hdr->len++;
	return hdr + 1;
}

YML_PRIVATE void _da_free(void *da)
{
	if (!da)
		return;
	_da_hdr *hdr = (_da_hdr *)da - 1;
	YMLDEALLOC(hdr, hdr->alloc);
}

/* ── src/_hm.c ─────────────────────────────────────────────────── */
#include <stdlib.h>
#include <string.h>

/* djb2 hash */
static size_t hash_str(const char *s)
{
	size_t h = 5381;
	while (*s)
		h = h * 33 ^ (unsigned char)*s++;
	return h;
}

/* Округлить cap до ближайшей степени двойки >= cap */
static size_t next_pow2(size_t n)
{
	if (n == 0)
		return 4;
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n |= n >> 32;
	return n + 1;
}

YML_PRIVATE _hm *hm_new(size_t cap, struct YMLAllocator* alloc)
{
	_hm *hm = YMLALLOC(sizeof(_hm), alloc);
	if (!hm)
		return NULL;
	cap = next_pow2(cap < 4 ? 4 : cap);
	hm->entries = YMLCALLOC(cap, sizeof(_hm_entry), alloc);
	if (!hm->entries)
	{
		YMLDEALLOC(hm, alloc);
		return NULL;
	}
	hm->cap = cap;
	hm->len = 0;
	hm->alloc = alloc;
	return hm;
}

/* Перехеширование при load factor > 0.7 */
static bool hm_grow(_hm *hm)
{
	size_t new_cap = hm->cap * 2;
	_hm_entry *new_entries = YMLCALLOC(new_cap, sizeof(_hm_entry), hm->alloc);
	if (!new_entries)
		return false;
	for (size_t i = 0; i < hm->cap; i++)
	{
		if (!hm->entries[i].key)
			continue;
		size_t idx = hash_str(hm->entries[i].key) & (new_cap - 1);
		while (new_entries[idx].key)
			idx = (idx + 1) & (new_cap - 1);
		new_entries[idx] = hm->entries[i];
	}
	YMLDEALLOC(hm->entries, hm->alloc);
	hm->entries = new_entries;
	hm->cap = new_cap;
	return true;
}

YML_PRIVATE bool hm_set(_hm *hm, const char *key, YMLValue value)
{
	if (hm->len * 10 >= hm->cap * 7) /* load > 0.7 */
		if (!hm_grow(hm))
			return false;

	size_t idx = hash_str(key) & (hm->cap - 1);
	while (hm->entries[idx].key)
	{
		if (strcmp(hm->entries[idx].key, key) == 0)
		{
			hm->entries[idx].value = value; /* обновить */
			return true;
		}
		idx = (idx + 1) & (hm->cap - 1);
	}
	hm->entries[idx].key = yml_strdup(key, hm->alloc);
	if (!hm->entries[idx].key)
		return false;
	hm->entries[idx].value = value;
	hm->len++;
	return true;
}

YML_PRIVATE YMLValue *hm_get(const _hm *hm, const char *key)
{
	size_t idx = hash_str(key) & (hm->cap - 1);
	while (hm->entries[idx].key)
	{
		if (strcmp(hm->entries[idx].key, key) == 0)
			return &hm->entries[idx].value;
		idx = (idx + 1) & (hm->cap - 1);
	}
	return NULL;
}

YML_PRIVATE bool hm_next(const _hm *hm, size_t *idx, const char **key, YMLValue **value)
{
	while (*idx < hm->cap)
	{
		_hm_entry *e = &hm->entries[(*idx)++];
		if (e->key)
		{
			*key = e->key;
			*value = &e->value;
			return true;
		}
	}
	return false;
}

YML_PRIVATE void hm_free(_hm *hm)
{
	if (!hm)
		return;
	for (size_t i = 0; i < hm->cap; i++)
	{
		if (!hm->entries[i].key)
			continue;
		YMLDEALLOC(hm->entries[i].key, hm->alloc);
		yml_value_free_impl(&hm->entries[i].value);
	}
	YMLDEALLOC(hm->entries, hm->alloc);
	YMLDEALLOC(hm, hm->alloc);
}

/* ── src/_lexer.c ──────────────────────────────────────────────── */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

/* ── транскодирование UTF-16 / UTF-32 → UTF-8 ──────────────────────── */

/* Кодировать Unicode codepoint в UTF-8; вернуть число записанных байт. */
static int enc_utf8(uint32_t cp, char *out)
{
	if (cp < 0x80)
	{
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800)
	{
		out[0] = (char)(0xC0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000)
	{
		out[0] = (char)(0xE0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (char)(0xF0 | (cp >> 18));
	out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
	out[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

/*
 * Транскодировать UTF-16 (LE или BE) в heap-строку UTF-8.
 * src_bytes — длина в байтах (не считая BOM, который уже пропущен).
 * Возвращает NUL-terminated строку или NULL при OOM.
 */
static char *decode_utf16(const unsigned char *src, size_t src_bytes, bool be)
{
	/* Худший случай: каждый code unit → 3 байта UTF-8. */
	size_t cap = src_bytes / 2 * 3 + 1;
	char *buf = YMLALLOC(cap, NULL);
	if (!buf)
		return NULL;
	size_t w = 0;

	for (size_t i = 0; i + 1 < src_bytes; i += 2)
	{
		uint16_t u = be
			? (uint16_t)((src[i] << 8) | src[i + 1])
			: (uint16_t)(src[i] | (src[i + 1] << 8));

		uint32_t cp;
		if (u >= 0xD800 && u <= 0xDBFF) /* высокий суррогат */
		{
			if (i + 3 >= src_bytes)
				break;
			uint16_t lo = be
				? (uint16_t)((src[i + 2] << 8) | src[i + 3])
				: (uint16_t)(src[i + 2] | (src[i + 3] << 8));
			if (lo < 0xDC00 || lo > 0xDFFF)
				break; /* некорректная суррогатная пара */
			cp = 0x10000u + ((uint32_t)(u - 0xD800u) << 10) + (lo - 0xDC00u);
			i += 2;
		}
		else
			cp = u;

		w += (size_t)enc_utf8(cp, buf + w);
	}
	buf[w] = '\0';
	return buf;
}

/*
 * Транскодировать UTF-32 (LE или BE) в heap-строку UTF-8.
 * src_bytes — длина в байтах (не считая BOM).
 * Возвращает NUL-terminated строку или NULL при OOM.
 */
static char *decode_utf32(const unsigned char *src, size_t src_bytes, bool be)
{
	/* Худший случай: каждый codepoint → 4 байта UTF-8. */
	size_t cap = src_bytes / 4 * 4 + 1;
	char *buf = YMLALLOC(cap, NULL);
	if (!buf)
		return NULL;
	size_t w = 0;

	for (size_t i = 0; i + 3 < src_bytes; i += 4)
	{
		uint32_t cp = be
			? ((uint32_t)src[i] << 24) | ((uint32_t)src[i + 1] << 16)
			| ((uint32_t)src[i + 2] << 8) | (uint32_t)src[i + 3]
			: (uint32_t)src[i] | ((uint32_t)src[i + 1] << 8)
			| ((uint32_t)src[i + 2] << 16) | ((uint32_t)src[i + 3] << 24);
		w += (size_t)enc_utf8(cp, buf + w);
	}
	buf[w] = '\0';
	return buf;
}

/*
 * Определить кодировку входа и при необходимости транскодировать в UTF-8.
 *
 * Возвращает:
 *   NULL        — src уже UTF-8 (out_src указывает на начало данных,
 *                 может быть src+3 если был UTF-8 BOM)
 *   heap-буфер  — UTF-8 копия; caller обязан free() после использования
 *   (char*)-1   — ошибка OOM; *error_out заполнен
 *
 * Обнаружение по YAML 1.2.2 §5.2: BOM или паттерн нулевых байт.
 */
static char *normalize_encoding(const char *src, const char **out_src,
                                const char **error_out)
{
	const unsigned char *b = (const unsigned char *)src;

	/* UTF-32 BE: BOM 00 00 FE FF */
	if (b[0] == 0x00 && b[1] == 0x00 && b[2] == 0xFE && b[3] == 0xFF)
	{
		size_t len = strlen(src + 4); /* приближённо: ищем \0 в хвосте */
		char *r = decode_utf32(b + 4, len, true);
		if (!r) { *error_out = "OOM"; return (char *)-1; }
		*out_src = r;
		return r;
	}
	/* UTF-32 LE: BOM FF FE 00 00 */
	if (b[0] == 0xFF && b[1] == 0xFE && b[2] == 0x00 && b[3] == 0x00)
	{
		/* Длина: ищем четыре нулевых байта подряд как терминатор. */
		size_t len = 0;
		while (b[4 + len] || b[4 + len + 1] || b[4 + len + 2] || b[4 + len + 3])
			len += 4;
		char *r = decode_utf32(b + 4, len, false);
		if (!r) { *error_out = "OOM"; return (char *)-1; }
		*out_src = r;
		return r;
	}
	/* UTF-16 BE: BOM FE FF */
	if (b[0] == 0xFE && b[1] == 0xFF)
	{
		size_t len = 0;
		while (b[2 + len] || b[2 + len + 1])
			len += 2;
		char *r = decode_utf16(b + 2, len, true);
		if (!r) { *error_out = "OOM"; return (char *)-1; }
		*out_src = r;
		return r;
	}
	/* UTF-16 LE: BOM FF FE (и не UTF-32 LE — уже проверен выше) */
	if (b[0] == 0xFF && b[1] == 0xFE)
	{
		size_t len = 0;
		while (b[2 + len] || b[2 + len + 1])
			len += 2;
		char *r = decode_utf16(b + 2, len, false);
		if (!r) { *error_out = "OOM"; return (char *)-1; }
		*out_src = r;
		return r;
	}
	/* UTF-8 BOM: EF BB BF — просто пропустить */
	if (b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF)
	{
		*out_src = src + 3;
		return NULL;
	}
	/* Implicit UTF-32 BE: 00 00 00 xx */
	if (b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x00)
	{
		size_t len = 0;
		while (b[len] || b[len + 1] || b[len + 2] || b[len + 3])
			len += 4;
		char *r = decode_utf32(b, len, true);
		if (!r) { *error_out = "OOM"; return (char *)-1; }
		*out_src = r;
		return r;
	}
	/* Implicit UTF-32 LE: xx 00 00 00 */
	if (b[1] == 0x00 && b[2] == 0x00 && b[3] == 0x00)
	{
		size_t len = 0;
		while (b[len] || b[len + 1] || b[len + 2] || b[len + 3])
			len += 4;
		char *r = decode_utf32(b, len, false);
		if (!r) { *error_out = "OOM"; return (char *)-1; }
		*out_src = r;
		return r;
	}
	/* Implicit UTF-16 BE: 00 xx 00 xx */
	if (b[0] == 0x00 && b[2] == 0x00)
	{
		size_t len = 0;
		while (b[len] || b[len + 1])
			len += 2;
		char *r = decode_utf16(b, len, true);
		if (!r) { *error_out = "OOM"; return (char *)-1; }
		*out_src = r;
		return r;
	}
	/* Implicit UTF-16 LE: xx 00 xx 00 */
	if (b[1] == 0x00 && b[3] == 0x00)
	{
		size_t len = 0;
		while (b[len] || b[len + 1])
			len += 2;
		char *r = decode_utf16(b, len, false);
		if (!r) { *error_out = "OOM"; return (char *)-1; }
		*out_src = r;
		return r;
	}

	/* UTF-8 без BOM — обычный путь */
	*out_src = src;
	return NULL;
}

/* ── состояние лексера ─────────────────────────────────────────────── */

typedef struct
{
	const char *src;
	const char *p;	   /* текущая позиция */
	int line;		   /* 1-based */
	int col;		   /* 0-based */
	int flow_depth;	   /* 0 = блочный контекст */
	const char *error; /* static string при ошибке */
} Lexer;

/* ── утилиты ───────────────────────────────────────────────────────── */

static char lc(Lexer *l) { return *l->p; }

static void ladvance(Lexer *l)
{
	if (*l->p == '\n')
	{
		l->line++;
		l->col = 0;
	}
	else
		l->col++;
	l->p++;
}

static void skip_spaces(Lexer *l)
{
	while (*l->p == ' ' || *l->p == '\t')
		ladvance(l);
}

static void skip_line(Lexer *l)
{
	while (*l->p && *l->p != '\n')
		ladvance(l);
	if (*l->p == '\n')
		ladvance(l);
}

/* Пропустить пустые строки и комментарии. */
static void skip_ws(Lexer *l)
{
	for (;;)
	{
		skip_spaces(l);
		if (*l->p == '#')
		{
			skip_line(l);
			continue;
		}
		if (*l->p == '\n')
		{
			ladvance(l);
			continue;
		}
		if (*l->p == '\r' && *(l->p + 1) == '\n')
		{
			ladvance(l);
			ladvance(l);
			continue;
		}
		break;
	}
}

static Token make_tok(Lexer *l, TK_Type type)
{
	return (Token){.type = type, .line = l->line, .col = l->col, .value = NULL, .value_len = 0, .style = SCALAR_PLAIN};
}

static bool is_flow(Lexer *l) { return l->flow_depth > 0; }

/* ':' является MAP_VAL если за ним пробел/\n/\r/EOF или мы в flow */
static bool is_map_val_colon(Lexer *l)
{
	char next = *(l->p + 1);
	return is_flow(l) || next == ' ' || next == '\t' ||
		   next == '\n' || next == '\r' || next == '\0';
}

/* '-' является SEQ_ENTRY если за ним пробел/\n или начало строки */
static bool is_seq_entry_dash(Lexer *l)
{
	char next = *(l->p + 1);
	return next == ' ' || next == '\t' || next == '\n' ||
		   next == '\r' || next == '\0';
}

/* ── скаляры ───────────────────────────────────────────────────────── */

/* Одиночные кавычки: '' = экранированная ' */
static Token lex_single_quoted(Lexer *l)
{
	Token t = make_tok(l, TK_SCALAR);
	t.style = SCALAR_SINGLE_QUOTED;
	ladvance(l); /* skip ' */
	const char *start = l->p;
	while (*l->p)
	{
		if (*l->p == '\'')
		{
			if (*(l->p + 1) == '\'')
			{
				ladvance(l);
				ladvance(l);
				continue;
			}
			break;
		}
		ladvance(l);
	}
	t.value = start;
	t.value_len = (size_t)(l->p - start);
	if (*l->p == '\'')
		ladvance(l);
	return t;
}

/* Двойные кавычки: стандартные escape-последовательности YAML */
static Token lex_double_quoted(Lexer *l)
{
	Token t = make_tok(l, TK_SCALAR);
	t.style = SCALAR_DOUBLE_QUOTED;
	ladvance(l); /* skip " */
	const char *start = l->p;
	while (*l->p && *l->p != '"')
	{
		if (*l->p == '\\')
			ladvance(l); /* skip escape */
		ladvance(l);
	}
	t.value = start;
	t.value_len = (size_t)(l->p - start);
	if (*l->p == '"')
		ladvance(l);
	return t;
}

/*
 * Блочный скаляр: | (literal) или > (folded).
 * Заголовок: [chomping indicator] [indent indicator] — необязательные.
 * chomping: '-' = strip, '+' = keep, по умолчанию = clip (последний \n).
 * indent: цифра — явный отступ, иначе определяется по первой строке.
 * Возвращает токен с value/value_len указывающим на scoped копию текста
 * без leading indent (raw bytes, парсер займётся chomping при построении узла).
 *
 * NB: value здесь НЕ указывает в src, а в отдельный буфер. Освобождение —
 * ответственность парсера после построения YMLValue.
 */
static Token lex_block_scalar(Lexer *l, ScalarStyle style)
{
	Token t = make_tok(l, TK_SCALAR);
	t.style = style;
	ladvance(l); /* skip | or > */

	/* разбор заголовка */
	char chomping = 'c'; /* c=clip, '-'=strip, '+'=keep */
	int explicit_indent = 0;
	while (*l->p == '-' || *l->p == '+' || (*l->p >= '1' && *l->p <= '9'))
	{
		if (*l->p == '-' || *l->p == '+')
			chomping = *l->p;
		else
			explicit_indent = *l->p - '0';
		ladvance(l);
	}
	skip_spaces(l);
	if (*l->p == '#')
		skip_line(l);
	if (*l->p == '\n')
		ladvance(l);

	/* определить базовый отступ */
	int base_indent = explicit_indent;
	if (base_indent == 0)
	{
		const char *pp = l->p;
		while (*pp == ' ')
			pp++;
		base_indent = (int)(pp - l->p);
	}

	/* собрать строки тела */
	size_t buf_cap = 256;
	char *buf = YMLALLOC(buf_cap, NULL);
	if (!buf)
	{
		l->error = "OOM";
		t.value = "";
		t.value_len = 0;
		return t;
	}
	size_t buf_len = 0;

	while (*l->p)
	{
		/* пустая строка или строка полностью из пробелов */
		const char *line_start = l->p;
		int spaces = 0;
		while (*l->p == ' ')
		{
			ladvance(l);
			spaces++;
		}
		if (*l->p == '\n' || *l->p == '\r' || *l->p == '\0')
		{
			/* пустая строка — добавить \n */
			if (buf_len + 1 >= buf_cap)
			{
				buf_cap *= 2;
				buf = YMLREALLOC(buf, buf_cap, NULL);
				if (!buf)
				{
					l->error = "OOM";
					t.value = "";
					t.value_len = 0;
					return t;
				}
			}
			buf[buf_len++] = '\n';
			if (*l->p == '\r')
				ladvance(l);
			if (*l->p == '\n')
				ladvance(l);
			continue;
		}
		/* строка с меньшим отступом — конец блока */
		if (spaces < base_indent)
		{
			l->p = line_start;
			l->col -= spaces;
			break;
		}
		/* пропустить base_indent пробелов */
		int extra = spaces - base_indent;
		/* добавить extra leading spaces (для literal) */
		while (buf_len + (size_t)extra + 1 >= buf_cap)
		{
			buf_cap *= 2;
			buf = YMLREALLOC(buf, buf_cap, NULL);
			if (!buf)
			{
				l->error = "OOM";
				t.value = "";
				t.value_len = 0;
				return t;
			}
		}
		for (int i = 0; i < extra; i++)
			buf[buf_len++] = ' ';

		/* содержимое строки */
		while (*l->p && *l->p != '\n' && *l->p != '\r')
		{
			if (buf_len + 1 >= buf_cap)
			{
				buf_cap *= 2;
				buf = YMLREALLOC(buf, buf_cap, NULL);
				if (!buf)
				{
					l->error = "OOM";
					t.value = "";
					t.value_len = 0;
					return t;
				}
			}
			buf[buf_len++] = *l->p;
			ladvance(l);
		}
		if (buf_len + 1 >= buf_cap)
		{
			buf_cap *= 2;
			buf = YMLREALLOC(buf, buf_cap, NULL);
			if (!buf)
			{
				l->error = "OOM";
				t.value = "";
				t.value_len = 0;
				return t;
			}
		}
		buf[buf_len++] = '\n';
		if (*l->p == '\r')
			ladvance(l);
		if (*l->p == '\n')
			ladvance(l);
	}

	/* chomping: удалить trailing newlines */
	if (chomping == '-')
	{
		while (buf_len > 0 && buf[buf_len - 1] == '\n')
			buf_len--;
	}
	else if (chomping == 'c')
	{
		/* оставить ровно один \n в конце */
		while (buf_len > 1 && buf[buf_len - 1] == '\n' && buf[buf_len - 2] == '\n')
			buf_len--;
	}
	/* '+' — оставить как есть */

	buf[buf_len] = '\0';
	t.value = buf;
	t.value_len = buf_len;
	t.owns_value = true;
	return t;
}

/*
 * Plain scalar: читаем до конца "безопасного" контента.
 * В блочном контексте: стоп на ': ', ' #', ',', '[', ']', '{', '}', '\n'
 * В flow-контексте: стоп также на ',' / ':' / ']' / '}'
 */
static Token lex_plain(Lexer *l)
{
	Token t = make_tok(l, TK_SCALAR);
	t.style = SCALAR_PLAIN;
	const char *start = l->p;
	while (*l->p)
	{
		char c = *l->p;
		char n = *(l->p + 1);
		if (c == '\n' || c == '\r')
			break;
		if (c == '#' && (l->p > start) && *(l->p - 1) == ' ')
			break;
		if (is_flow(l) && (c == ',' || c == ']' || c == '}'))
			break;
		if (c == ':' && (n == ' ' || n == '\t' || n == '\n' || n == '\r' || n == '\0'))
			break;
		if (c == ':' && is_flow(l))
			break;
		ladvance(l);
	}
	/* trim trailing spaces */
	const char *end = l->p;
	while (end > start && (*(end - 1) == ' ' || *(end - 1) == '\t'))
		end--;
	t.value = start;
	t.value_len = (size_t)(end - start);
	return t;
}

/* &name или *name */
static Token lex_anchor_alias(Lexer *l, TK_Type type)
{
	Token t = make_tok(l, type);
	ladvance(l); /* skip & or * */
	const char *start = l->p;
	while (*l->p && *l->p != ' ' && *l->p != '\t' &&
		   *l->p != '\n' && *l->p != '\r' &&
		   *l->p != ',' && *l->p != '[' && *l->p != ']' &&
		   *l->p != '{' && *l->p != '}')
		ladvance(l);
	t.value = start;
	t.value_len = (size_t)(l->p - start);
	return t;
}

/* !tag / !!tag / !<uri> */
static Token lex_tag(Lexer *l)
{
	Token t = make_tok(l, TK_TAG);
	const char *start = l->p;
	ladvance(l); /* skip first ! */
	if (*l->p == '!')
		ladvance(l); /* !!tag */
	else if (*l->p == '<')
	{ /* !<uri> */
		while (*l->p && *l->p != '>')
			ladvance(l);
		if (*l->p == '>')
			ladvance(l);
	}
	while (*l->p && *l->p != ' ' && *l->p != '\t' &&
		   *l->p != '\n' && *l->p != '\r')
		ladvance(l);
	t.value = start;
	t.value_len = (size_t)(l->p - start);
	return t;
}

/* ── главный цикл лексера ──────────────────────────────────────────── */

YML_PRIVATE Token *lex(const char *src, const char **error_out)
{
	/* Транскодировать UTF-16/32 → UTF-8 если необходимо. */
	const char *use_src = src;
	char *converted = normalize_encoding(src, &use_src, error_out);
	if (converted == (char *)-1)
		return NULL; /* OOM при транскодировании */

	Lexer l = {.src = use_src, .p = use_src, .line = 1, .col = 0, .flow_depth = 0, .error = NULL};
	Token *tokens = da_new(Token, 64, NULL);
	if (!tokens)
	{
		YMLDEALLOC(converted, NULL);
		if (error_out)
			*error_out = "OOM";
		return NULL;
	}

	for (;;)
	{
		skip_ws(&l);
		int tok_line = l.line, tok_col = l.col;

		if (*l.p == '\0')
		{
			da_push(tokens, make_tok(&l, TK_EOF));
			break;
		}

		/* --- / ... в начале строки (col==0 или только пробелы до) */
		if (l.col == 0 && l.p[0] == '-' && l.p[1] == '-' && l.p[2] == '-' &&
			(l.p[3] == ' ' || l.p[3] == '\n' || l.p[3] == '\r' || l.p[3] == '\0'))
		{
			ladvance(&l);
			ladvance(&l);
			ladvance(&l);
			Token t = {.type = TK_DOC_START, .line = tok_line, .col = tok_col};
			da_push(tokens, t);
			continue;
		}
		if (l.col == 0 && l.p[0] == '.' && l.p[1] == '.' && l.p[2] == '.' &&
			(l.p[3] == ' ' || l.p[3] == '\n' || l.p[3] == '\r' || l.p[3] == '\0'))
		{
			ladvance(&l);
			ladvance(&l);
			ladvance(&l);
			Token t = {.type = TK_DOC_END, .line = tok_line, .col = tok_col};
			da_push(tokens, t);
			continue;
		}

		char c = lc(&l);

		/* flow indicators */
		if (c == '[')
		{
			ladvance(&l);
			l.flow_depth++;
			Token t = {.type = TK_FLOW_SEQ_START, .line = tok_line, .col = tok_col};
			da_push(tokens, t);
			continue;
		}
		if (c == ']')
		{
			ladvance(&l);
			if (l.flow_depth > 0)
				l.flow_depth--;
			Token t = {.type = TK_FLOW_SEQ_END, .line = tok_line, .col = tok_col};
			da_push(tokens, t);
			continue;
		}
		if (c == '{')
		{
			ladvance(&l);
			l.flow_depth++;
			Token t = {.type = TK_FLOW_MAP_START, .line = tok_line, .col = tok_col};
			da_push(tokens, t);
			continue;
		}
		if (c == '}')
		{
			ladvance(&l);
			if (l.flow_depth > 0)
				l.flow_depth--;
			Token t = {.type = TK_FLOW_MAP_END, .line = tok_line, .col = tok_col};
			da_push(tokens, t);
			continue;
		}
		if (c == ',' && is_flow(&l))
		{
			ladvance(&l);
			Token t = {.type = TK_FLOW_ENTRY, .line = tok_line, .col = tok_col};
			da_push(tokens, t);
			continue;
		}

		/* block seq entry */
		if (c == '-' && !is_flow(&l) && is_seq_entry_dash(&l))
		{
			ladvance(&l);
			Token t = {.type = TK_SEQ_ENTRY, .line = tok_line, .col = tok_col};
			da_push(tokens, t);
			continue;
		}

		/* explicit map key */
		if (c == '?' && (*(l.p + 1) == ' ' || *(l.p + 1) == '\n'))
		{
			ladvance(&l);
			Token t = {.type = TK_MAP_KEY, .line = tok_line, .col = tok_col};
			da_push(tokens, t);
			continue;
		}

		/* map value */
		if (c == ':' && is_map_val_colon(&l))
		{
			ladvance(&l);
			Token t = {.type = TK_MAP_VAL, .line = tok_line, .col = tok_col};
			da_push(tokens, t);
			continue;
		}

		/* anchor / alias */
		if (c == '&')
		{
			Token t = lex_anchor_alias(&l, TK_ANCHOR);
			t.line = tok_line;
			t.col = tok_col;
			da_push(tokens, t);
			continue;
		}
		if (c == '*')
		{
			Token t = lex_anchor_alias(&l, TK_ALIAS);
			t.line = tok_line;
			t.col = tok_col;
			da_push(tokens, t);
			continue;
		}

		/* tag */
		if (c == '!')
		{
			Token t = lex_tag(&l);
			t.line = tok_line;
			t.col = tok_col;
			da_push(tokens, t);
			continue;
		}

		/* block scalars */
		if (c == '|')
		{
			Token t = lex_block_scalar(&l, SCALAR_LITERAL);
			t.line = tok_line;
			t.col = tok_col;
			da_push(tokens, t);
			continue;
		}
		if (c == '>')
		{
			Token t = lex_block_scalar(&l, SCALAR_FOLDED);
			t.line = tok_line;
			t.col = tok_col;
			da_push(tokens, t);
			continue;
		}

		/* quoted scalars */
		if (c == '\'')
		{
			Token t = lex_single_quoted(&l);
			t.line = tok_line;
			t.col = tok_col;
			da_push(tokens, t);
			continue;
		}
		if (c == '"')
		{
			Token t = lex_double_quoted(&l);
			t.line = tok_line;
			t.col = tok_col;
			da_push(tokens, t);
			continue;
		}

		/* plain scalar */
		{
			Token t = lex_plain(&l);
			t.line = tok_line;
			t.col = tok_col;
			da_push(tokens, t);
		}
	}

	YMLDEALLOC(converted, NULL); /* NULL-safe; освобождаем UTF-8 буфер если был выделен */

	if (l.error)
	{
		if (error_out)
			*error_out = l.error;
		da_free(tokens);
		return NULL;
	}
	if (error_out)
		*error_out = NULL;
	return tokens;
}

/* ── src/_yml_utils.c ──────────────────────────────────────────── */
YML_PRIVATE YMLValue *yml_deep_copy(const YMLValue *src, struct YMLAllocator *target_alloc)
{
	if (!src)
		return NULL;
	struct YMLAllocator* alloc = target_alloc ? target_alloc : src->allocator;
	YMLValue *v = YMLALLOC(sizeof(YMLValue), alloc);
	if (!v)
		return NULL;
	v->type = src->type;
	v->allocator = alloc;
	switch (src->type)
	{
	case YML_NULL:
	case YML_BOOL:
	case YML_INT:
	case YML_FLOAT:
		v->value = src->value;
		break;
	case YML_STRING:
		v->value.string = src->value.string ? yml_strdup(src->value.string, alloc) : NULL;
		break;
	case YML_ARRAY:
	{
		size_t n = da_len(src->value.array);
		YMLValue *arr = da_new(YMLValue, n > 0 ? n : 1, alloc);
		for (size_t i = 0; i < n; i++)
		{
			YMLValue *cp = yml_deep_copy(&src->value.array[i], alloc);
			if (cp)
			{
				da_push(arr, *cp);
				YMLDEALLOC(cp, alloc);
			}
		}
		v->value.array = arr;
		break;
	}
	case YML_OBJECT:
	{
		_hm *src_hm = (_hm *)src->value.object;
		_hm *dst_hm = hm_new(src_hm->cap, alloc);
		size_t idx = 0;
		const char *key;
		YMLValue *val;
		while (hm_next(src_hm, &idx, &key, &val))
		{
			YMLValue *cp = yml_deep_copy(val, alloc);
			if (cp)
			{
				hm_set(dst_hm, key, *cp);
				YMLDEALLOC(cp, alloc);
			}
		}
		v->value.object = dst_hm;
		break;
	}
	default:
		break;
	}
	return v;
}

/* ── src/YMLParser.c ───────────────────────────────────────────── */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>
#include <inttypes.h>

/* ── глобальное состояние последней ошибки ─────────────────────────── */

static _Thread_local int g_ok;
static _Thread_local char g_error[256];

static void set_error(int code, const char *msg)
{
	g_ok = code;
	snprintf(g_error, sizeof(g_error), "%s", msg);
}

int YMLPrintError(void)
{
	if (g_ok == 0)
		return 0;
	fprintf(stderr, "YMLParser error %d: %s\n", g_ok, g_error);
	return g_ok;
}

/* ── состояние парсера ─────────────────────────────────────────────── */

/*
 * Таблица якорей: имя → указатель на уже построенный YMLValue.
 * Алиас (*name) — просто возвращаем тот же указатель; дерево —
 * не граф, поэтому при YMLDestroy нельзя освобождать узел дважды.
 * Якоря хранятся в da<_anchor_entry>, живут на время одного YMLParse.
 */
typedef struct
{
	char *name;
	YMLValue *node;
} _anchor_entry;

typedef struct
{
	Token *tokens;
	size_t pos;

	_anchor_entry *anchors;

	struct YMLAllocator* alloc;

	int ok;
	char error[256];
} Parser;

/* ── управление таблицей якорей ────────────────────────────────────── */

/* Освободить все записи в таблице якорей, сохранив выделенную ёмкость da. */
static void anchors_clear(Parser *p)
{
	for (size_t i = 0; i < da_len(p->anchors); i++)
	{
		YMLDEALLOC(p->anchors[i].name, p->alloc);
		_YMLDestroy(p->anchors[i].node, (struct _YMLOptionals){0});
	}
	((_da_hdr *)p->anchors - 1)->len = 0;
}

/* ── утилиты парсера ───────────────────────────────────────────────── */

static Token *cur(Parser *p) { return &p->tokens[p->pos]; }
static Token *peek(Parser *p, size_t offset) { return &p->tokens[p->pos + offset]; }
static void advance(Parser *p)
{
	if (cur(p)->type != TK_EOF)
		p->pos++;
}

static void parse_error(Parser *p, const char *msg)
{
	Token *t = cur(p);
	snprintf(p->error, sizeof(p->error),
			 "line %d col %d: %s", t->line, t->col, msg);
	p->ok = 1;
}

/* Скопировать value/value_len токена в heap (NUL-terminated). */
static char *token_strdup(const Token *t, struct YMLAllocator* alloc)
{
	char *s = YMLALLOC(t->value_len + 1, alloc);
	if (!s)
		return NULL;
	memcpy(s, t->value, t->value_len);
	s[t->value_len] = '\0';
	return s;
}

/* ── инференс типа для plain-скаляра (YAML Core Schema) ───────────── */

/*
 * Порядок проверки:
 *   1. null:  ~  null  Null  NULL  (пустая строка тоже null)
 *   2. bool:  true True TRUE  /  false False FALSE
 *   3. int:   [-+]?[0-9]+  |  0x[0-9a-fA-F]+  |  0o[0-7]+
 *   4. float: [-+]?(\.[0-9]+|[0-9]+(\.[0-9]*)?)([eE][-+]?[0-9]+)?
 *             .inf  -.inf  +.inf  .nan
 *   5. string: всё остальное
 */
static YMLValue infer_scalar(const char *s, size_t len, struct YMLAllocator* alloc);

/* ── рекурсивные функции разбора ───────────────────────────────────── */

/*
 * Разобрать один узел начиная с текущего токена.
 * min_indent — минимальный отступ, при котором блочная коллекция
 * считается вложенной (0 для корня).
 */
static YMLValue *parse_node(Parser *p, int min_indent);

/* Разобрать блочный маппинг. Все ключи-значения с отступом == indent. */
static YMLValue *parse_block_mapping(Parser *p, int indent);

/* Разобрать блочную последовательность. Все элементы '-' с отступом == indent. */
static YMLValue *parse_block_sequence(Parser *p, int indent);

/* Разобрать flow-маппинг { k: v, ... } */
static YMLValue *parse_flow_mapping(Parser *p);

/* Разобрать flow-последовательность [ v, ... ] */
static YMLValue *parse_flow_sequence(Parser *p);

/* Разобрать скалярный токен и применить infer_scalar (или тег). */
static YMLValue *parse_scalar(Parser *p);

/* Глубокая копия YMLValue (для разворачивания алиасов). */
/* ── инференс типа plain-скаляра (YAML Core Schema) ───────────────── */

static bool is_decimal(const char *s, size_t len)
{
	if (!len)
		return false;
	size_t i = 0;
	if (s[i] == '+' || s[i] == '-')
		i++;
	if (i == len)
		return false;
	for (; i < len; i++)
		if (!isdigit((unsigned char)s[i]))
			return false;
	return true;
}

static bool is_hex(const char *s, size_t len)
{
	if (len < 3 || s[0] != '0' || s[1] != 'x')
		return false;
	for (size_t i = 2; i < len; i++)
		if (!isxdigit((unsigned char)s[i]))
			return false;
	return len > 2;
}

static bool is_octal(const char *s, size_t len)
{
	if (len < 3 || s[0] != '0' || s[1] != 'o')
		return false;
	for (size_t i = 2; i < len; i++)
		if (s[i] < '0' || s[i] > '7')
			return false;
	return len > 2;
}

static YMLValue infer_scalar(const char *s, size_t len, struct YMLAllocator* alloc)
{
	YMLValue v = {0};

	if (len == 0 || (len == 1 && s[0] == '~') ||
		(len == 4 && (memcmp(s, "null", 4) == 0 || memcmp(s, "Null", 4) == 0 || memcmp(s, "NULL", 4) == 0)))
	{
		v.type = YML_NULL;
		return v;
	}

	if ((len == 4 && (memcmp(s, "true", 4) == 0 || memcmp(s, "True", 4) == 0 || memcmp(s, "TRUE", 4) == 0)))
	{
		v.type = YML_BOOL;
		v.value.boolean = true;
		return v;
	}
	if ((len == 5 && (memcmp(s, "false", 5) == 0 || memcmp(s, "False", 5) == 0 || memcmp(s, "FALSE", 5) == 0)))
	{
		v.type = YML_BOOL;
		v.value.boolean = false;
		return v;
	}

	if ((len == 4 && memcmp(s, ".inf", 4) == 0) || (len == 5 && memcmp(s, "+.inf", 5) == 0))
	{
		v.type = YML_FLOAT;
		v.value.number = HUGE_VAL;
		return v;
	}
	if (len == 5 && memcmp(s, "-.inf", 5) == 0)
	{
		v.type = YML_FLOAT;
		v.value.number = -HUGE_VAL;
		return v;
	}
	if ((len == 4 && memcmp(s, ".nan", 4) == 0) || (len == 4 && memcmp(s, ".NaN", 4) == 0) ||
		(len == 4 && memcmp(s, ".NAN", 4) == 0))
	{
		v.type = YML_FLOAT;
		v.value.number = NAN;
		return v;
	}

	if (is_decimal(s, len))
	{
		char tmp[64];
		size_t n = len < 63 ? len : 63;
		memcpy(tmp, s, n);
		tmp[n] = '\0';
		v.type = YML_INT;
		v.value.integer = (int64_t)strtoll(tmp, NULL, 10);
		return v;
	}
	if (is_hex(s, len))
	{
		char tmp[64];
		size_t n = len < 63 ? len : 63;
		memcpy(tmp, s, n);
		tmp[n] = '\0';
		v.type = YML_INT;
		v.value.integer = (int64_t)strtoll(tmp + 2, NULL, 16);
		return v;
	}
	if (is_octal(s, len))
	{
		char tmp[64];
		size_t n = len < 63 ? len : 63;
		memcpy(tmp, s, n);
		tmp[n] = '\0';
		v.type = YML_INT;
		v.value.integer = (int64_t)strtoll(tmp + 2, NULL, 8);
		return v;
	}

	{
		bool has_dot_or_e = false;
		const char *p2 = s;
		size_t rem = len;
		if (rem && (*p2 == '+' || *p2 == '-'))
		{
			p2++;
			rem--;
		}
		for (size_t i = 0; i < rem; i++)
		{
			if (p2[i] == '.' || p2[i] == 'e' || p2[i] == 'E')
			{
				has_dot_or_e = true;
				break;
			}
		}
		if (has_dot_or_e)
		{
			char tmp[64];
			size_t n = len < 63 ? len : 63;
			memcpy(tmp, s, n);
			tmp[n] = '\0';
			char *end;
			double d = strtod(tmp, &end);
			if (end == tmp + n)
			{
				v.type = YML_FLOAT;
				v.value.number = d;
				return v;
			}
		}
	}

	char *copy = YMLALLOC(len + 1, alloc);
	if (copy)
	{
		memcpy(copy, s, len);
		copy[len] = '\0';
	}
	v.type = YML_STRING;
	v.value.string = copy;
	return v;
}

/* ── обработка строк ───────────────────────────────────────────────── */

/* Декодировать \uXXXX (4 hex цифры) в UTF-8 байты, вернуть число записанных байт. */
static int encode_utf8(uint32_t cp, char *out)
{
	if (cp < 0x80)
	{
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800)
	{
		out[0] = (char)(0xC0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000)
	{
		out[0] = (char)(0xE0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (char)(0xF0 | (cp >> 18));
	out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
	out[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

static uint32_t parse_hex_n(const char *s, int n)
{
	uint32_t v = 0;
	for (int i = 0; i < n; i++)
	{
		v <<= 4;
		char c = s[i];
		if (c >= '0' && c <= '9')
			v |= (uint32_t)(c - '0');
		else if (c >= 'a' && c <= 'f')
			v |= (uint32_t)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F')
			v |= (uint32_t)(c - 'A' + 10);
	}
	return v;
}

/*
 * Обработать double-quoted строку: escape-последовательности → байты.
 * src/len — содержимое между кавычками (без самих кавычек).
 * Возвращает heap-строку (caller освобождает).
 */
static char *unescape_double_quoted(const char *src, size_t len, struct YMLAllocator* alloc)
{
	char *buf = YMLALLOC(len + 1, alloc);
	if (!buf)
		return NULL;
	size_t w = 0;
	for (size_t i = 0; i < len;)
	{
		if (src[i] != '\\')
		{
			buf[w++] = src[i++];
			continue;
		}
		i++; /* skip \ */
		if (i >= len)
			break;
		char esc = src[i++];
		switch (esc)
		{
		case '0':
			buf[w++] = '\0';
			break;
		case 'a':
			buf[w++] = '\a';
			break;
		case 'b':
			buf[w++] = '\b';
			break;
		case 't':
		case '\t':
			buf[w++] = '\t';
			break;
		case 'n':
			buf[w++] = '\n';
			break;
		case 'v':
			buf[w++] = '\v';
			break;
		case 'f':
			buf[w++] = '\f';
			break;
		case 'r':
			buf[w++] = '\r';
			break;
		case 'e':
			buf[w++] = 0x1B;
			break;
		case '"':
			buf[w++] = '"';
			break;
		case '/':
			buf[w++] = '/';
			break;
		case '\\':
			buf[w++] = '\\';
			break;
		case 'x': /* \xXX */
			if (i + 2 <= len)
			{
				buf[w++] = (char)parse_hex_n(src + i, 2);
				i += 2;
			}
			break;
		case 'u': /* \uXXXX */
			if (i + 4 <= len)
			{
				w += (size_t)encode_utf8(parse_hex_n(src + i, 4), buf + w);
				i += 4;
			}
			break;
		case 'U': /* \UXXXXXXXX */
			if (i + 8 <= len)
			{
				w += (size_t)encode_utf8(parse_hex_n(src + i, 8), buf + w);
				i += 8;
			}
			break;
		case '\n': /* line continuation: skip leading whitespace on next line */
			while (i < len && (src[i] == ' ' || src[i] == '\t' || src[i] == '\n' || src[i] == '\r'))
				i++;
			break;
		default:
			buf[w++] = esc;
			break;
		}
	}
	buf[w] = '\0';
	return buf;
}

/*
 * Обработать single-quoted строку: '' → '.
 */
static char *unescape_single_quoted(const char *src, size_t len, struct YMLAllocator* alloc)
{
	char *buf = YMLALLOC(len + 1, alloc);
	if (!buf)
		return NULL;
	size_t w = 0;
	for (size_t i = 0; i < len;)
	{
		if (src[i] == '\'' && i + 1 < len && src[i + 1] == '\'')
		{
			buf[w++] = '\'';
			i += 2;
		}
		else
			buf[w++] = src[i++];
	}
	buf[w] = '\0';
	return buf;
}

/*
 * Свернуть содержимое folded-блочного скаляра.
 * Правило: одиночный \n → ' ', но \n\n → \n.
 */
static char *fold_scalar(const char *src, size_t len, struct YMLAllocator* alloc)
{
	char *buf = YMLALLOC(len + 1, alloc);
	if (!buf)
		return NULL;
	size_t w = 0;
	for (size_t i = 0; i < len;)
	{
		if (src[i] != '\n')
		{
			buf[w++] = src[i++];
			continue;
		}
		if (i + 1 >= len)
		{
			/* финальный \n: сохранить (clip chomping) */
			buf[w++] = '\n';
			i++;
		}
		else if (src[i + 1] == '\n')
		{
			/* пустые строки: текущий \n сворачивается,
			   каждая пустая строка → один \n в выводе */
			i++;
			while (i < len && src[i] == '\n')
			{
				buf[w++] = '\n';
				i++;
			}
		}
		else
		{
			/* одиночный \n между строками → пробел */
			buf[w++] = ' ';
			i++;
		}
	}
	buf[w] = '\0';
	return buf;
}

/* ── якоря / алиасы ────────────────────────────────────────────────── */

static void anchor_add(Parser *p, const char *name, size_t name_len, YMLValue *node)
{
	char *n = YMLALLOC(name_len + 1, p->alloc);
	if (!n)
		return;
	memcpy(n, name, name_len);
	n[name_len] = '\0';
	_anchor_entry e = {.name = n, .node = yml_deep_copy(node, NULL)};
	da_push(p->anchors, e);
}

static YMLValue *anchor_find(Parser *p, const char *name, size_t name_len)
{
	size_t count = da_len(p->anchors);
	for (size_t i = 0; i < count; i++)
	{
		_anchor_entry *e = &p->anchors[i];
		if (strlen(e->name) == name_len && memcmp(e->name, name, name_len) == 0)
			return e->node;
	}
	return NULL;
}

/* ── глубокая копия YMLValue ───────────────────────────────────────── */

/* ── применить тег к значению ──────────────────────────────────────── */

/*
 * Встроенные теги YAML (Core Schema):
 *   !!null !!bool !!int !!float !!str !!seq !!map
 * Игнорируем кастомные (!mytype и т.п.).
 * tag_val / tag_len — содержимое тега включая ! (например "!!str", len=5).
 */
/*
 * Применить тег к уже построенному значению v.
 * При необходимости конвертирует значение + тип.
 * Работает только с plain-скалярами — вызывается из parse_node.
 */
static void apply_tag(YMLValue *v, const char *tag, size_t tag_len)
{
	if (tag_len < 2)
		return;
	const char *t = tag;
	size_t tl = tag_len;
	if (tl >= 2 && t[0] == '!' && t[1] == '!')
	{
		t += 2;
		tl -= 2;
	}
	else if (tl >= 1 && t[0] == '!')
	{
		t++;
		tl--;
	}

	if (tl == 4 && memcmp(t, "null", 4) == 0)
	{
		v->type = YML_NULL;
		return;
	}
	if (tl == 5 && memcmp(t, "float", 5) == 0)
	{
		v->type = YML_FLOAT;
		return;
	}

	if (tl == 3 && memcmp(t, "str", 3) == 0)
	{
		if (v->type == YML_STRING)
			return;
		char buf[64] = "";
		switch (v->type)
		{
		case YML_INT:
			snprintf(buf, sizeof(buf), "%" PRId64, v->value.integer);
			break;
		case YML_FLOAT:
			snprintf(buf, sizeof(buf), "%g", v->value.number);
			break;
		case YML_BOOL:
			snprintf(buf, sizeof(buf), "%s", v->value.boolean ? "true" : "false");
			break;
		case YML_NULL:
			snprintf(buf, sizeof(buf), "null");
			break;
		default:
			break;
		}
		v->type = YML_STRING;
		v->value.string = yml_strdup(buf, v->allocator);
		return;
	}

	if (tl == 3 && memcmp(t, "int", 3) == 0)
	{
		if (v->type == YML_INT)
			return;
		if (v->type == YML_STRING)
		{
			char *end;
			int64_t n = strtoll(v->value.string, &end, 0);
			YMLDEALLOC((char *)v->value.string, v->allocator);
			v->type = YML_INT;
			v->value.integer = n;
		}
		else if (v->type == YML_FLOAT)
		{
			v->type = YML_INT;
			v->value.integer = (int64_t)v->value.number;
		}
		return;
	}

	if (tl == 4 && memcmp(t, "bool", 4) == 0)
	{
		if (v->type == YML_BOOL)
			return;
		bool b = false;
		if (v->type == YML_STRING)
			b = (strcmp(v->value.string, "true") == 0 || strcmp(v->value.string, "1") == 0);
		else if (v->type == YML_INT)
			b = v->value.integer != 0;
		if (v->type == YML_STRING)
			YMLDEALLOC((char *)v->value.string, v->allocator);
		v->type = YML_BOOL;
		v->value.boolean = b;
		return;
	}
}

/* ── parse_scalar ──────────────────────────────────────────────────── */

static YMLValue *parse_scalar(Parser *p)
{
	Token *t = cur(p);
	if (t->type != TK_SCALAR)
	{
		parse_error(p, "expected scalar");
		return NULL;
	}
	advance(p);

	YMLValue *v = YMLALLOC(sizeof(YMLValue), p->alloc);
	if (!v)
	{
		parse_error(p, "OOM");
		return NULL;
	}
	v->allocator = p->alloc;

	switch (t->style)
	{
	case SCALAR_PLAIN:
		*v = infer_scalar(t->value, t->value_len, p->alloc);
		v->allocator = p->alloc;
		break;
	case SCALAR_SINGLE_QUOTED:
		v->type = YML_STRING;
		v->value.string = unescape_single_quoted(t->value, t->value_len, p->alloc);
		break;
	case SCALAR_DOUBLE_QUOTED:
		v->type = YML_STRING;
		v->value.string = unescape_double_quoted(t->value, t->value_len, p->alloc);
		break;
	case SCALAR_LITERAL:
		v->type = YML_STRING;
		v->value.string = YMLALLOC(t->value_len + 1, p->alloc);
		if (v->value.string)
		{
			memcpy((char *)v->value.string, t->value, t->value_len);
			((char *)v->value.string)[t->value_len] = '\0';
		}
		if (t->owns_value)
		{
			YMLDEALLOC((char *)t->value, p->alloc);
			t->value = NULL;
		}
		break;
	case SCALAR_FOLDED:
		v->type = YML_STRING;
		v->value.string = fold_scalar(t->value, t->value_len, p->alloc);
		if (t->owns_value)
		{
			YMLDEALLOC((char *)t->value, p->alloc);
			t->value = NULL;
		}
		break;
	}
	return v;
}

/* ── parse_flow_sequence ────────────────────────────────────────────── */

static YMLValue *parse_flow_sequence(Parser *p)
{
	advance(p);
	YMLValue *v = YMLALLOC(sizeof(YMLValue), p->alloc);
	if (!v)
	{
		parse_error(p, "OOM");
		return NULL;
	}
	v->type = YML_ARRAY;
	v->value.array = da_new(YMLValue, 4, p->alloc);
	v->allocator = p->alloc;

	while (cur(p)->type != TK_FLOW_SEQ_END && cur(p)->type != TK_EOF)
	{
		if (cur(p)->type == TK_FLOW_ENTRY)
		{
			advance(p);
			continue;
		}
		YMLValue *elem = parse_node(p, 0);
		if (p->ok || !elem)
			break;
		da_push(v->value.array, *elem);
		YMLDEALLOC(elem, p->alloc);
	}
	if (cur(p)->type != TK_FLOW_SEQ_END)
		parse_error(p, "unclosed '['");
	else
		advance(p);
	return v;
}

/* ── parse_flow_mapping ─────────────────────────────────────────────── */

static YMLValue *parse_flow_mapping(Parser *p)
{
	advance(p);
	_hm *hm = hm_new(8, p->alloc);
	YMLValue *v = YMLALLOC(sizeof(YMLValue), p->alloc);
	if (!v || !hm)
	{
		parse_error(p, "OOM");
		return NULL;
	}
	v->type = YML_OBJECT;
	v->value.object = hm;
	v->allocator = p->alloc;

	while (cur(p)->type != TK_FLOW_MAP_END && cur(p)->type != TK_EOF)
	{
		if (cur(p)->type == TK_FLOW_ENTRY)
		{
			advance(p);
			continue;
		}
		if (cur(p)->type == TK_MAP_KEY)
		{
			advance(p);
		}

		if (cur(p)->type != TK_SCALAR)
		{
			parse_error(p, "expected scalar key in flow mapping");
			break;
		}
		char *key = token_strdup(cur(p), p->alloc);
		advance(p);

		if (cur(p)->type != TK_MAP_VAL)
		{
			YMLDEALLOC(key, p->alloc);
			parse_error(p, "expected ':' in flow mapping");
			break;
		}
		advance(p);

		YMLValue *val = parse_node(p, 0);
		if (p->ok || !val)
		{
			YMLDEALLOC(key, p->alloc);
			break;
		}
		hm_set(hm, key, *val);
		YMLDEALLOC(key, p->alloc);
		YMLDEALLOC(val, p->alloc);
	}
	if (cur(p)->type != TK_FLOW_MAP_END)
		parse_error(p, "unclosed '{'");
	else
		advance(p);
	return v;
}

/* ── parse_block_sequence ───────────────────────────────────────────── */

static YMLValue *parse_block_sequence(Parser *p, int indent)
{
	YMLValue *v = YMLALLOC(sizeof(YMLValue), p->alloc);
	if (!v)
	{
		parse_error(p, "OOM");
		return NULL;
	}
	v->type = YML_ARRAY;
	v->value.array = da_new(YMLValue, 4, p->alloc);
	v->allocator = p->alloc;

	while (cur(p)->type == TK_SEQ_ENTRY && cur(p)->col == indent)
	{
		advance(p);
		if (cur(p)->type == TK_EOF || cur(p)->type == TK_DOC_END ||
			(cur(p)->type == TK_SEQ_ENTRY && cur(p)->col == indent))
		{
			YMLValue null_v = {.type = YML_NULL, .allocator = p->alloc};
			da_push(v->value.array, null_v);
			continue;
		}
		YMLValue *elem = parse_node(p, indent + 1);
		if (p->ok || !elem)
			break;
		da_push(v->value.array, *elem);
		YMLDEALLOC(elem, p->alloc);
	}
	return v;
}

/* ── parse_block_mapping ─────────────────────────────────────────────── */

/*
 * Применить merge key "<<" — скопировать пары из src в dst,
 * не перезаписывая уже существующие ключи.
 */
static void apply_merge(_hm *dst, const YMLValue *src)
{
	if (!src)
		return;
	if (src->type == YML_OBJECT)
	{
		_hm *src_hm = (_hm *)src->value.object;
		size_t idx = 0;
		const char *key;
		YMLValue *val;
		while (hm_next(src_hm, &idx, &key, &val))
		{
			if (!hm_get(dst, key))
			{
				YMLValue *cp = yml_deep_copy(val, NULL);
				if (cp)
				{
					hm_set(dst, key, *cp);
					YMLDEALLOC(cp, src->allocator);
				}
			}
		}
	}
	else if (src->type == YML_ARRAY)
	{
		size_t n = da_len(src->value.array);
		for (size_t i = 0; i < n; i++)
			apply_merge(dst, &src->value.array[i]);
	}
}

static YMLValue *parse_block_mapping(Parser *p, int indent)
{
	_hm *hm = hm_new(8, p->alloc);
	YMLValue *v = YMLALLOC(sizeof(YMLValue), p->alloc);
	if (!v || !hm)
	{
		parse_error(p, "OOM");
		return NULL;
	}
	v->type = YML_OBJECT;
	v->value.object = hm;
	v->allocator = p->alloc;

	for (;;)
	{
		bool explicit_key = (cur(p)->type == TK_MAP_KEY && cur(p)->col == indent);
		bool implicit_key = (cur(p)->type == TK_SCALAR && cur(p)->col == indent &&
							 peek(p, 1)->type == TK_MAP_VAL &&
							 peek(p, 1)->line == cur(p)->line);
		if (!explicit_key && !implicit_key)
			break;

		if (explicit_key)
			advance(p);

		if (cur(p)->type != TK_SCALAR)
		{
			parse_error(p, "expected scalar key");
			break;
		}
		char *key = token_strdup(cur(p), p->alloc);
		if (!key)
		{
			parse_error(p, "OOM");
			break;
		}
		advance(p);

		if (cur(p)->type != TK_MAP_VAL)
		{
			YMLValue null_v = {.type = YML_NULL, .allocator = p->alloc};
			if (strcmp(key, "<<") == 0)
			{
				YMLDEALLOC(key, p->alloc);
				continue;
			}
			hm_set(hm, key, null_v);
			YMLDEALLOC(key, p->alloc);
			continue;
		}
		advance(p);

		YMLValue *val;
		if (cur(p)->type == TK_EOF || cur(p)->type == TK_DOC_END ||
			cur(p)->type == TK_DOC_START)
		{
			val = YMLALLOC(sizeof(YMLValue), p->alloc);
			if (val) {
				val->type = YML_NULL;
				val->allocator = p->alloc;
			}
		}
		else
		{
			val = parse_node(p, indent + 1);
		}
		if (p->ok || !val)
		{
			YMLDEALLOC(key, p->alloc);
			break;
		}

		if (strcmp(key, "<<") == 0)
		{
			apply_merge(hm, val);
			_YMLDestroy(val, (struct _YMLOptionals){0});
		}
		else
		{
			hm_set(hm, key, *val);
			YMLDEALLOC(val, p->alloc);
		}
		YMLDEALLOC(key, p->alloc);
	}
	return v;
}

/* ── parse_node ──────────────────────────────────────────────────────── */

static YMLValue *parse_node(Parser *p, int min_indent)
{
	if (p->ok)
		return NULL;

	const char *tag = NULL;
	size_t tag_len = 0;
	if (cur(p)->type == TK_TAG)
	{
		tag = cur(p)->value;
		tag_len = cur(p)->value_len;
		advance(p);
	}

	const char *anchor_name = NULL;
	size_t anchor_len = 0;
	if (cur(p)->type == TK_ANCHOR)
	{
		anchor_name = cur(p)->value;
		anchor_len = cur(p)->value_len;
		advance(p);
	}

	YMLValue *v = NULL;
	Token *t = cur(p);

	if (t->type == TK_ALIAS)
	{
		YMLValue *orig = anchor_find(p, t->value, t->value_len);
		if (!orig)
		{
			parse_error(p, "unknown alias");
			return NULL;
		}
		advance(p);
		v = yml_deep_copy(orig, NULL);
	}
	else if (t->type == TK_FLOW_SEQ_START)
	{
		v = parse_flow_sequence(p);
	}
	else if (t->type == TK_FLOW_MAP_START)
	{
		v = parse_flow_mapping(p);
	}
	else if (t->type == TK_SEQ_ENTRY && t->col >= min_indent)
	{
		v = parse_block_sequence(p, t->col);
	}
	else if (t->type == TK_MAP_KEY && t->col >= min_indent)
	{
		v = parse_block_mapping(p, t->col);
	}
	else if (t->type == TK_SCALAR && t->col >= min_indent)
	{
		Token *nxt = peek(p, 1);
		if (nxt->type == TK_MAP_VAL && nxt->line == t->line)
		{
			v = parse_block_mapping(p, t->col);
		}
		else
		{
			v = parse_scalar(p);
		}
	}
	else if (t->type == TK_EOF || t->type == TK_DOC_END || t->type == TK_DOC_START)
	{
		v = YMLALLOC(sizeof(YMLValue), p->alloc);
		if (v) {
			v->type = YML_NULL;
			v->allocator = p->alloc;
		}
	}
	else
	{
		v = YMLALLOC(sizeof(YMLValue), p->alloc);
		if (v) {
			v->type = YML_NULL;
			v->allocator = p->alloc;
		}
	}

	if (!v)
	{
		parse_error(p, "OOM");
		return NULL;
	}

	if (tag)
		apply_tag(v, tag, tag_len);

	if (anchor_name)
		anchor_add(p, anchor_name, anchor_len, v);

	return v;
}

/* ── YMLParseStream ──────────────────────────────────────────────────── */

YMLValue **_YMLParseStream(const char *yml_str, struct _YMLOptionals optionals)
{
	g_ok = 0;
	g_error[0] = '\0';

	const char *lex_err = NULL;
	Token *tokens = lex(yml_str, &lex_err);
	if (!tokens)
	{
		set_error(1, lex_err ? lex_err : "lexer error");
		if (optionals.ok)
			*optionals.ok = g_ok;
		if (optionals.error)
			*optionals.error = g_error;
		return NULL;
	}

	Parser p = {.tokens = tokens, .pos = 0, .anchors = da_new(_anchor_entry, 8, NULL), .alloc = optionals.allocator, .ok = 0};

	YMLValue **docs = da_new(YMLValue *, 4, NULL);

	while (cur(&p)->type != TK_EOF)
	{
		if (cur(&p)->type == TK_DOC_START)
		{
			anchors_clear(&p);
			advance(&p);
		}
		if (cur(&p)->type == TK_EOF)
			break;
		YMLValue *doc = parse_node(&p, 0);
		if (p.ok)
		{
			set_error(p.ok, p.error);
			if (doc)
				_YMLDestroy(doc, (struct _YMLOptionals){0});
			break;
		}
		da_push(docs, doc);
		if (cur(&p)->type == TK_DOC_END)
		{
			anchors_clear(&p);
			advance(&p);
		}
	}

	for (size_t i = 0; i < da_len(p.anchors); i++)
		YMLDEALLOC(p.anchors[i].name, p.alloc);
	da_free(p.anchors);
	da_free(tokens);

	if (optionals.ok)
		*optionals.ok = g_ok;
	if (optionals.error)
		*optionals.error = g_error;
	return docs;
}

/* ── публичные функции ─────────────────────────────────────────────── */

YMLValue *_YMLParse(const char *yml_str, struct _YMLOptionals optionals)
{
	g_ok = 0;
	g_error[0] = '\0';

	const char *lex_err = NULL;
	Token *tokens = lex(yml_str, &lex_err);
	if (!tokens)
	{
		set_error(1, lex_err ? lex_err : "lexer error");
		if (optionals.ok)
			*optionals.ok = g_ok;
		if (optionals.error)
			*optionals.error = g_error;
		return NULL;
	}

	Parser p = {
		.tokens = tokens,
		.pos = 0,
		.anchors = da_new(_anchor_entry, 8, NULL),
		.alloc = optionals.allocator,
		.ok = 0,
	};

	/* пропустить необязательный --- */
	if (cur(&p)->type == TK_DOC_START)
		advance(&p);

	YMLValue *root = parse_node(&p, 0);

	/* синхронизировать ошибку в глобальное состояние */
	if (p.ok != 0)
	{
		set_error(p.ok, p.error);
		if (root)
			_YMLDestroy(root, (struct _YMLOptionals){0});
		root = NULL;
	}

	/* освободить имена и deep-copy ноды якорей */
	for (size_t i = 0; i < da_len(p.anchors); i++)
	{
		YMLDEALLOC(p.anchors[i].name, p.alloc);
		_YMLDestroy(p.anchors[i].node, (struct _YMLOptionals){0});
	}
	da_free(p.anchors);
	da_free(tokens);

	if (optionals.ok)
		*optionals.ok = g_ok;
	if (optionals.error)
		*optionals.error = g_error;
	return root;
}

void _YMLDestroy(YMLValue *root, struct _YMLOptionals optionals)
{
	if (!root)
		return;
	yml_value_free_impl(root);
	YMLDEALLOC(root, root->allocator);
	if (optionals.ok)
		*optionals.ok = 0;
}

void _YMLDestroyStream(YMLValue **stream, struct _YMLOptionals optionals)
{
	if (!stream)
		return;
	size_t n = da_len(stream);
	for (size_t i = 0; i < n; i++)
		_YMLDestroy(stream[i], (struct _YMLOptionals){0});
	da_free(stream);
	if (optionals.ok)
		*optionals.ok = 0;
}

// NULL - нода чтобы небыло segfold если ключ не найден
static YMLValue YMLNULLVallue = {.type=YML_NULL, .value={0}}; 

/* индекс = YMLValueType + 1 (YML_ANY=-1 → 0, YML_NULL=0 → 1, …) */
static const char *const _yml_type_names[] = {
	"YML_ANY", "YML_NULL", "YML_BOOL", "YML_INT",
	"YML_FLOAT", "YML_STRING", "YML_ARRAY", "YML_OBJECT",
};
#define YML_TYPE_NAME(t) \
	(_yml_type_names[((int)(t) + 1 < 0 || (int)(t) + 1 >= (int)(sizeof(_yml_type_names)/sizeof(*_yml_type_names))) \
	                 ? 0 : (int)(t) + 1])

YMLValue *_YMLMapGet(void *hm, const char *key, struct _YMLOptionals optionals)
{
	g_ok = 0;
	g_error[0] = '\0'; /* каждый вызов обновляет глобальное состояние */
	if (!hm)
	{
		set_error(1, "YMLMapGet: null object");
		if (optionals.ok)
			*optionals.ok = g_ok;
		if (optionals.error)
			*optionals.error = g_error;
		return &YMLNULLVallue;
	}

	if (optionals.splitter != 0)
	{
		size_t len = strlen(key);
		char *buf = YMLALLOC(len + 1, optionals.allocator);
		if (!buf)
		{
			set_error(2, "YMLMapGet: OOM");
			if (optionals.ok)
				*optionals.ok = g_ok;
			if (optionals.error)
				*optionals.error = g_error;
			return &YMLNULLVallue;
		}
		memcpy(buf, key, len + 1);

		void *cur_hm = hm;
		char *seg = buf;
		for (;;)
		{
			char *sep = seg;
			while (*sep && *sep != optionals.splitter)
				sep++;
			int is_last = (*sep == '\0');
			*sep = '\0';

			YMLValue *v = hm_get((_hm *)cur_hm, seg);
			if (!v)
			{
				snprintf(g_error, sizeof(g_error), "YMLMapGet: key '%s' not found", seg);
				g_ok = 1;
				YMLDEALLOC(buf, optionals.allocator);
				if (optionals.ok)
					*optionals.ok = g_ok;
				if (optionals.error)
					*optionals.error = g_error;
				return &YMLNULLVallue;
			}
			if (is_last)
			{
				YMLDEALLOC(buf, optionals.allocator);
				if (optionals.type != YML_ANY && v->type != optionals.type)
				{
					snprintf(g_error, sizeof(g_error),
							 "YMLMapGet: key '%s' has type %s, expected %s",
							 seg, YML_TYPE_NAME(v->type), YML_TYPE_NAME(optionals.type));
					g_ok = 2;
					if (optionals.ok)
						*optionals.ok = g_ok;
					if (optionals.error)
						*optionals.error = g_error;
					return &YMLNULLVallue;
				}
				if (optionals.ok)
					*optionals.ok = 0;
				return v;
			}
			if (v->type != YML_OBJECT)
			{
				snprintf(g_error, sizeof(g_error),
						 "YMLMapGet: '%s' is not an object (type %d)", seg, v->type);
				g_ok = 1;
				if (optionals.ok)
					*optionals.ok = g_ok;
				if (optionals.error)
					*optionals.error = g_error;
				return &YMLNULLVallue;
			}
			cur_hm = v->value.object;
			seg = sep + 1;
		}
	}

	YMLValue *v = hm_get((_hm *)hm, key);
	if (!v)
	{
		snprintf(g_error, sizeof(g_error), "YMLMapGet: key '%s' not found", key);
		g_ok = 1;
		if (optionals.ok)
			*optionals.ok = g_ok;
		if (optionals.error)
			*optionals.error = g_error;
		return &YMLNULLVallue;
	}
	if (optionals.type != YML_ANY && v->type != optionals.type)
	{
		snprintf(g_error, sizeof(g_error),
				 "YMLMapGet: key '%s' has type %s, expected %s",
				 key, YML_TYPE_NAME(v->type), YML_TYPE_NAME(optionals.type));
		g_ok = 2;
		if (optionals.ok)
			*optionals.ok = g_ok;
		if (optionals.error)
			*optionals.error = g_error;
		return &YMLNULLVallue;
	}
	if (optionals.ok)
		*optionals.ok = 0;
	return v;
}

_YMLMapIter _YMLMapIterBegin(void *hm)
{
	return (_YMLMapIter){._hm = hm, ._i = 0};
}

bool _YMLMapIterNext(_YMLMapIter *iter, const char **key, YMLValue **value)
{
	if (!iter || !iter->_hm)
		return false;
	return hm_next((_hm *)iter->_hm, &iter->_i, key, value);
}

/* ── src/YMLWriter.c ───────────────────────────────────────────── */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── internal value constructors ───────────────────────────────────── */

static YMLValue _yml_mk_str(const char *v, struct YMLAllocator* alloc)
{
	YMLValue r = {.type = YML_STRING, .allocator = alloc};
	r.value.string = v ? yml_strdup(v, alloc) : NULL;
	return r;
}

static YMLValue _yml_mk_node(YMLValue *v, struct YMLAllocator *target_alloc)
{
	if (!v)
		return (YMLValue){.type = YML_NULL};
	YMLValue *cp = yml_deep_copy(v, target_alloc);
	if (!cp)
		return (YMLValue){.type = YML_NULL};
	YMLValue r = *cp;
	YMLDEALLOC(cp, r.allocator);
	return r;
}

/* ── YMLCreate / YMLCreateArr ───────────────────────────────────────── */

YMLValue *_YMLCreate(struct YMLAllocator* alloc)
{
	YMLValue *v = YMLALLOC(sizeof(YMLValue), alloc);
	if (!v)
		return NULL;
	_hm *hm = hm_new(8, alloc);
	if (!hm)
	{
		YMLDEALLOC(v, alloc);
		return NULL;
	}
	v->type = YML_OBJECT;
	v->value.object = hm;
	v->allocator = alloc;
	return v;
}

YMLValue *_YMLCreateArr(struct YMLAllocator* alloc)
{
	YMLValue *v = YMLALLOC(sizeof(YMLValue), alloc);
	if (!v)
		return NULL;
	YMLValue *arr = da_new(YMLValue, 4, alloc);
	if (!arr)
	{
		YMLDEALLOC(v, alloc);
		return NULL;
	}
	v->type = YML_ARRAY;
	v->value.array = arr;
	v->allocator = alloc;
	return v;
}

/* ── helpers: вставка с cleanup дубликата ───────────────────────────── */

static void map_insert(YMLValue *obj, const char *key, YMLValue val)
{
	_hm *hm = (_hm *)obj->value.object;
	YMLValue *existing = hm_get(hm, key);
	if (existing)
		yml_value_free_impl(existing); /* чистим содержимое старого значения */
	hm_set(hm, key, val);
}

/* ── YMLMapAdd ──────────────────────────────────────────────────────── */

void YMLMapAddNull(YMLValue *obj, const char *key)
{
	map_insert(obj, key, (YMLValue){.type = YML_NULL, .allocator = obj->allocator});
}

void _YMLMapAdd_bool(YMLValue *obj, const char *key, bool val)
{
	map_insert(obj, key, (YMLValue){.type = YML_BOOL, .value.boolean = val, .allocator = obj->allocator});
}

void _YMLMapAdd_int(YMLValue *obj, const char *key, long long val)
{
	map_insert(obj, key, (YMLValue){.type = YML_INT, .value.integer = val, .allocator = obj->allocator});
}

void _YMLMapAdd_float(YMLValue *obj, const char *key, double val)
{
	map_insert(obj, key, (YMLValue){.type = YML_FLOAT, .value.number = val, .allocator = obj->allocator});
}

void _YMLMapAdd_str(YMLValue *obj, const char *key, const char *val)
{
	map_insert(obj, key, _yml_mk_str(val, obj->allocator));
}

void _YMLMapAdd_node(YMLValue *obj, const char *key, YMLValue *val)
{
	map_insert(obj, key, _yml_mk_node(val, obj->allocator));
}

static YMLValue *_yml_build_int_arr(const long long *arr, size_t len, struct YMLAllocator* alloc)
{
	YMLValue *da = da_new(YMLValue, len > 0 ? len : 1, alloc);
	for (size_t i = 0; i < len; i++)
	{
		YMLValue e = {.type = YML_INT, .value.integer = arr[i], .allocator = alloc};
		da_push(da, e);
	}
	return da;
}

static YMLValue *_yml_build_float_arr(const double *arr, size_t len, struct YMLAllocator* alloc)
{
	YMLValue *da = da_new(YMLValue, len > 0 ? len : 1, alloc);
	for (size_t i = 0; i < len; i++)
	{
		YMLValue e = {.type = YML_FLOAT, .value.number = arr[i], .allocator = alloc};
		da_push(da, e);
	}
	return da;
}

static YMLValue *_yml_build_str_arr(const char *const *arr, size_t len, struct YMLAllocator* alloc)
{
	YMLValue *da = da_new(YMLValue, len > 0 ? len : 1, alloc);
	for (size_t i = 0; i < len; i++)
	{
		YMLValue e = {.type = YML_STRING,
		              .value.string = arr[i] ? yml_strdup(arr[i], alloc) : NULL,
		              .allocator = alloc};
		da_push(da, e);
	}
	return da;
}

void _YMLMapAddArr_int(YMLValue *obj, const char *key,
                       const long long *arr, size_t len)
{
	YMLValue v = {.type = YML_ARRAY, .value.array = _yml_build_int_arr(arr, len, obj->allocator), .allocator = obj->allocator};
	map_insert(obj, key, v);
}

void _YMLMapAddArr_float(YMLValue *obj, const char *key,
                         const double *arr, size_t len)
{
	YMLValue v = {.type = YML_ARRAY, .value.array = _yml_build_float_arr(arr, len, obj->allocator), .allocator = obj->allocator};
	map_insert(obj, key, v);
}

void _YMLMapAddArr_str(YMLValue *obj, const char *key,
                       const char *const *arr, size_t len)
{
	YMLValue v = {.type = YML_ARRAY, .value.array = _yml_build_str_arr(arr, len, obj->allocator), .allocator = obj->allocator};
	map_insert(obj, key, v);
}

/* ── YMLArrPush ─────────────────────────────────────────────────────── */

void YMLArrPushNull(YMLValue *arr)
{
	YMLValue e = {.type = YML_NULL, .allocator = arr->allocator};
	da_push(arr->value.array, e);
}

void _YMLArrPush_bool(YMLValue *arr, bool val)
{
	YMLValue e = {.type = YML_BOOL, .value.boolean = val, .allocator = arr->allocator};
	da_push(arr->value.array, e);
}

void _YMLArrPush_int(YMLValue *arr, long long val)
{
	YMLValue e = {.type = YML_INT, .value.integer = val, .allocator = arr->allocator};
	da_push(arr->value.array, e);
}

void _YMLArrPush_float(YMLValue *arr, double val)
{
	YMLValue e = {.type = YML_FLOAT, .value.number = val, .allocator = arr->allocator};
	da_push(arr->value.array, e);
}

void _YMLArrPush_str(YMLValue *arr, const char *val)
{
	da_push(arr->value.array, _yml_mk_str(val, arr->allocator));
}

void _YMLArrPush_node(YMLValue *arr, YMLValue *val)
{
	da_push(arr->value.array, _yml_mk_node(val, arr->allocator));
}

void _YMLArrPushArr_int(YMLValue *arr, const long long *c_arr, size_t len)
{
	YMLValue nested = {.type = YML_ARRAY,
	                   .value.array = _yml_build_int_arr(c_arr, len, arr->allocator),
	                   .allocator = arr->allocator};
	da_push(arr->value.array, nested);
}

void _YMLArrPushArr_float(YMLValue *arr, const double *c_arr, size_t len)
{
	YMLValue nested = {.type = YML_ARRAY,
	                   .value.array = _yml_build_float_arr(c_arr, len, arr->allocator),
	                   .allocator = arr->allocator};
	da_push(arr->value.array, nested);
}

void _YMLArrPushArr_str(YMLValue *arr, const char *const *c_arr, size_t len)
{
	YMLValue nested = {.type = YML_ARRAY,
	                   .value.array = _yml_build_str_arr(c_arr, len, arr->allocator),
	                   .allocator = arr->allocator};
	da_push(arr->value.array, nested);
}

/* ══════════════════════════ SERIALIZER ══════════════════════════════ */

typedef struct
{
	FILE   *stream; /* != NULL → поток */
	char   *buf;    /* != NULL → буфер */
	size_t  cap;    /* ёмкость буфера */
	size_t  pos;    /* байт записано */
	int     indent; /* пробелов на уровень */
	int     emit_start; /* 1 → emit "---" */
	int     ok;
	char    error[256];
} _YMLWriteCtx;

/* Записать len байт; при переполнении выставить ok=1 */
static void _yml_emit(_YMLWriteCtx *ctx, const char *s, size_t len)
{
	if (ctx->ok)
		return;
	if (ctx->stream)
	{
		fwrite(s, 1, len, ctx->stream);
	}
	else
	{
		if (ctx->pos + len + 1 > ctx->cap)
		{
			ctx->ok = 1;
			snprintf(ctx->error, sizeof(ctx->error), "YMLWrite: buffer too small");
			return;
		}
		memcpy(ctx->buf + ctx->pos, s, len);
		ctx->pos += len;
	}
}

static void _yml_emit_c(_YMLWriteCtx *ctx, char c)
{
	_yml_emit(ctx, &c, 1);
}

static void _yml_emit_str(_YMLWriteCtx *ctx, const char *s)
{
	if (s)
		_yml_emit(ctx, s, strlen(s));
}

static void _yml_emit_indent(_YMLWriteCtx *ctx, int depth)
{
	for (int i = 0; i < depth * ctx->indent; i++)
		_yml_emit_c(ctx, ' ');
}

/* Нужны ли кавычки для plain-скаляра? */
static int _yml_needs_quotes(const char *s)
{
	if (!s || *s == '\0')
		return 1;
	/* ведущий пробел */
	if (*s == ' ' || *s == '\t')
		return 1;
	/* спецсимволы в начале */
	switch (*s)
	{
	case '-': case ':': case '?': case '#': case '&': case '*':
	case '!': case '|': case '>': case '\'': case '"': case '%':
	case '@': case '`': case '[': case '{': case ',':
		return 1;
	default:
		break;
	}
	/* управляющие символы или спецпоследовательности внутри */
	for (const char *p = s; *p; p++)
	{
		if ((unsigned char)*p < 0x20)
			return 1; /* \n, \r, \t и т.д. */
		if (*p == ':' && (*(p + 1) == ' ' || *(p + 1) == '\0'))
			return 1;
		if (*p == ' ' && *(p + 1) == '#')
			return 1;
	}
	/* строки совпадающие с YAML-скалярами */
	if (strcmp(s, "null") == 0 || strcmp(s, "~") == 0 ||
	    strcmp(s, "true") == 0 || strcmp(s, "false") == 0 ||
	    strcmp(s, ".inf") == 0 || strcmp(s, "-.inf") == 0 ||
	    strcmp(s, ".nan") == 0 || strcmp(s, ".Inf") == 0 ||
	    strcmp(s, ".NaN") == 0 || strcmp(s, ".INF") == 0)
		return 1;
	return 0;
}

/* Записать строку с кавычками + escape если нужно, иначе plain */
static void _yml_write_string(_YMLWriteCtx *ctx, const char *s)
{
	if (!s)
	{
		_yml_emit_str(ctx, "null");
		return;
	}
	if (!_yml_needs_quotes(s))
	{
		_yml_emit_str(ctx, s);
		return;
	}
	_yml_emit_c(ctx, '"');
	for (const char *p = s; *p; p++)
	{
		switch (*p)
		{
		case '"':  _yml_emit(ctx, "\\\"", 2); break;
		case '\\': _yml_emit(ctx, "\\\\", 2); break;
		case '\n': _yml_emit(ctx, "\\n",  2); break;
		case '\r': _yml_emit(ctx, "\\r",  2); break;
		case '\t': _yml_emit(ctx, "\\t",  2); break;
		default:   _yml_emit_c(ctx, *p);      break;
		}
	}
	_yml_emit_c(ctx, '"');
}

static void _yml_write_node(_YMLWriteCtx *ctx, const YMLValue *node, int depth);

static void _yml_write_node(_YMLWriteCtx *ctx, const YMLValue *node, int depth)
{
	if (ctx->ok)
		return;
	if (!node)
	{
		_yml_emit_str(ctx, "null");
		return;
	}
	switch (node->type)
	{
	case YML_NULL:
		_yml_emit_str(ctx, "null");
		break;
	case YML_BOOL:
		_yml_emit_str(ctx, node->value.boolean ? "true" : "false");
		break;
	case YML_INT:
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%lld", (long long)node->value.integer);
		_yml_emit_str(ctx, buf);
		break;
	}
	case YML_FLOAT:
	{
		double d = node->value.number;
		char buf[32];
		if (isinf(d))
			snprintf(buf, sizeof(buf), "%s", d > 0 ? ".inf" : "-.inf");
		else if (isnan(d))
			snprintf(buf, sizeof(buf), ".nan");
		else
			snprintf(buf, sizeof(buf), "%.17g", d);
		_yml_emit_str(ctx, buf);
		break;
	}
	case YML_STRING:
		_yml_write_string(ctx, node->value.string);
		break;
	case YML_ARRAY:
	{
		size_t n = da_len(node->value.array);
		if (n == 0)
		{
			_yml_emit_str(ctx, "[]");
			break;
		}
		for (size_t i = 0; i < n; i++)
		{
			const YMLValue *elem = &node->value.array[i];
			_yml_emit_c(ctx, '\n');
			_yml_emit_indent(ctx, depth);
			_yml_emit_str(ctx, "- ");
			int is_collection = (elem->type == YML_ARRAY || elem->type == YML_OBJECT);
			if (is_collection)
				_yml_write_node(ctx, elem, depth + 1);
			else
				_yml_write_node(ctx, elem, depth);
		}
		break;
	}
	case YML_OBJECT:
	{
		_hm *hm = (_hm *)node->value.object;
		if (hm->len == 0)
		{
			_yml_emit_str(ctx, "{}");
			break;
		}
		size_t idx = 0;
		const char *key;
		YMLValue *val;
		while (hm_next(hm, &idx, &key, &val))
		{
			_yml_emit_c(ctx, '\n');
			_yml_emit_indent(ctx, depth);
			_yml_write_string(ctx, key);
			_yml_emit_c(ctx, ':');
			int is_collection = (val->type == YML_ARRAY || val->type == YML_OBJECT);
			if (is_collection)
				_yml_write_node(ctx, val, depth + 1);
			else
			{
				_yml_emit_c(ctx, ' ');
				_yml_write_node(ctx, val, depth);
			}
		}
		break;
	}
	default:
		_yml_emit_str(ctx, "null");
		break;
	}
}

static void _yml_write(YMLValue *obj, _YMLWriteCtx *ctx)
{
	if (ctx->emit_start)
		_yml_emit_str(ctx, "---");

	/* корневой объект/массив начинается без ведущего \n */
	if (obj && (obj->type == YML_OBJECT || obj->type == YML_ARRAY))
	{
		_hm *hm = NULL;
		size_t n = 0;
		if (obj->type == YML_OBJECT)
		{
			hm = (_hm *)obj->value.object;
			n = hm->len;
		}
		else
			n = da_len(obj->value.array);

		if (n == 0)
		{
			if (ctx->emit_start)
				_yml_emit_c(ctx, '\n');
			_yml_emit_str(ctx, obj->type == YML_OBJECT ? "{}\n" : "[]\n");
			return;
		}

		/* записываем дочерние элементы: каждый начинается с \n+indent */
		if (obj->type == YML_OBJECT)
		{
			size_t idx = 0;
			const char *key;
			YMLValue *val;
			while (hm_next(hm, &idx, &key, &val))
			{
				_yml_emit_c(ctx, '\n');
				_yml_write_string(ctx, key);
				_yml_emit_c(ctx, ':');
				int is_col = (val->type == YML_ARRAY || val->type == YML_OBJECT);
				if (is_col)
					_yml_write_node(ctx, val, 1);
				else
				{
					_yml_emit_c(ctx, ' ');
					_yml_write_node(ctx, val, 0);
				}
			}
		}
		else
		{
			for (size_t i = 0; i < n; i++)
			{
				const YMLValue *elem = &obj->value.array[i];
				_yml_emit_c(ctx, '\n');
				_yml_emit_str(ctx, "- ");
				int is_col = (elem->type == YML_ARRAY || elem->type == YML_OBJECT);
				if (is_col)
					_yml_write_node(ctx, elem, 1);
				else
					_yml_write_node(ctx, elem, 0);
			}
		}
		_yml_emit_c(ctx, '\n');
	}
	else
	{
		if (ctx->emit_start)
			_yml_emit_c(ctx, '\n');
		_yml_write_node(ctx, obj, 0);
		_yml_emit_c(ctx, '\n');
	}
}

/* ── public ─────────────────────────────────────────────────────────── */

void _YMLWriteStream(YMLValue *obj, FILE *stream, struct _YMLWriteOptions opts)
{
	_YMLWriteCtx ctx = {
		.stream = stream,
		.buf    = NULL,
		.cap    = 0,
		.pos    = 0,
		.indent = opts.indent > 0 ? opts.indent : 2,
		.emit_start = opts.start,
		.ok     = 0,
	};
	_yml_write(obj, &ctx);
	if (opts.ok)
		*opts.ok = ctx.ok;
	if (opts.error && ctx.ok)
		*opts.error = ctx.error;
}

int _YMLWriteBuf(YMLValue *obj, char *buf, size_t cap, struct _YMLWriteOptions opts)
{
	_YMLWriteCtx ctx = {
		.stream = NULL,
		.buf    = buf,
		.cap    = cap,
		.pos    = 0,
		.indent = opts.indent > 0 ? opts.indent : 2,
		.emit_start = opts.start,
		.ok     = 0,
	};
	_yml_write(obj, &ctx);
	if (ctx.ok == 0 && cap > 0)
		buf[ctx.pos] = '\0';
	if (opts.ok)
		*opts.ok = ctx.ok;
	if (opts.error && ctx.ok)
		*opts.error = ctx.error;
	return ctx.ok ? -1 : (int)ctx.pos;
}

/* ── src/_allocator.c ──────────────────────────────────────────── */
#include <stdio.h>
#include <stdlib.h>



static void* malloc_alloc(size_t len, const char* FILE, int LINE) {
	void* data = malloc(len);
	if(data==NULL) fprintf(stderr, "[ERROR] alloc failed file: %s:%d\n", FILE, LINE);
	return data;
}

static void* malloc_realloc(void* ptr, size_t new_len, const char* FILE, int LINE) {
	void* data = realloc(ptr, new_len);
	if(data==NULL) fprintf(stderr, "[ERROR] realloc failed file: %s:%d\n", FILE, LINE);
	return data;
}

static void* malloc_calloc(size_t n, size_t size, const char* FILE, int LINE) {
	void* data = calloc(n, size);
	if(data==NULL) fprintf(stderr, "[ERROR] calloc failed file: %s:%d\n", FILE, LINE);
	return data;
}

static void malloc_dealloc(void* ptr, const char* FILE, int LINE) {
	free(ptr);
}


YML_PRIVATE void* _yml_alloc(size_t len, struct YMLAllocator* ctx, const char* FILE, int LINE){
	if (ctx && ctx->alloc) return ctx->alloc(len, ctx->ctx, FILE, LINE);
	return malloc_alloc(len, FILE, LINE);
}

YML_PRIVATE void* _yml_realloc(void* ptr, size_t new_len, struct YMLAllocator* ctx, const char* FILE, int LINE){
	if (ctx && ctx->realloc) return ctx->realloc(ptr, new_len, ctx->ctx, FILE, LINE);
	return malloc_realloc(ptr, new_len, FILE, LINE);
}

YML_PRIVATE void* _yml_calloc(size_t n, size_t size, struct YMLAllocator* ctx, const char* FILE, int LINE){
	if (ctx && ctx->calloc) return ctx->calloc(n, size, ctx->ctx, FILE, LINE);
	return malloc_calloc(n, size, FILE, LINE);
}

YML_PRIVATE void  _yml_dealloc(void* ptr, struct YMLAllocator* ctx, const char* FILE, int LINE){
	if (ctx && ctx->dealloc) return ctx->dealloc(ptr, ctx->ctx, FILE, LINE);
	return malloc_dealloc(ptr, FILE, LINE);
}

#endif /* YMLPARSER_IMPLEMENTATION */

#endif /* YMLPARSER_H */
