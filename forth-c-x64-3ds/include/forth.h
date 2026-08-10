#ifndef FORTH_H
#define FORTH_H

#include "pal.h"
#include <assert.h>

#define FORTH_LIKELY(x) __builtin_expect(!!(x), 1)
#define FORTH_UNLIKELY(x) __builtin_expect(!!(x), 0)

constexpr size_t STACK_CAPACITY = 0x100;
constexpr size_t DICT_CAPACITY = 0x10000;

struct ForthEngine;
typedef void (*PrimitiveFn)(struct ForthEngine *vm);

constexpr uint8_t FLAG_IMMEDIATE = 0x01;
constexpr uint8_t FLAG_HIDDEN = 0x02;

// 32-Byte Cache-Aligned Word Header (1 cache line on 3DS ARM11)
typedef struct WordHeader
{
    alignas(32) struct WordHeader *link;
    void *code;
    uint8_t flags;
    uint8_t name_len;
    uint16_t param_count;
#if __INTPTR_WIDTH__ == 64
    char name[12];
#else
    char name[20];
#endif
} WordHeader;

constexpr size_t MAX_NAME_LENGTH = sizeof(((WordHeader *)0)->name) - 1;

typedef struct ForthEngine
{
    cell_t data_stack[STACK_CAPACITY];
    int sp;
    cell_t return_stack[STACK_CAPACITY];
    int rp;
    alignas(32) uint8_t dict_buf[DICT_CAPACITY];
    cell_t here;
    WordHeader *latest;
    cell_t state; // 0 = Interpreting, 1 = Compiling.
    bool quit;
    bool silent;
    const char *source_ptr;

    char last_command[64];
} ForthEngine;

static_assert(sizeof(WordHeader) == 32, "WordHeader MUST be exactly 32 bytes.");
static_assert(alignof(WordHeader) == 32, "WordHeader MUST be 32-byte aligned.");

// Core Engine API.
void forth_init(ForthEngine *vm);
void forth_push(ForthEngine *vm, cell_t val);
[[nodiscard]] cell_t forth_pop(ForthEngine *vm);
WordHeader *forth_alloc_word(ForthEngine *vm);
void forth_compile_cell(ForthEngine *vm, cell_t val);
WordHeader *forth_find_word(ForthEngine *vm, const char *name);
bool forth_next_token(ForthEngine *vm, char *out_buf, size_t max_len);
void forth_eval(ForthEngine *vm, const char *input);

#endif // FORTH_H
