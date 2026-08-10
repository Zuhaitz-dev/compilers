#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../../include/forth.h"

static int forth_strcasecmp(const char *a, const char *b)
{
    while (*a && *b)
    {
        int ca = toupper((unsigned char)*a);
        int cb = toupper((unsigned char)*b);
        if (ca != cb)
        {
            return ca - cb;
        }
        a++;
        b++;
    }
    return toupper((unsigned char)*a) - toupper((unsigned char)*b);
}

#ifdef __3DS__
#include <3ds.h>
#endif

enum OpCode
{
    OP_NOP,          // 0
    OP_ADD,          // 1
    OP_SUB,          // 2
    OP_MUL,          // 3
    OP_DUP,          // 4
    OP_DROP,         // 5
    OP_SWAP,         // 6
    OP_DOT,          // 7
    OP_DOT_S,        // 8
    OP_LIT,          // 9
    OP_EXIT,         // 10
    OP_OVER,         // 11
    OP_ROT,          // 12
    OP_QDUP,         // 13
    OP_NIP,          // 14
    OP_TUCK,         // 15
    OP_DIV,          // 16
    OP_MOD,          // 17
    OP_DIVMOD,       // 18
    OP_INC,          // 19
    OP_DEC,          // 20
    OP_EQUALS,       // 21
    OP_LESS,         // 22
    OP_GREATER,      // 23
    OP_ZEQUALS,      // 24
    OP_AND,          // 25
    OP_OR,           // 26
    OP_XOR,          // 27
    OP_INVERT,       // 28
    OP_HERE,         // 29
    OP_ALLOT,        // 30
    OP_FETCH,        // 31
    OP_STORE,        // 32
    OP_CFETCH,       // 33
    OP_CSTORE,       // 34
    OP_BRANCH,       // 35
    OP_ZBRANCH,      // 36
    OP_CR,           // 37
    OP_EMIT,         // 38
    OP_CLS,          // 39
    OP_KEY_A,        // 40
    OP_TOUCH_COORDS, // 41
    OP_WAIT_TOUCH,   // 42
    OP_DOCOL,        // 43
    OP_COLON,        // 44
    OP_SEMICOLON,    // 45
    OP_DOVAR,        // 46
    OP_CREATE,       // 47
    OP_VARIABLE,     // 48
    OP_COMMA,        // 49
    OP_IF,           // 50
    OP_ELSE,         // 51
    OP_THEN,         // 52
    OP_BEGIN,        // 53
    OP_UNTIL,        // 54
    OP_INCLUDE,      // 55
    OP_COMMENT,      // 56
    OP_PAREN,        // 57
    OP_DOT_QUOTE,    // 58
    OP_PRINT_STR,    // 59
    OP_GR_COLOR,     // 60
    OP_GR_PIXEL,     // 61
    OP_GR_RECT,      // 62
    OP_GR_CLS,       // 63
    OP_GR_FLUSH,     // 64
    OP_SILENT,       // 65
    OP_VERBOSE,      // 66
    OP_DO,           // 67
    OP_LOOP,         // 68
    OP_PLUS_LOOP,    // 69
    OP_I,            // 70
    OP_UNLOOP,       // 71
    OP_GREAT_EQ,     // 72
    OP_LESS_EQ,      // 73
    OP_DEPTH,        // 74
    OP_TICK,         // 75
    OP_EXECUTE,      // 76
    OP_STATE,        // 77
    OP_LEAVE,        // 78
    OP_ABS,          // 79
    OP_NEGATE,       // 80
    OP_MIN,          // 81
    OP_MAX,          // 82
    OP_TOUCH_Q,      // 83
    OP_COUNT         // sentinel, must be last
};

void forth_push(ForthEngine *vm, cell_t val)
{
    if (FORTH_UNLIKELY(vm->sp >= (int)STACK_CAPACITY - 1))
    {
        g_pal.print_str(" Error: Data stack overflow!\n");
        return;
    }
    vm->data_stack[++vm->sp] = val;
}

cell_t forth_pop(ForthEngine *vm)
{
    if (FORTH_UNLIKELY(vm->sp < 0))
    {
        g_pal.print_str(" Error: Data stack underflow!\n");
        return 0;
    }
    return vm->data_stack[vm->sp--];
}

WordHeader *forth_alloc_word(ForthEngine *vm)
{
    vm->here = (vm->here + 31) & ~31;
    if (FORTH_UNLIKELY(vm->here + sizeof(WordHeader) >= DICT_CAPACITY))
    {
        g_pal.print_str(" Error: Dictionary full!\n");
        return nullptr;
    }
    WordHeader *w = (WordHeader *)&vm->dict_buf[vm->here];
    vm->here += sizeof(WordHeader);
    return w;
}

void forth_compile_cell(ForthEngine *vm, cell_t val)
{
    if (FORTH_UNLIKELY(vm->here + sizeof(cell_t) >= DICT_CAPACITY))
    {
        g_pal.print_str(" Error: Dictionary memory exhausted!\n");
        return;
    }
    *(cell_t *)&vm->dict_buf[vm->here] = val;
    vm->here += sizeof(cell_t);
}

WordHeader *forth_find_word(ForthEngine *vm, const char *name)
{
    WordHeader *curr = vm->latest;
    while (curr != nullptr)
    {
        if (!(curr->flags & FLAG_HIDDEN) && 0 == forth_strcasecmp(curr->name, name))
        {
            return curr;
        }
        curr = curr->link;
    }
    return nullptr;
}

bool forth_next_token(ForthEngine *vm, char *out_buf, size_t max_len)
{
    if (!vm->source_ptr || !*vm->source_ptr)
    {
        return false;
    }
    while (*vm->source_ptr && isspace((unsigned char)*vm->source_ptr))
    {
        vm->source_ptr++;
    }
    if (!*vm->source_ptr)
    {
        return false;
    }

    size_t len = 0;
    bool truncated = false;
    while (*vm->source_ptr && !isspace((unsigned char)*vm->source_ptr))
    {
        if (len < max_len - 1)
        {
            out_buf[len++] = *vm->source_ptr++;
        }
        else
        {
            vm->source_ptr++;
            truncated = true;
        }
    }
    out_buf[len] = '\0';
    if (truncated)
    {
        g_pal.print_str(" Warning: Word truncated: ");
        g_pal.print_str(out_buf);
        g_pal.emit_char('\n');
    }
    return len > 0;
}

static void register_primitive(ForthEngine *vm, const char *name, void *label_addr, uint8_t flags)
{
    WordHeader *w = forth_alloc_word(vm);
    if (!w)
    {
        return;
    }
    w->link = vm->latest;
    w->flags = flags;
    w->name_len = (uint8_t)strlen(name);
    strncpy(w->name, name, sizeof(w->name) - 1);
    w->name[sizeof(w->name) - 1] = '\0';
    w->code = label_addr;
    vm->latest = w;
}

static void *g_exit_label = nullptr;
static void *g_lit_label = nullptr;
static void *g_docol_label = nullptr;
static void *g_dovar_label = nullptr;
static void *g_branch_label = nullptr;
static void *g_zbranch_label = nullptr;
static void *g_print_str_label = nullptr;
static void *g_tick_label = nullptr;

[[gnu::hot]] static void **execute_threaded(ForthEngine *vm, void **program)
{
    void **ip = program;
#define FORTH_NEXT() goto **(ip++)

    static void *dispatch_table[] = {
        &&L_NOP,     // OP_NOP
        &&L_ADD,     // OP_ADD
        &&L_SUB,     // OP_SUB
        &&L_MUL,     // OP_MUL
        &&L_DUP,     // OP_DUP
        &&L_DROP,    // OP_DROP
        &&L_SWAP,    // OP_SWAP
        &&L_DOT,     // OP_DOT
        &&L_DOT_S,   // OP_DOT_S
        &&L_LIT,     // OP_LIT
        &&L_EXIT,    // OP_EXIT
        &&L_OVER,    // OP_OVER
        &&L_ROT,     // OP_ROT
        &&L_QDUP,    // OP_QDUP
        &&L_NIP,     // OP_NIP
        &&L_TUCK,    // OP_TUCK
        &&L_DIV,     // OP_DIV
        &&L_MOD,     // OP_MOD
        &&L_DIVMOD,  // OP_DIVMOD
        &&L_INC,     // OP_INC
        &&L_DEC,     // OP_DEC
        &&L_EQUALS,  // OP_EQUALS
        &&L_LESS,    // OP_LESS
        &&L_GREATER, // OP_GREATER
        &&L_ZEQUALS, // OP_ZEQUALS
        &&L_AND,     // OP_AND
        &&L_OR,      // OP_OR
        &&L_XOR,     // OP_XOR
        &&L_INVERT,  // OP_INVERT
        &&L_HERE,    // OP_HERE
        &&L_ALLOT,   // OP_ALLOT
        &&L_FETCH,   // OP_FETCH
        &&L_STORE,   // OP_STORE
        &&L_CFETCH,  // OP_CFETCH
        &&L_CSTORE,  // OP_CSTORE
        &&L_BRANCH,  // OP_BRANCH
        &&L_ZBRANCH, // OP_ZBRANCH
        &&L_CR,      // OP_CR
        &&L_EMIT,    // OP_EMIT
        &&L_CLS,     // OP_CLS
#ifdef __3DS__
        &&L_KEY_A,        // OP_KEY_A
        &&L_TOUCH_COORDS, // OP_TOUCH_COORDS
        &&L_WAIT_TOUCH,   // OP_WAIT_TOUCH
#else
        &&L_NOP, // OP_KEY_A
        &&L_NOP, // OP_TOUCH_COORDS
        &&L_NOP, // OP_WAIT_TOUCH
#endif
        &&L_DOCOL,     // OP_DOCOL
        &&L_COLON,     // OP_COLON
        &&L_SEMICOLON, // OP_SEMICOLON
        &&L_DOVAR,     // OP_DOVAR
        &&L_CREATE,    // OP_CREATE
        &&L_VARIABLE,  // OP_VARIABLE
        &&L_COMMA,     // OP_COMMA
        &&L_IF,        // OP_IF
        &&L_ELSE,      // OP_ELSE
        &&L_THEN,      // OP_THEN
        &&L_BEGIN,     // OP_BEGIN
        &&L_UNTIL,     // OP_UNTIL
        &&L_INCLUDE,   // OP_INCLUDE
        &&L_COMMENT,   // OP_COMMENT
        &&L_PAREN,     // OP_PAREN
        &&L_DOT_QUOTE, // OP_DOT_QUOTE
        &&L_PRINT_STR, // OP_PRINT_STR
        &&L_GR_COLOR,  // OP_GR_COLOR
        &&L_GR_PIXEL,  // OP_GR_PIXEL
        &&L_GR_RECT,   // OP_GR_RECT
        &&L_GR_CLS,    // OP_GR_CLS
        &&L_GR_FLUSH,  // OP_GR_FLUSH
        &&L_SILENT,    // OP_SILENT
        &&L_VERBOSE,   // OP_VERBOSE
        &&L_DO,        // OP_DO
        &&L_LOOP,      // OP_LOOP
        &&L_PLUS_LOOP, // OP_PLUS_LOOP
        &&L_I,         // OP_I
        &&L_UNLOOP,    // OP_UNLOOP
        &&L_GREAT_EQ,  // OP_GREAT_EQ
        &&L_LESS_EQ,   // OP_LESS_EQ
        &&L_DEPTH,     // OP_DEPTH
        &&L_TICK,      // OP_TICK
        &&L_EXECUTE,   // OP_EXECUTE
        &&L_STATE,     // OP_STATE
        &&L_LEAVE,     // OP_LEAVE
        &&L_ABS,       // OP_ABS
        &&L_NEGATE,    // OP_NEGATE
        &&L_MIN,       // OP_MIN
        &&L_MAX,       // OP_MAX
#ifdef __3DS__
        &&L_TOUCH_Q, // OP_TOUCH_Q
#else
        &&L_NOP, // OP_TOUCH_Q
#endif
    };
    static_assert(sizeof(dispatch_table) / sizeof(dispatch_table[0]) == OP_COUNT,
                  "dispatch_table must have one entry per OpCode");

    if (FORTH_UNLIKELY(nullptr == program))
    {
        return dispatch_table;
    }

    FORTH_NEXT();

L_NOP:
    FORTH_NEXT();
L_ADD:
{
    cell_t b = forth_pop(vm);
    cell_t a = forth_pop(vm);
    forth_push(vm, a + b);
}
    FORTH_NEXT();
L_SUB:
{
    cell_t b = forth_pop(vm);
    cell_t a = forth_pop(vm);
    forth_push(vm, a - b);
}
    FORTH_NEXT();
L_MUL:
{
    cell_t b = forth_pop(vm);
    cell_t a = forth_pop(vm);
    forth_push(vm, a * b);
}
    FORTH_NEXT();
L_DUP:
    if (FORTH_LIKELY(vm->sp >= 0))
    {
        forth_push(vm, vm->data_stack[vm->sp]);
    }
    FORTH_NEXT();
L_DROP:
    (void)forth_pop(vm);
    FORTH_NEXT();
L_SWAP:
{
    cell_t b = forth_pop(vm);
    cell_t a = forth_pop(vm);
    forth_push(vm, b);
    forth_push(vm, a);
}
    FORTH_NEXT();
L_DOT:
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld ", (long long)(scell_t)forth_pop(vm));
    g_pal.print_str(buf);
}
    FORTH_NEXT();
L_DOT_S:
{
    char buf[64];
    snprintf(buf, sizeof(buf), "<%d> ", vm->sp + 1);
    g_pal.print_str(buf);
    for (int i = 0; i <= vm->sp; ++i)
    {
        snprintf(buf, sizeof(buf), "%lld ", (long long)(scell_t)vm->data_stack[i]);
        g_pal.print_str(buf);
    }
    g_pal.emit_char('\n');
}
    FORTH_NEXT();
L_LIT:
    forth_push(vm, (cell_t)(*ip++));
    FORTH_NEXT();
L_EXIT:
    if (vm->rp >= 0)
    {
        ip = (void **)vm->return_stack[vm->rp--];
        FORTH_NEXT();
    }
    return nullptr;

L_OVER:
    if (FORTH_LIKELY(vm->sp >= 1))
    {
        forth_push(vm, vm->data_stack[vm->sp - 1]);
    }
    FORTH_NEXT();
L_ROT:
    if (FORTH_LIKELY(vm->sp >= 2))
    {
        cell_t c = forth_pop(vm);
        cell_t b = forth_pop(vm);
        cell_t a = forth_pop(vm);
        forth_push(vm, b);
        forth_push(vm, c);
        forth_push(vm, a);
    }
    FORTH_NEXT();
L_QDUP:
    if (FORTH_LIKELY(vm->sp >= 0 && vm->data_stack[vm->sp] != 0))
    {
        forth_push(vm, vm->data_stack[vm->sp]);
    }
    FORTH_NEXT();
L_NIP:
{
    cell_t b = forth_pop(vm);
    (void)forth_pop(vm);
    forth_push(vm, b);
}
    FORTH_NEXT();
L_TUCK:
{
    cell_t b = forth_pop(vm);
    cell_t a = forth_pop(vm);
    forth_push(vm, b);
    forth_push(vm, a);
    forth_push(vm, b);
}
    FORTH_NEXT();
L_DIV:
{
    cell_t b = forth_pop(vm);
    cell_t a = forth_pop(vm);
    if (b == 0)
    {
        g_pal.print_str(" Error: Division by zero!\n");
        forth_push(vm, 0);
    }
    else
    {
        forth_push(vm, a / b);
    }
}
    FORTH_NEXT();
L_MOD:
{
    cell_t b = forth_pop(vm);
    cell_t a = forth_pop(vm);
    if (b == 0)
    {
        g_pal.print_str(" Error: Division by zero!\n");
        forth_push(vm, 0);
    }
    else
    {
        forth_push(vm, a % b);
    }
}
    FORTH_NEXT();
L_DIVMOD:
{
    cell_t b = forth_pop(vm);
    cell_t a = forth_pop(vm);
    if (b == 0)
    {
        g_pal.print_str(" Error: Division by zero!\n");
        forth_push(vm, 0);
        forth_push(vm, 0);
    }
    else
    {
        forth_push(vm, a % b);
        forth_push(vm, a / b);
    }
}
    FORTH_NEXT();
L_INC:
    if (FORTH_LIKELY(vm->sp >= 0))
    {
        vm->data_stack[vm->sp]++;
    }
    FORTH_NEXT();
L_DEC:
    if (FORTH_LIKELY(vm->sp >= 0))
    {
        vm->data_stack[vm->sp]--;
    }
    FORTH_NEXT();

L_EQUALS:
{
    cell_t b = forth_pop(vm);
    cell_t a = forth_pop(vm);
    forth_push(vm, (a == b) ? -1 : 0);
}
    FORTH_NEXT();
L_LESS:
{
    scell_t b = (scell_t)forth_pop(vm);
    scell_t a = (scell_t)forth_pop(vm);
    forth_push(vm, (a < b) ? -1 : 0);
}
    FORTH_NEXT();
L_GREATER:
{
    scell_t b = (scell_t)forth_pop(vm);
    scell_t a = (scell_t)forth_pop(vm);
    forth_push(vm, (a > b) ? -1 : 0);
}
    FORTH_NEXT();
L_ZEQUALS:
{
    cell_t a = forth_pop(vm);
    forth_push(vm, (a == 0) ? -1 : 0);
}
    FORTH_NEXT();
L_AND:
{
    cell_t b = forth_pop(vm);
    cell_t a = forth_pop(vm);
    forth_push(vm, a & b);
}
    FORTH_NEXT();
L_OR:
{
    cell_t b = forth_pop(vm);
    cell_t a = forth_pop(vm);
    forth_push(vm, a | b);
}
    FORTH_NEXT();
L_XOR:
{
    cell_t b = forth_pop(vm);
    cell_t a = forth_pop(vm);
    forth_push(vm, a ^ b);
}
    FORTH_NEXT();
L_INVERT:
    if (FORTH_LIKELY(vm->sp >= 0))
    {
        vm->data_stack[vm->sp] = ~vm->data_stack[vm->sp];
    }
    FORTH_NEXT();

L_HERE:
    forth_push(vm, (cell_t)&vm->dict_buf[vm->here]);
    FORTH_NEXT();
L_ALLOT:
    vm->here += forth_pop(vm);
    FORTH_NEXT();
L_FETCH:
{
    cell_t *addr = (cell_t *)forth_pop(vm);
    forth_push(vm, *addr);
}
    FORTH_NEXT();
L_STORE:
{
    cell_t *addr = (cell_t *)forth_pop(vm);
    cell_t val = forth_pop(vm);
    *addr = val;
}
    FORTH_NEXT();
L_CFETCH:
{
    uint8_t *addr = (uint8_t *)forth_pop(vm);
    forth_push(vm, *addr);
}
    FORTH_NEXT();
L_CSTORE:
{
    uint8_t *addr = (uint8_t *)forth_pop(vm);
    uint8_t val = (uint8_t)forth_pop(vm);
    *addr = val;
}
    FORTH_NEXT();
L_BRANCH:
    ip = (void **)(*ip);
    FORTH_NEXT();
L_ZBRANCH:
{
    void **target = (void **)(*ip++);
    if (forth_pop(vm) == 0)
    {
        ip = target;
    }
}
    FORTH_NEXT();

L_CR:
    g_pal.emit_char('\n');
    FORTH_NEXT();
L_EMIT:
    g_pal.emit_char((char)forth_pop(vm));
    FORTH_NEXT();
L_CLS:
    g_pal.print_str("\x1b[2J\x1b[H");
    vm->silent = false;
    FORTH_NEXT();

#ifdef __3DS__
L_KEY_A:
    hidScanInput();
    forth_push(vm, (hidKeysHeld() & KEY_A) ? -1 : 0);
    FORTH_NEXT();
L_TOUCH_COORDS:
{
    hidScanInput();
    touchPosition touch;
    hidTouchRead(&touch);
    forth_push(vm, touch.px);
    forth_push(vm, touch.py);
}
    FORTH_NEXT();
L_WAIT_TOUCH:
{
    touchPosition touch;
    do
    {
        gspWaitForVBlank();
        hidScanInput();
        hidTouchRead(&touch);
    } while ((hidKeysHeld() & KEY_TOUCH) == 0 && aptMainLoop());

    forth_push(vm, touch.px);
    forth_push(vm, touch.py);
}
    FORTH_NEXT();
L_TOUCH_Q:
    hidScanInput();
    forth_push(vm, (hidKeysHeld() & KEY_TOUCH) ? -1 : 0);
    FORTH_NEXT();
#endif

L_DOCOL:
    if (FORTH_UNLIKELY(vm->rp >= (int)STACK_CAPACITY - 1))
    {
        g_pal.print_str(" Error: Return stack overflow!\n");
        return nullptr;
    }
    vm->return_stack[++vm->rp] = (cell_t)(ip + 1);
    ip = (void **)(*ip);
    FORTH_NEXT();

L_COLON:
{
    char token[MAX_NAME_LENGTH + 1];
    if (forth_next_token(vm, token, sizeof(token)))
    {
        WordHeader *w = forth_alloc_word(vm);
        if (!w)
        {
            FORTH_NEXT();
        }
        w->link = vm->latest;
        w->flags = 0;
        w->name_len = (uint8_t)strlen(token);
        strncpy(w->name, token, sizeof(w->name) - 1);
        w->name[sizeof(w->name) - 1] = '\0';
        w->code = g_docol_label;
        vm->latest = w;
        vm->state = 1;
    }
}
    FORTH_NEXT();

L_SEMICOLON:
    if (vm->state == 0)
    {
        g_pal.print_str(" Error: ; outside compile mode!\n");
        FORTH_NEXT();
    }
    forth_compile_cell(vm, (cell_t)g_exit_label);
    vm->state = 0;
    FORTH_NEXT();

L_DOVAR:
    forth_push(vm, (cell_t)(*ip++));
    FORTH_NEXT();

L_CREATE:
{
    char token[MAX_NAME_LENGTH + 1];
    if (forth_next_token(vm, token, sizeof(token)))
    {
        WordHeader *w = forth_alloc_word(vm);
        if (!w)
        {
            FORTH_NEXT();
        }
        w->link = vm->latest;
        w->flags = 0;
        w->name_len = (uint8_t)strlen(token);
        strncpy(w->name, token, sizeof(w->name) - 1);
        w->name[sizeof(w->name) - 1] = '\0';
        w->code = g_dovar_label;
        vm->latest = w;
    }
}
    FORTH_NEXT();

L_VARIABLE:
{
    char token[MAX_NAME_LENGTH + 1];
    if (forth_next_token(vm, token, sizeof(token)))
    {
        WordHeader *w = forth_alloc_word(vm);
        if (!w)
        {
            FORTH_NEXT();
        }
        w->link = vm->latest;
        w->flags = 0;
        w->name_len = (uint8_t)strlen(token);
        strncpy(w->name, token, sizeof(w->name) - 1);
        w->name[sizeof(w->name) - 1] = '\0';
        w->code = g_dovar_label;
        vm->latest = w;
        forth_compile_cell(vm, 0);
    }
}
    FORTH_NEXT();

L_COMMA:
    forth_compile_cell(vm, forth_pop(vm));
    FORTH_NEXT();

L_IF:
    if (vm->state == 0)
    {
        g_pal.print_str(" Error: IF outside compile mode!\n");
    }
    else
    {
        forth_compile_cell(vm, (cell_t)g_zbranch_label);
        forth_push(vm, (cell_t)&vm->dict_buf[vm->here]);
        forth_compile_cell(vm, 0);
    }
    FORTH_NEXT();

L_ELSE:
    if (vm->state == 0)
    {
        g_pal.print_str(" Error: ELSE outside compile mode!\n");
    }
    else
    {
        forth_compile_cell(vm, (cell_t)g_branch_label);
        cell_t *new_patch = (cell_t *)&vm->dict_buf[vm->here];
        forth_compile_cell(vm, 0);

        cell_t *old_patch = (cell_t *)forth_pop(vm);
        *old_patch = (cell_t)&vm->dict_buf[vm->here];

        forth_push(vm, (cell_t)new_patch);
    }
    FORTH_NEXT();

L_THEN:
    if (vm->state == 0)
    {
        g_pal.print_str(" Error: THEN outside compile mode!\n");
    }
    else
    {
        cell_t *patch_addr = (cell_t *)forth_pop(vm);
        *patch_addr = (cell_t)&vm->dict_buf[vm->here];
    }
    FORTH_NEXT();

L_BEGIN:
    if (vm->state == 0)
    {
        g_pal.print_str(" Error: BEGIN outside compile mode!\n");
    }
    else
    {
        forth_push(vm, (cell_t)&vm->dict_buf[vm->here]);
    }
    FORTH_NEXT();

L_UNTIL:
    if (vm->state == 0)
    {
        g_pal.print_str(" Error: UNTIL outside compile mode!\n");
    }
    else
    {
        cell_t target = forth_pop(vm);
        forth_compile_cell(vm, (cell_t)g_zbranch_label);
        forth_compile_cell(vm, target);
    }
    FORTH_NEXT();

L_INCLUDE:
{
    char filepath[256];
    if (forth_next_token(vm, filepath, sizeof(filepath)))
    {
        FILE *fp = fopen(filepath, "rb");
        if (!fp)
        {
            g_pal.print_str(" Error: Could not open SD card file: ");
            g_pal.print_str(filepath);
            g_pal.print_str("\n");
        }
        else
        {
            fseek(fp, 0, SEEK_END);
            long fsize = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            if (fsize > 0)
            {
                char *buf = (char *)g_pal.alloc_mem(fsize + 1);
                if (buf)
                {
                    size_t read_bytes = fread(buf, 1, fsize, fp);
                    buf[read_bytes] = '\0';
                    g_pal.print_str(" >> Loading SD Script: ");
                    g_pal.print_str(filepath);
                    g_pal.print_str("\n");
                    forth_eval(vm, buf);
                    g_pal.free_mem(buf);
                }
            }
            fclose(fp);
        }
    }
}
    FORTH_NEXT();

L_COMMENT:
    while (*vm->source_ptr && *vm->source_ptr != '\n' && *vm->source_ptr != '\r')
    {
        vm->source_ptr++;
    }
    FORTH_NEXT();

L_PAREN:
    while (*vm->source_ptr && *vm->source_ptr != ')')
    {
        vm->source_ptr++;
    }
    if (*vm->source_ptr == ')')
    {
        vm->source_ptr++;
    }
    FORTH_NEXT();

L_PRINT_STR:
{
    const char *str = (const char *)ip;
    g_pal.print_str(str);
    size_t len = strlen(str) + 1;
    size_t cells = (len + sizeof(cell_t) - 1) / sizeof(cell_t);
    ip += cells;
}
    FORTH_NEXT();

L_DOT_QUOTE:
{
    if (*vm->source_ptr == ' ')
    {
        vm->source_ptr++;
    }
    char str_buf[256];
    size_t len = 0;
    while (*vm->source_ptr && *vm->source_ptr != '"' && len < sizeof(str_buf) - 1)
    {
        str_buf[len++] = *vm->source_ptr++;
    }
    str_buf[len] = '\0';
    if (*vm->source_ptr == '"')
    {
        vm->source_ptr++;
    }

    if (vm->state == 0)
    {
        g_pal.print_str(str_buf);
    }
    else
    {
        forth_compile_cell(vm, (cell_t)g_print_str_label);
        size_t alloc_len = len + 1;
        if (vm->here + alloc_len < DICT_CAPACITY)
        {
            memcpy(&vm->dict_buf[vm->here], str_buf, alloc_len);
            size_t cells = (alloc_len + sizeof(cell_t) - 1) / sizeof(cell_t);
            vm->here += cells * sizeof(cell_t);
        }
    }
}
    FORTH_NEXT();

L_GR_COLOR:
{
    uint8_t b = (uint8_t)forth_pop(vm);
    uint8_t g = (uint8_t)forth_pop(vm);
    uint8_t r = (uint8_t)forth_pop(vm);
    if (g_pal.set_color)
    {
        g_pal.set_color(r, g, b);
    }
}
    FORTH_NEXT();

L_GR_PIXEL:
{
    int y = (int)forth_pop(vm);
    int x = (int)forth_pop(vm);
    if (g_pal.draw_pixel)
    {
        g_pal.draw_pixel(x, y);
    }
}
    FORTH_NEXT();

L_GR_RECT:
{
    int h = (int)forth_pop(vm);
    int w = (int)forth_pop(vm);
    int y = (int)forth_pop(vm);
    int x = (int)forth_pop(vm);
    if (g_pal.draw_rect)
    {
        g_pal.draw_rect(x, y, w, h);
    }
}
    FORTH_NEXT();

L_GR_CLS:
    if (g_pal.clear_gfx)
    {
        g_pal.clear_gfx();
    }
    vm->silent = true;
    FORTH_NEXT();
L_GR_FLUSH:
    if (g_pal.flush_gfx)
    {
        g_pal.flush_gfx();
    }
    FORTH_NEXT();

L_SILENT:
    vm->silent = true;
    FORTH_NEXT();
L_VERBOSE:
    vm->silent = false;
    FORTH_NEXT();

L_I:
    if (FORTH_LIKELY(vm->rp >= 0))
    {
        forth_push(vm, vm->return_stack[vm->rp]);
    }
    FORTH_NEXT();

L_DO:
    if (vm->state == 0)
    {
        g_pal.print_str(" Error: DO outside compile mode!\n");
    }
    else
    {
        static void *dop_label = &&RT_DO;
        forth_compile_cell(vm, (cell_t)dop_label);
        forth_push(vm, (cell_t)&vm->dict_buf[vm->here]);
        forth_compile_cell(vm, 0);
    }
    FORTH_NEXT();

RT_DO:
{
    cell_t start = forth_pop(vm);
    cell_t limit = forth_pop(vm);
    if (FORTH_UNLIKELY(vm->rp >= (int)STACK_CAPACITY - 2))
    {
        g_pal.print_str(" Error: Return stack overflow in DO!\n");
        return nullptr;
    }
    vm->return_stack[++vm->rp] = limit;
    vm->return_stack[++vm->rp] = start;
    ip++;
}
    FORTH_NEXT();

L_LOOP:
    if (vm->state == 0)
    {
        g_pal.print_str(" Error: LOOP outside compile mode!\n");
    }
    else
    {
        static void *loopp_label = &&RT_LOOP;
        forth_compile_cell(vm, (cell_t)loopp_label);
        cell_t patch_addr = forth_pop(vm);

        *(cell_t *)patch_addr = (cell_t)&vm->dict_buf[vm->here];
        forth_compile_cell(vm, patch_addr + sizeof(cell_t));

        forth_push(vm, (cell_t)&vm->dict_buf[vm->here]);
    }
    FORTH_NEXT();

RT_LOOP:
{
    cell_t index = ++vm->return_stack[vm->rp];
    cell_t limit = vm->return_stack[vm->rp - 1];
    if (index < limit)
    {
        ip = (void **)(*ip);
    }
    else
    {
        vm->rp -= 2;
        ip++;
    }
}
    FORTH_NEXT();

L_PLUS_LOOP:
    if (vm->state == 0)
    {
        g_pal.print_str(" Error: +LOOP outside compile mode!\n");
    }
    else
    {
        static void *plus_loopp_label = &&RT_PLUS_LOOP;
        forth_compile_cell(vm, (cell_t)plus_loopp_label);
        cell_t patch_addr = forth_pop(vm);
        *(cell_t *)patch_addr = (cell_t)&vm->dict_buf[vm->here];
        forth_compile_cell(vm, patch_addr + sizeof(cell_t));
    }
    FORTH_NEXT();

RT_PLUS_LOOP:
{
    cell_t step = forth_pop(vm);
    cell_t index = (vm->return_stack[vm->rp] += step);
    cell_t limit = vm->return_stack[vm->rp - 1];
    if (step > 0 ? (index < limit) : (index >= limit))
    {
        ip = (void **)(*ip);
    }
    else
    {
        vm->rp -= 2;
        ip++;
    }
}
    FORTH_NEXT();

L_UNLOOP:
    if (vm->rp >= 1)
    {
        vm->rp -= 2;
    }
    FORTH_NEXT();

L_GREAT_EQ:
{
    scell_t b = (scell_t)forth_pop(vm);
    scell_t a = (scell_t)forth_pop(vm);
    forth_push(vm, (a >= b) ? -1 : 0);
}
    FORTH_NEXT();
L_LESS_EQ:
{
    scell_t b = (scell_t)forth_pop(vm);
    scell_t a = (scell_t)forth_pop(vm);
    forth_push(vm, (a <= b) ? -1 : 0);
}
    FORTH_NEXT();

L_DEPTH:
    forth_push(vm, (cell_t)(vm->sp + 1));
    FORTH_NEXT();

L_TICK:
{
    char token[MAX_NAME_LENGTH + 1];
    if (forth_next_token(vm, token, sizeof(token)))
    {
        WordHeader *w = forth_find_word(vm, token);
        if (w)
        {
            if (vm->state == 0)
            {
                forth_push(vm, (cell_t)w);
            }
            else
            {
                forth_compile_cell(vm, (cell_t)g_lit_label);
                forth_compile_cell(vm, (cell_t)w);
            }
        }
        else
        {
            g_pal.print_str(" Error: ' unknown word: ");
            g_pal.print_str(token);
            g_pal.emit_char('\n');
        }
    }
}
    FORTH_NEXT();

L_EXECUTE:
{
    cell_t xt = forth_pop(vm);
    WordHeader *w = (WordHeader *)xt;
    if (w->code == g_docol_label)
    {
        void *thread[3] = {g_docol_label, (void *)(w + 1), g_exit_label};
        execute_threaded(vm, thread);
    }
    else if (w->code == g_dovar_label)
    {
        void *thread[3] = {g_dovar_label, (void *)(w + 1), g_exit_label};
        execute_threaded(vm, thread);
    }
    else
    {
        void *thread[2] = {w->code, g_exit_label};
        execute_threaded(vm, thread);
    }
}
    FORTH_NEXT();

L_STATE:
    forth_push(vm, vm->state);
    FORTH_NEXT();

L_LEAVE:
    if (vm->state == 0)
    {
        g_pal.print_str(" Error: LEAVE outside compile mode!\n");
    }
    else
    {
        static void *leave_runtime = &&RT_LEAVE;
        forth_compile_cell(vm, (cell_t)leave_runtime);
    }
    FORTH_NEXT();

RT_LEAVE:
    vm->return_stack[vm->rp] = vm->return_stack[vm->rp - 1];
    FORTH_NEXT();

L_ABS:
    if (FORTH_LIKELY(vm->sp >= 0))
    {
        scell_t val = (scell_t)vm->data_stack[vm->sp];
        if (val < 0)
        {
            vm->data_stack[vm->sp] = (cell_t)(-val);
        }
    }
    FORTH_NEXT();

L_NEGATE:
    if (FORTH_LIKELY(vm->sp >= 0))
    {
        vm->data_stack[vm->sp] = (cell_t)(-((scell_t)vm->data_stack[vm->sp]));
    }
    FORTH_NEXT();

L_MIN:
{
    cell_t b = forth_pop(vm);
    cell_t a = forth_pop(vm);
    forth_push(vm, ((scell_t)a < (scell_t)b) ? a : b);
}
    FORTH_NEXT();

L_MAX:
{
    cell_t b = forth_pop(vm);
    cell_t a = forth_pop(vm);
    forth_push(vm, ((scell_t)a > (scell_t)b) ? a : b);
}
    FORTH_NEXT();

#undef FORTH_NEXT
}

void forth_init(ForthEngine *vm)
{
    *vm = (ForthEngine){};
    vm->sp = -1;
    vm->rp = -1;
    vm->latest = nullptr;
    vm->state = 0;
    vm->silent = false;

    void **table = execute_threaded(vm, nullptr);
    g_lit_label = table[OP_LIT];
    g_exit_label = table[OP_EXIT];
    g_branch_label = table[OP_BRANCH];
    g_zbranch_label = table[OP_ZBRANCH];
    g_docol_label = table[OP_DOCOL];
    g_dovar_label = table[OP_DOVAR];
    g_print_str_label = table[OP_PRINT_STR];
    g_tick_label = table[OP_TICK];

    register_primitive(vm, "NOP", table[OP_NOP], 0);
    register_primitive(vm, "+", table[OP_ADD], 0);
    register_primitive(vm, "-", table[OP_SUB], 0);
    register_primitive(vm, "*", table[OP_MUL], 0);
    register_primitive(vm, "DUP", table[OP_DUP], 0);
    register_primitive(vm, "DROP", table[OP_DROP], 0);
    register_primitive(vm, "SWAP", table[OP_SWAP], 0);
    register_primitive(vm, ".", table[OP_DOT], 0);
    register_primitive(vm, ".S", table[OP_DOT_S], 0);
    register_primitive(vm, "EXIT", table[OP_EXIT], 0);
    register_primitive(vm, "OVER", table[OP_OVER], 0);
    register_primitive(vm, "ROT", table[OP_ROT], 0);
    register_primitive(vm, "?DUP", table[OP_QDUP], 0);
    register_primitive(vm, "NIP", table[OP_NIP], 0);
    register_primitive(vm, "TUCK", table[OP_TUCK], 0);
    register_primitive(vm, "/", table[OP_DIV], 0);
    register_primitive(vm, "MOD", table[OP_MOD], 0);
    register_primitive(vm, "/MOD", table[OP_DIVMOD], 0);
    register_primitive(vm, "1+", table[OP_INC], 0);
    register_primitive(vm, "1-", table[OP_DEC], 0);
    register_primitive(vm, "=", table[OP_EQUALS], 0);
    register_primitive(vm, "<", table[OP_LESS], 0);
    register_primitive(vm, ">", table[OP_GREATER], 0);
    register_primitive(vm, "0=", table[OP_ZEQUALS], 0);
    register_primitive(vm, "AND", table[OP_AND], 0);
    register_primitive(vm, "OR", table[OP_OR], 0);
    register_primitive(vm, "XOR", table[OP_XOR], 0);
    register_primitive(vm, "INVERT", table[OP_INVERT], 0);
    register_primitive(vm, "HERE", table[OP_HERE], 0);
    register_primitive(vm, "ALLOT", table[OP_ALLOT], 0);
    register_primitive(vm, "@", table[OP_FETCH], 0);
    register_primitive(vm, "!", table[OP_STORE], 0);
    register_primitive(vm, "C@", table[OP_CFETCH], 0);
    register_primitive(vm, "C!", table[OP_CSTORE], 0);
    register_primitive(vm, "CR", table[OP_CR], 0);
    register_primitive(vm, "EMIT", table[OP_EMIT], 0);
    register_primitive(vm, "CLS", table[OP_CLS], 0);
#ifdef __3DS__
    register_primitive(vm, "KEY-A?", table[OP_KEY_A], 0);
    register_primitive(vm, "TOUCH-COORDS", table[OP_TOUCH_COORDS], 0);
    register_primitive(vm, "WAIT-TOUCH", table[OP_WAIT_TOUCH], 0);
    register_primitive(vm, "TOUCH?", table[OP_TOUCH_Q], 0);
#endif
    register_primitive(vm, ":", table[OP_COLON], 0);
    register_primitive(vm, ";", table[OP_SEMICOLON], FLAG_IMMEDIATE);
    register_primitive(vm, "CREATE", table[OP_CREATE], 0);
    register_primitive(vm, "VARIABLE", table[OP_VARIABLE], 0);
    register_primitive(vm, ",", table[OP_COMMA], 0);
    register_primitive(vm, "IF", table[OP_IF], FLAG_IMMEDIATE);
    register_primitive(vm, "ELSE", table[OP_ELSE], FLAG_IMMEDIATE);
    register_primitive(vm, "THEN", table[OP_THEN], FLAG_IMMEDIATE);
    register_primitive(vm, "BEGIN", table[OP_BEGIN], FLAG_IMMEDIATE);
    register_primitive(vm, "UNTIL", table[OP_UNTIL], FLAG_IMMEDIATE);
    register_primitive(vm, "INCLUDE", table[OP_INCLUDE], 0);
    register_primitive(vm, "\\", table[OP_COMMENT], FLAG_IMMEDIATE);
    register_primitive(vm, "(", table[OP_PAREN], FLAG_IMMEDIATE);
    register_primitive(vm, ".\"", table[OP_DOT_QUOTE], FLAG_IMMEDIATE);
    register_primitive(vm, "GR-COLOR!", table[OP_GR_COLOR], 0);
    register_primitive(vm, "GR-PIXEL!", table[OP_GR_PIXEL], 0);
    register_primitive(vm, "GR-RECT!", table[OP_GR_RECT], 0);
    register_primitive(vm, "GR-CLS", table[OP_GR_CLS], 0);
    register_primitive(vm, "GR-FLUSH", table[OP_GR_FLUSH], 0);
    register_primitive(vm, "SILENT", table[OP_SILENT], 0);
    register_primitive(vm, "VERBOSE", table[OP_VERBOSE], 0);
    register_primitive(vm, "DO", table[OP_DO], FLAG_IMMEDIATE);
    register_primitive(vm, "LOOP", table[OP_LOOP], FLAG_IMMEDIATE);
    register_primitive(vm, "+LOOP", table[OP_PLUS_LOOP], FLAG_IMMEDIATE);
    register_primitive(vm, "I", table[OP_I], 0);
    register_primitive(vm, "UNLOOP", table[OP_UNLOOP], 0);
    register_primitive(vm, ">=", table[OP_GREAT_EQ], 0);
    register_primitive(vm, "<=", table[OP_LESS_EQ], 0);
    register_primitive(vm, "DEPTH", table[OP_DEPTH], 0);
    register_primitive(vm, "'", table[OP_TICK], FLAG_IMMEDIATE);
    register_primitive(vm, "EXECUTE", table[OP_EXECUTE], 0);
    register_primitive(vm, "STATE", table[OP_STATE], 0);
    register_primitive(vm, "LEAVE", table[OP_LEAVE], FLAG_IMMEDIATE);
    register_primitive(vm, "ABS", table[OP_ABS], 0);
    register_primitive(vm, "NEGATE", table[OP_NEGATE], 0);
    register_primitive(vm, "MIN", table[OP_MIN], 0);
    register_primitive(vm, "MAX", table[OP_MAX], 0);
}

void forth_eval(ForthEngine *vm, const char *input)
{
    const char *saved_source = vm->source_ptr;
    vm->source_ptr = input;

    if (input)
    {
        strncpy(vm->last_command, input, sizeof(vm->last_command) - 1);
        vm->last_command[sizeof(vm->last_command) - 1] = '\0';
        for (int i = 0; vm->last_command[i] != '\0'; i++)
        {
            if (vm->last_command[i] == '\n' || vm->last_command[i] == '\r')
            {
                vm->last_command[i] = '\0';
                break;
            }
        }
    }

    char token[MAX_NAME_LENGTH + 1];

    while (forth_next_token(vm, token, sizeof(token)))
    {
        WordHeader *curr = forth_find_word(vm, token);
        if (curr != nullptr)
        {
            if (vm->state == 0 || (curr->flags & FLAG_IMMEDIATE))
            {
                if (curr->code == g_docol_label)
                {
                    void *thread[3] = {g_docol_label, (void *)(curr + 1), g_exit_label};
                    execute_threaded(vm, thread);
                }
                else if (curr->code == g_dovar_label)
                {
                    void *thread[3] = {g_dovar_label, (void *)(curr + 1), g_exit_label};
                    execute_threaded(vm, thread);
                }
                else
                {
                    void *thread[2] = {curr->code, g_exit_label};
                    execute_threaded(vm, thread);
                }
            }
            else
            {
                if (curr->code == g_docol_label || curr->code == g_dovar_label)
                {
                    forth_compile_cell(vm, (cell_t)curr->code);
                    forth_compile_cell(vm, (cell_t)(curr + 1));
                }
                else
                {
                    forth_compile_cell(vm, (cell_t)curr->code);
                }
            }
        }
        else
        {
            char *endptr;
            long val = strtol(token, &endptr, 10);
            if ('\0' == *endptr)
            {
                if (vm->state == 0)
                {
                    forth_push(vm, (cell_t)val);
                }
                else
                {
                    forth_compile_cell(vm, (cell_t)g_lit_label);
                    forth_compile_cell(vm, (cell_t)val);
                }
            }
            else
            {
                g_pal.print_str("Unknown word: ");
                g_pal.print_str(token);
                g_pal.print_str("\n");
                vm->source_ptr = saved_source;
                return;
            }
        }
    }
    vm->source_ptr = saved_source;
}
