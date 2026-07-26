// I am bored, so we are going to write a B compiler in C!
// For some info, the B compiler heavily influenced C, and was designed by Ken Thompson.
// It was written originally in BCPL (not A, I am sorry for that).
// And... Yes. Well, after this I will surely rewrite this in Zen C.

// Also, it might not be exhaustive for now. And once I am ready I might make my own dialect.
// So, don't trust this as if you would trust the reference manual
// (https://www.thinkage.ca/gcos/expl/b/manu/manu.html)

// NOTE: I've used a manual for the Honeywell 6000 series, not for the PDP-7 (yes, RTFM moment).
//       It is not really an issue, because after all this is a port to x86-64 Linux. The thing
//       is that this is also a super-set of B. I haven't implemented all extra features as of
//       the moment I am writing this comment, but I have implemented all original B features.
//       I think at least.

#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdnoreturn.h>

bool g_use_color = false;

#define RED_COLOR (g_use_color ? "\033[31m" : "")
#define RESET_COLOR (g_use_color ? "\033[0m" : "")

void color_init()
{
    g_use_color = isatty(STDOUT_FILENO) && NULL == getenv("NO_COLOR");
}

typedef enum
{
    ERR_LEXICAL,
    ERR_SYNTAX,
    ERR_SEMANTIC,
    ERR_FATAL
} error_type_t;

noreturn void compile_error(error_type_t err_type, const char *format, ...);
void emit(const char *format, ...);
void emit_label(const char *format, ...);

// Lexer moment: because we don't eat raw, we like our tokens well done.

#define LEXEME_LENGTH 256 // Increased to handle large assembly strings.
#define WORD_LENGTH 8     // To be loyal to our PDP-7.

int current_line = 1;

typedef struct
{
    char name[LEXEME_LENGTH];
    char text[LEXEME_LENGTH];
} manifest_t;

manifest_t manifests[100];
int manifest_count = 0;

const char *source_stack[10];
int source_stack_ptr = 0;

int label_counter = 0; // We need global labels for jumps.
int string_counter = 0;
int current_break_label = -1;
int current_next_label = -1;

char global_vectors[100][LEXEME_LENGTH];
int global_vector_count = 0;

const char *abi_regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

typedef enum
{
    TOK_AUTO, // In B we only have one datatype (because PDP-7), or better said,
              // everything is a WORD (idk how long originally), 'auto'.
              // Don't confuse it with C++ and C23 'auto'.
              //
              // In old C, we also had 'auto', which was used to
              // say "hey, this variable lives in the stack" kinda
              // but that's what happens by default in C, so... yes.

    TOK_ID,    // Variables.
    TOK_NUM,   // Numbers.
    TOK_CHAR,  // Char literals.
    TOK_IF,    // if statements.
    TOK_ELSE,  // In B we have 'else' but not 'else if'.
    TOK_WHILE, // while statements.
    TOK_FOR,
    TOK_REPEAT, // Like 'while (1)'.
    TOK_SWITCH,
    TOK_CASE,
    TOK_DEFAULT,
    TOK_BREAK,
    TOK_NEXT,          // In C this is known as 'continue'.
    TOK_COLON,         // ':'.
    TOK_ASSIGN,        // '='.
    TOK_PLUS,          // '+'.
    TOK_MINUS,         // '-'.
    TOK_STAR,          // '*'.
    TOK_SLASH,         // '/'.
    TOK_MOD,           // '%'.
    TOK_AMPERSAND,     // '&'.
    TOK_OR,            // '|'.
    TOK_XOR,           // '^'.
    TOK_NOT,           // '!'.
    TOK_LSHIFT,        // '<<'.
    TOK_RSHIFT,        // '>>'.
    TOK_ADD_ASSIGN,    // '+='.
    TOK_SUB_ASSIGN,    // '-='.
    TOK_MUL_ASSIGN,    // '*='.
    TOK_DIV_ASSIGN,    // '/='.
    TOK_MOD_ASSIGN,    // '%='.
    TOK_AND_ASSIGN,    // '&='.
    TOK_OR_ASSIGN,     // '|='.
    TOK_XOR_ASSIGN,    // '^='.
    TOK_LSHIFT_ASSIGN, // '<<='.
    TOK_RSHIFT_ASSIGN, // '>>='.
    TOK_INC,           // '++'.
    TOK_DEC,           // '--'.
    TOK_EQ,            // "==".
    TOK_NEQ,           // "!=".
    TOK_LT,            // '<'.
    TOK_GT,            // '>'.
    TOK_LTE,           // '<='.
    TOK_GTE,           // '>='.
    TOK_LOGICAL_AND,   // '&&'.
    TOK_LOGICAL_OR,    // '||'.
    TOK_COMPLEMENT,    // '~'.
    TOK_SEMI,          // ';'.
    TOK_LBRACE,        // '{'.
    TOK_RBRACE,        // '}'.
    TOK_LPAREN,        // '('.
    TOK_RPAREN,        // ')'.
    TOK_LBRACK,        // '['.
    TOK_RBRACK,        // ']'.
    TOK_RETURN,        // 'return'.
    TOK_GOTO,          // 'goto'.
    TOK_COMMA,         // ','.
    TOK_QUESTION,      // '?'.
    TOK_EXTRN,         // 'extrn'.
    TOK_STRING,        // "Hello, World!\n".
    TOK_EOF,           // End of File.
    TOK_UNKNOWN        // Whatever else we find.
} token_type_t;

typedef struct
{
    token_type_t type;
    char lexeme[LEXEME_LENGTH]; // The actual content.
} token_t;

const char *source_code; // The pointer we use to track where we are.

token_t make_token(token_type_t type, const char *lexeme)
{
    token_t t;
    t.type = type;
    strncpy(t.lexeme, lexeme, LEXEME_LENGTH - 1);
    t.lexeme[LEXEME_LENGTH - 1] = '\0';
    return t;
}

token_t get_next_token()
{
    // First we deal with spaces. We just ignore them.
    // I think some languages do not ignore them... Imagine that chaos.
    while (true)
    {
        if ('\0' == *source_code)
        {
            if (source_stack_ptr > 0)
            {
                source_code = source_stack[--source_stack_ptr];
                continue;
            }
            else
            {
                break; // Real EOF.
            }
        }
        else if ('\n' == *source_code)
        {
            current_line++;
            source_code++;
        }
        else if (isspace(*source_code))
        {
            source_code++;
        }
        else if ('/' == source_code[0] && '*' == source_code[1])
        {
            source_code += 2;

            while (*source_code != '\0' && !('*' == source_code[0] && '/' == source_code[1]))
            {
                source_code++;
            }

            if (*source_code != '\0')
            {
                source_code += 2;
            }
        }
        else
        {
            break;
        }
    }

    switch (*source_code)
    {
    case '\0':
        return make_token(TOK_EOF, "EOF");
    case ':':
        source_code++;
        return make_token(TOK_COLON, ":");
    case '=':
        source_code++;
        if ('=' == *source_code)
        {
            source_code++;
            return make_token(TOK_EQ, "==");
        }
        return make_token(TOK_ASSIGN, "=");
    case '+':
        source_code++;
        if ('=' == *source_code)
        {
            source_code++;
            return make_token(TOK_ADD_ASSIGN, "+=");
        }
        if ('+' == *source_code)
        {
            source_code++;
            return make_token(TOK_INC, "++");
        }
        return make_token(TOK_PLUS, "+");
    case '-':
        source_code++;
        if ('=' == *source_code)
        {
            source_code++;
            return make_token(TOK_SUB_ASSIGN, "-=");
        }
        if ('-' == *source_code)
        {
            source_code++;
            return make_token(TOK_DEC, "--");
        }
        return make_token(TOK_MINUS, "-");
    case '*':
        source_code++;
        if ('=' == *source_code)
        {
            source_code++;
            return make_token(TOK_MUL_ASSIGN, "*=");
        }
        return make_token(TOK_STAR, "*");
    case '/':
        source_code++;
        if ('=' == *source_code)
        {
            source_code++;
            return make_token(TOK_DIV_ASSIGN, "/=");
        }
        return make_token(TOK_SLASH, "/");
    case '%':
        source_code++;
        if ('=' == *source_code)
        {
            source_code++;
            return make_token(TOK_MOD_ASSIGN, "%=");
        }
        return make_token(TOK_MOD, "%");
    case '&':
        source_code++;
        if ('=' == *source_code)
        {
            source_code++;
            return make_token(TOK_AND_ASSIGN, "&=");
        }
        if ('&' == *source_code)
        {
            source_code++;
            return make_token(TOK_LOGICAL_AND, "&&");
        }
        return make_token(TOK_AMPERSAND, "&");
    case '|':
        source_code++;
        if ('=' == *source_code)
        {
            source_code++;
            return make_token(TOK_OR_ASSIGN, "|=");
        }
        if ('|' == *source_code)
        {
            source_code++;
            return make_token(TOK_LOGICAL_OR, "||");
        }
        return make_token(TOK_OR, "|");
    case '~':
        source_code++;
        return make_token(TOK_COMPLEMENT, "~");
    case '^':
        source_code++;
        if ('=' == *source_code)
        {
            source_code++;
            return make_token(TOK_XOR_ASSIGN, "^=");
        }
        return make_token(TOK_XOR, "^");
    case '!':
        source_code++;
        if ('=' == *source_code)
        {
            source_code++;
            return make_token(TOK_NEQ, "!=");
        }
        return make_token(TOK_NOT, "!");
    case '<':
        source_code++;
        if ('=' == *source_code)
        {
            source_code++;
            return make_token(TOK_LTE, "<=");
        }
        if ('<' == *source_code)
        {
            source_code++;
            return make_token(TOK_LSHIFT, "<<");
        }
        return make_token(TOK_LT, "<");
    case '>':
        source_code++;
        if ('=' == *source_code)
        {
            source_code++;
            return make_token(TOK_GTE, ">=");
        }
        if ('>' == *source_code)
        {
            source_code++;
            return make_token(TOK_RSHIFT, ">>");
        }
        return make_token(TOK_GT, ">");
    case ';':
        source_code++;
        return make_token(TOK_SEMI, ";");
    case '{':
        source_code++;
        return make_token(TOK_LBRACE, "{");
    case '}':
        source_code++;
        return make_token(TOK_RBRACE, "}");
    case '(':
        source_code++;
        return make_token(TOK_LPAREN, "(");
    case ')':
        source_code++;
        return make_token(TOK_RPAREN, ")");
    case '[':
        source_code++;
        return make_token(TOK_LBRACK, "[");
    case ']':
        source_code++;
        return make_token(TOK_RBRACK, "]");
    case ',':
        source_code++;
        return make_token(TOK_COMMA, ",");
    case '?':
        source_code++;
        return make_token(TOK_QUESTION, "?");

    // This is for character literals.
    case '\'':
    {
        source_code++;
        long long char_value = 0;
        int char_count = 0;

        while (*source_code != '\'' && *source_code != '\0')
        {

            if (char_count >= 8)
            {
                compile_error(ERR_LEXICAL, "Character constant too long (max 8 characters)");
            }

            char c = *source_code++;

            if ('*' == c)
            {
                char esc = *source_code++;
                switch (esc)
                {
                case '(':
                    c = '{';
                    break;
                case ')':
                    c = '}';
                    break;
                case '<':
                    c = '[';
                    break;
                case '>':
                    c = ']';
                    break;
                case '\'':
                    c = '\'';
                    break;
                case '"':
                    c = '"';
                    break;
                case 'n':
                    c = 10; // Newline.
                    break;
                case 't':
                    c = 9; // Horizontal tab.
                    break;
                case 'v':
                    c = 11; // Vertical tab.
                    break;
                case 'e':
                    c = 0; // Null.
                    break;
                case 'b':
                    c = 8; // Backspace.
                    break;
                case 'r':
                    c = 13; // Carriage return.
                    break;
                case 'f':
                    c = 12; // ASCII form feed.
                    break;
                case 'x':
                    c = 127; // I just learnt that the delete character is represented as 127 in
                             // ASCII because in paper tape to remove errors they just punch all 7
                             // holes.
                    break;
                // TODO: add the rest ('*nnn'):
                // https://www.thinkage.ca/gcos/expl/b/manu/manu.html#AppendixA
                default:
                    c = esc;
                }
            }

            char_value = (char_value << 8) | (unsigned char)c;
            char_count++;
        }

        if ('\'' == *source_code)
        {
            source_code++;
        }

        char buffer[LEXEME_LENGTH];
        sprintf(buffer, "%lld", char_value);
        return make_token(TOK_CHAR, buffer);
    }

    case '"':
    {
        source_code++;
        char buffer[LEXEME_LENGTH];
        int i = 0;

        while (*source_code != '\0' && *source_code != '"' && i < LEXEME_LENGTH - 1)
        {
            if ('*' == *source_code)
            {
                // So... Nowadays you would do '\n' for a newline.
                // The problem back then was that many keyboards
                // didn't have the backslash key yet.
                //
                // It is also important to note that the standard escape
                // sequences didn't even exist back then. So it was a bit
                // of a chaos.
                //
                // The solution they implemented was using '*' for
                // escape codes. So, let's go that way!
                source_code++;

                // TODO: this will be a bit different than the other one
                // I gotta adjust the B escape sequences to C ones.
                // B escapes: https://www.thinkage.ca/gcos/expl/b/manu/manu.html#AppendixA
                // C escapes: Check the ISO/IEC 9899:1999, aka C99.
                // NOTE: not all C escapes are directly connected to the B escapes.
                switch (*source_code)
                {
                case 'n':
                    buffer[i++] = 10;
                    break;
                case 't':
                    buffer[i++] = 9;
                    break;
                case 'v':
                    buffer[i++] = 11;
                    break;
                case 'f':
                    buffer[i++] = 12;
                    break;
                // In B this is defined as 'end of string (ASCII NUL = 000)'.
                // In C this escape doesn't seem to be defined.
                // So we just use the classic null terminator.
                case 'e':
                    buffer[i++] = 0;
                    break;
                case '"':
                    buffer[i++] = '\\';
                    buffer[i++] = '"';
                    break;
                case '*':
                    buffer[i++] = '*';
                    break;
                default:
                    buffer[i++] = *source_code; // Just copy whatever.
                }
                source_code++;
            }
            else
            {
                buffer[i++] = *source_code++;
            }
        }
        if ('"' == *source_code)
        {
            source_code++;
        }

        buffer[i] = '\0';
        return make_token(TOK_STRING, buffer);
    }

    // Source: https://www.thinkage.ca/gcos/expl/b/manu/manu.html#AppendixA_2
    case '$':
        source_code++;
        switch (*source_code)
        {
        case '(':
            source_code++;
            return make_token(TOK_LBRACE, "{");
        case ')':
            source_code++;
            return make_token(TOK_RBRACE, "}");
        case '<':
            source_code++;
            return make_token(TOK_LBRACK, "[");
        case '>':
            source_code++;
            return make_token(TOK_RBRACK, "]");
        case '+':
            source_code++;
            return make_token(TOK_OR, "|");
        case '-':
            source_code++;
            return make_token(TOK_XOR, "^");
        case 'a':
            source_code++;
            return make_token(TOK_STAR, "@");
        case '\'':
        {
            source_code++;
            unsigned long long bcd_value = 0;
            int char_count = 0;

            while (*source_code != '\'' && *source_code != '\0')
            {
                if (char_count >= 10)
                {
                    compile_error(ERR_LEXICAL, "BCD constant too long (max 10 characters)");
                }

                char c = toupper(*source_code++);
                unsigned char bcd_char = c & 0x3F;
                bcd_value = (bcd_value << 6) | bcd_char;
                char_count++;
            }

            if ('\'' == *source_code)
            {
                source_code++;
            }
            else
            {
                compile_error(ERR_LEXICAL, "Unterminated BCD constant");
            }

            char buffer[LEXEME_LENGTH];
            sprintf(buffer, "%llu", bcd_value);
            return make_token(TOK_NUM, buffer);
        }
        default:
            break;
        }
        break;
    // Same same but different.
    case '`':
    {
        source_code++;
        unsigned long long bcd_value = 0;
        int char_count = 0;

        while (*source_code != '`' && *source_code != '\0')
        {
            if (char_count >= 10)
            {
                compile_error(ERR_LEXICAL, "BCD constant too long (max 10 characters)");
            }

            // WE HAVE 6 BITS ONLY. WHAT IS LOWERCASE?
            char c = toupper(*source_code++);
            unsigned char bcd_char = c & 0x3F;
            bcd_value = (bcd_value << 6) | bcd_char;
            char_count++;
        }

        if ('`' == *source_code)
        {
            source_code++;
        }
        else
        {
            compile_error(ERR_LEXICAL, "Unterminated BCD constant");
        }

        char buffer[LEXEME_LENGTH];
        sprintf(buffer, "%llu", bcd_value);
        return make_token(TOK_NUM, buffer);
    }
    }

    // Now it's the turn for numbers.
    if (isdigit(*source_code))
    {
        char buffer[LEXEME_LENGTH];
        int i = 0;
        int is_float = 0;

        while ((isdigit(*source_code) || '.' == *source_code || 'e' == *source_code ||
                'E' == *source_code) &&
               i < LEXEME_LENGTH - 1)
        {
            if ('.' == *source_code || 'e' == *source_code || 'E' == *source_code)
            {
                is_float = 1;
            }

            buffer[i++] = *source_code++;

            if (is_float && ('e' == buffer[i - 1] || 'E' == buffer[i - 1]))
            {
                if ('+' == *source_code || '-' == *source_code)
                {
                    buffer[i++] = *source_code++;
                }
            }
        }
        buffer[i] = '\0';

        if (is_float)
        {
            // Parse as a double then bit-cast.
            double float_val = strtod(buffer, NULL);
            unsigned long long bits;
            memcpy(&bits, &float_val, sizeof(double));

            sprintf(buffer, "%llu", bits);
            return make_token(TOK_NUM, buffer);
        }
        else
        {
            // It's a standard integer.
            long long value = 0;
            int base = 10;
            int j = 0;

            if ('0' == buffer[0])
            {
                base = 8;
            }

            while (buffer[j] != '\0')
            {
                int digit = buffer[j] - '0';

                if (8 == base && digit >= 8)
                {
                    compile_error(ERR_LEXICAL, "Invalid digit '%c' in octal constant", buffer[j]);
                }

                value = (value * base) + digit;
                j++;
            }

            sprintf(buffer, "%lld", value);
            return make_token(TOK_NUM, buffer);
        }
    }

    if (isalpha(*source_code) || '_' == *source_code)
    {
        char buffer[LEXEME_LENGTH];
        int i = 0;
        while ((isalnum(*source_code) || '_' == *source_code) && i < LEXEME_LENGTH - 1)
        {
            buffer[i++] = *source_code++;
        }
        buffer[i] = '\0';

        if (0 == strcmp(buffer, "auto"))
        {
            return make_token(TOK_AUTO, "auto");
        }
        if (0 == strcmp(buffer, "if"))
        {
            return make_token(TOK_IF, "if");
        }
        if (0 == strcmp(buffer, "else"))
        {
            return make_token(TOK_ELSE, "else");
        }
        if (0 == strcmp(buffer, "while"))
        {
            return make_token(TOK_WHILE, "while");
        }
        if (0 == strcmp(buffer, "for"))
        {
            return make_token(TOK_FOR, "for");
        }
        if (0 == strcmp(buffer, "repeat"))
        {
            return make_token(TOK_REPEAT, "repeat");
        }
        if (0 == strcmp(buffer, "switch"))
        {
            return make_token(TOK_SWITCH, "switch");
        }
        if (0 == strcmp(buffer, "case"))
        {
            return make_token(TOK_CASE, "case");
        }
        if (0 == strcmp(buffer, "default"))
        {
            return make_token(TOK_DEFAULT, "default");
        }
        if (0 == strcmp(buffer, "break"))
        {
            return make_token(TOK_BREAK, "break");
        }
        if (0 == strcmp(buffer, "next"))
        {
            return make_token(TOK_NEXT, "next");
        }
        if (0 == strcmp(buffer, "return"))
        {
            return make_token(TOK_RETURN, "return");
        }
        if (0 == strcmp(buffer, "goto"))
        {
            return make_token(TOK_GOTO, "goto");
        }
        if (0 == strcmp(buffer, "extrn"))
        {
            return make_token(TOK_EXTRN, "extrn");
        }

        for (int m = 0; m < manifest_count; m++)
        {
            if (0 == strcmp(buffer, manifests[m].name))
            {
                if (source_stack_ptr >= 10)
                {
                    compile_error(ERR_LEXICAL, "Manifest nesting exceeds 10 levels");
                }
                // Push current position, swap to manifest text, and recurse...
                source_stack[source_stack_ptr++] = source_code;
                source_code = manifests[m].text;
                return get_next_token();
            }
        }

        return make_token(TOK_ID, buffer);
    }

    // If you are here, I am sorry.
    char bad_char[2] = {*source_code, '\0'};
    source_code++;
    return make_token(TOK_UNKNOWN, bad_char);
}

// Parser moment: time for AST and recursion (yippee).

typedef enum
{
    AST_PROGRAM,         // The root of the whole program.
    AST_VAR_DECL,        // Like, 'auto x;'.
    AST_ASSIGN,          // Like, 'x = ...'.
    AST_COMPOUND_ASSIGN, // 'x += ...'.
    AST_BINOP,           // Math operations.
    AST_NUM,             // Numbers.
    AST_ID,              // Variables.
    AST_IF,              // 'if (x < 10) { ... }'.
    AST_WHILE,           // 'while (x > 2) { ... }'.
    AST_FOR,             // You get the idea...
    AST_REPEAT,
    AST_SWITCH,
    AST_CASE,
    AST_DEFAULT,
    AST_BREAK,
    AST_NEXT,
    AST_CMP,       // 'x < 10'.
    AST_BLOCK,     // '{ ... }'.
    AST_EXPR_STMT, // Wrapper to clean up the stack after standalone expressions.
    AST_ADDR,      // '&x'.
    AST_DEREF,     // '*p'.
    AST_PRE_INC,   // '++i'.
    AST_POST_INC,  // 'i++'.
    AST_PRE_DEC,   // '--i'.
    AST_POST_DEC,  // 'i--'.
    AST_LOGICAL_AND,
    AST_LOGICAL_OR,
    AST_FUNC_DECL,  // Defining a function.
    AST_FUNC_CALL,  // Calling a function.
    AST_RETURN,     // Returning a value.
    AST_GLOBAL_DEF, // 'x { 5 };' or 'x;' for example.
    AST_EXTRN,      // 'extern x, y;'.
    AST_STRING,
    AST_NEG,        // '-x'.
    AST_NOT,        // '!x'.
    AST_COMPLEMENT, // '~x'.
    AST_GOTO,
    AST_LABEL,
    AST_TERNARY,
    AST_UNKNOWN // Whatever.
} AST_node_type_t;

typedef struct AST_node_t
{
    AST_node_type_t type;

    // For AST_NUM and AST_ID...
    char lexeme[LEXEME_LENGTH];

    // For AST_BINOP.
    char op;

    int offset;

    struct AST_node_t *left;
    struct AST_node_t *middle; // For the 'for' loops (condition).
    struct AST_node_t *right;
    struct AST_node_t *extra; // For the 'for' loops (body);
                              // For the 'if' (else body).
} AST_node_t;

AST_node_t *create_node(AST_node_type_t type)
{
    AST_node_t *node = malloc(sizeof(AST_node_t));
    node->type = type;
    memset(node->lexeme, 0, LEXEME_LENGTH);
    node->op = 0;
    node->offset = 0;
    node->left = NULL;
    node->middle = NULL;
    node->right = NULL;
    node->extra = NULL;
    return node;
}

token_t current_token;

void advance()
{
    current_token = get_next_token();
}

noreturn void compile_error(error_type_t err_type, const char *format, ...)
{
    const char *type_str = "Unknown Error";

    switch (err_type)
    {
    case ERR_LEXICAL:
        type_str = "Lexical Error";
        break;
    case ERR_SYNTAX:
        type_str = "Syntax Error";
        break;
    case ERR_SEMANTIC:
        type_str = "Semantic Error";
        break;
    case ERR_FATAL:
        type_str = "Fatal Error";
        break;
    }

    fprintf(stderr, "\n%s[%s | Line %d]%s ", RED_COLOR, type_str, current_line, RESET_COLOR);

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

// We check if the current token matches... and if so we move forward.
void eat(token_type_t type)
{
    if (current_token.type == type)
    {
        advance();
    }
    else
    {
        compile_error(ERR_SYNTAX, "Expected token type %d, but got '%s'", type,
                      current_token.lexeme);
    }
}

// Source: https://www.thinkage.ca/gcos/expl/b/manu/manu.html#AppendixB
typedef enum
{
    PREC_NONE = 0,
    PREC_ASSIGNMENT,     // = += -= etc. [RL]
    PREC_TERNARY,        // ?: [RL]
    PREC_LOGICAL_OR,     // || [LR]
    PREC_LOGICAL_AND,    // && [LR]
    PREC_RELATIONAL,     // == != > < <= >= [LR]
    PREC_ADDITIVE,       // + - [LR]
    PREC_MULTIPLICATIVE, // * / % [LR]
    PREC_BITWISE_OR,     // | [LR]
    PREC_BITWISE_XOR,    // ^ [LR]
    PREC_BITWISE_AND,    // & [LR]
    PREC_SHIFT,          // >> << [LR]
    PREC_UNARY,          // ++ -- * & - ! ~ [RL]
    PREC_CALL,           // arr[expr] func(args) postfix ++/-- [LR]
    PREC_PRIMARY         // name const (expr)
} precedence_t;

static int get_infix_precedence(token_type_t type)
{
    switch (type)
    {
    // 1 - Assignment [RL]
    case TOK_ASSIGN:
    case TOK_ADD_ASSIGN:
    case TOK_SUB_ASSIGN:
    case TOK_MUL_ASSIGN:
    case TOK_DIV_ASSIGN:
    case TOK_MOD_ASSIGN:
    case TOK_AND_ASSIGN:
    case TOK_OR_ASSIGN:
    case TOK_XOR_ASSIGN:
    case TOK_LSHIFT_ASSIGN:
    case TOK_RSHIFT_ASSIGN:
        return PREC_ASSIGNMENT;

    // 2 - Ternary [RL]
    case TOK_QUESTION:
        return PREC_TERNARY;

    // 3 - Logical OR [LR]
    case TOK_LOGICAL_OR:
        return PREC_LOGICAL_OR;

    // 4 - Logical AND [LR]
    case TOK_LOGICAL_AND:
        return PREC_LOGICAL_AND;

    // 5 - Relational & Equality [LR]
    case TOK_EQ:
    case TOK_NEQ:
    case TOK_LT:
    case TOK_GT:
    case TOK_LTE:
    case TOK_GTE:
        return PREC_RELATIONAL;

    // 6 - Additive [LR]
    case TOK_PLUS:
    case TOK_MINUS:
        return PREC_ADDITIVE;

    // 7 - Multiplicative [LR]
    case TOK_STAR:
    case TOK_SLASH:
    case TOK_MOD:
        return PREC_MULTIPLICATIVE;

    // 8 - Bitwise OR [LR]
    case TOK_OR:
        return PREC_BITWISE_OR;

    // 9 - Bitwise XOR [LR]
    case TOK_XOR:
        return PREC_BITWISE_XOR;

    // 10. Bitwise AND [LR]
    case TOK_AMPERSAND:
        return PREC_BITWISE_AND;

    // 11 - Shift [LR]
    case TOK_LSHIFT:
    case TOK_RSHIFT:
        return PREC_SHIFT;

    // 12 - Postfix / Calls / Subscripts [LR]
    case TOK_LPAREN: // func()
    case TOK_LBRACK: // arr[]
    case TOK_INC:    // x++
    case TOK_DEC:    // x--
        return PREC_CALL;

    default:
        return PREC_NONE;
    }
}

// This is for our Pratt parser (for operator precendence).
AST_node_t *parse_expr(int precedence);
AST_node_t *parse_prefix(void);
AST_node_t *parse_infix(AST_node_t *left);

AST_node_t *parse_expr(int precedence)
{
    // Every expression starts with a prefix like a number, variable, or unary op.
    AST_node_t *left = parse_prefix();

    // If it's an infix operator with a higher binding power then we let
    // it pull the left node into itself...
    while (precedence < get_infix_precedence(current_token.type))
    {
        left = parse_infix(left);
    }

    return left;
}

AST_node_t *parse_prefix(void)
{
    AST_node_t *node = create_node(AST_UNKNOWN);

    switch (current_token.type)
    {
    // Literals and variables.
    case TOK_ID:
        node->type = AST_ID;
        strcpy(node->lexeme, current_token.lexeme);
        advance();
        return node;

    case TOK_NUM:
    case TOK_CHAR:
        node->type = AST_NUM;
        strcpy(node->lexeme, current_token.lexeme);
        advance();
        return node;

    case TOK_STRING:
        node->type = AST_STRING;
        strcpy(node->lexeme, current_token.lexeme);
        advance();
        return node;

    // Grouping.
    case TOK_LPAREN:
        advance();
        free(node);                   // The node from above is not necessary for this case.
        node = parse_expr(PREC_NONE); // Always start from lowest precendence.
        eat(TOK_RPAREN);
        return node;

    // Prefix unary operators.
    case TOK_INC:
        advance();
        node->type = AST_PRE_INC;
        node->left = parse_expr(PREC_UNARY);
        return node;

    case TOK_DEC:
        advance();
        node->type = AST_PRE_DEC;
        node->left = parse_expr(PREC_UNARY);
        return node;

    case TOK_AMPERSAND:
        advance();
        node->type = AST_ADDR;
        if (TOK_ID == current_token.type)
        {
            strcpy(node->lexeme, current_token.lexeme);
            eat(TOK_ID);
            return node;
        }
        else
        {
            compile_error(ERR_SYNTAX, "'&' must be followed by a variable");
        }

    case TOK_STAR:
        advance();
        node->type = AST_DEREF;
        node->left = parse_expr(PREC_UNARY);
        return node;

    case TOK_MINUS:
        advance();
        node->type = AST_NEG;
        node->left = parse_expr(PREC_UNARY);
        return node;

    case TOK_NOT:
        advance();
        node->type = AST_NOT;
        node->left = parse_expr(PREC_UNARY);
        return node;

    case TOK_COMPLEMENT:
        advance();
        node->type = AST_COMPLEMENT;
        node->left = parse_expr(PREC_UNARY);
        return node;

    default:
        compile_error(ERR_SYNTAX, "Expected expression prefix, but got '%s'", current_token.lexeme);
    }
}

AST_node_t *parse_infix(AST_node_t *left)
{
    // Saving before advancing.
    token_t op_token = current_token;
    int precedence = get_infix_precedence(op_token.type);

    advance();

    // Postfix operators.
    if (TOK_INC == op_token.type || TOK_DEC == op_token.type)
    {
        AST_node_t *node = create_node(TOK_INC == op_token.type ? AST_POST_INC : AST_POST_DEC);
        node->left = left;
        return node;
    }

    if (TOK_LPAREN == op_token.type)
    {
        // Function call.
        AST_node_t *func_call = create_node(AST_FUNC_CALL);

        if (AST_ID == left->type)
        {
            strcpy(func_call->lexeme, left->lexeme);
            free(left); // We transfer the name to the call node itself...
        }
        else
        {
            compile_error(ERR_SEMANTIC, "Cannot call non-identifier");
        }

        AST_node_t *arg_head = NULL;
        AST_node_t *current_arg = NULL;

        while (TOK_RPAREN != current_token.type)
        {
            AST_node_t *new_arg = create_node(AST_UNKNOWN);
            new_arg->left = parse_expr(PREC_ASSIGNMENT);

            if (!arg_head)
            {
                arg_head = new_arg;
            }
            if (current_arg)
            {
                current_arg->right = new_arg;
            }
            current_arg = new_arg;

            if (TOK_COMMA == current_token.type)
            {
                advance();
            }
        }
        eat(TOK_RPAREN);
        func_call->left = arg_head;
        return func_call;
    }

    if (TOK_LBRACK == op_token.type)
    {
        // Array subscript: arr[index] -> *(arr + index)
        AST_node_t *index = parse_expr(PREC_NONE);
        eat(TOK_RBRACK);

        AST_node_t *add_node = create_node(AST_BINOP);
        add_node->op = '+';
        add_node->left = left;
        add_node->right = index;

        AST_node_t *deref_node = create_node(AST_DEREF);
        deref_node->left = add_node;
        return deref_node;
    }

    // Ternary operator ?: (right-associative)
    if (TOK_QUESTION == op_token.type)
    {
        AST_node_t *node = create_node(AST_TERNARY);
        node->left = left;                    // Condition.
        node->middle = parse_expr(PREC_NONE); // True branch.
        eat(TOK_COLON);
        node->right = parse_expr(precedence - 1); // False branch.
        return node;
    }

    // Assignments (right-associative)
    if (op_token.type == TOK_ASSIGN || (op_token.type >= TOK_ADD_ASSIGN && op_token.type <= TOK_RSHIFT_ASSIGN))
    {
        AST_node_t *node;

        if (TOK_ASSIGN == op_token.type)
        {
            node = create_node(AST_ASSIGN);
        }
        else
        {
            node = create_node(AST_COMPOUND_ASSIGN);
            switch (op_token.type)
            {
            case TOK_ADD_ASSIGN:
                node->op = '+';
                break;
            case TOK_SUB_ASSIGN:
                node->op = '-';
                break;
            case TOK_MUL_ASSIGN:
                node->op = '*';
                break;
            case TOK_DIV_ASSIGN:
                node->op = '/';
                break;
            case TOK_MOD_ASSIGN:
                node->op = '%';
                break;
            case TOK_AND_ASSIGN:
                node->op = '&';
                break;
            case TOK_OR_ASSIGN:
                node->op = '|';
                break;
            case TOK_XOR_ASSIGN:
                node->op = '^';
                break;
            case TOK_LSHIFT_ASSIGN:
                node->op = 'L';
                break;
            case TOK_RSHIFT_ASSIGN:
                node->op = 'R';
                break;
            default:
                break;
            }
        }

        // We enforce that the left side is an lvalue...
        if (left->type != AST_ID && left->type != AST_DEREF)
        {
            compile_error(ERR_SEMANTIC, "Invalid left-hand side of assignment");
        }

        node->left = left;
        node->right = parse_expr(precedence - 1);
        return node;
    }

    // Binary math, bitwise, logical, relational (left-associative)
    AST_node_t *node = NULL;

    if (TOK_LOGICAL_AND == op_token.type)
    {
        node = create_node(AST_LOGICAL_AND);
    }
    else if (TOK_LOGICAL_OR == op_token.type)
    {
        node = create_node(AST_LOGICAL_OR);
    }
    else if (op_token.type == TOK_EQ || op_token.type == TOK_NEQ || op_token.type == TOK_LT ||
             op_token.type == TOK_GT || op_token.type == TOK_LTE || op_token.type == TOK_GTE)
    {
        node = create_node(AST_CMP);
        strcpy(node->lexeme, op_token.lexeme);
    }
    else
    {
        // Standard binary ops (+, -, *, /, %, &, |, ^, <<, >>)
        node = create_node(AST_BINOP);
        if (TOK_LSHIFT == op_token.type)
        {
            node->op = 'L';
        }
        else if (TOK_RSHIFT == op_token.type)
        {
            node->op = 'R';
        }
        else
        {
            node->op = op_token.lexeme[0];
        }
    }

    node->left = left;
    node->right = parse_expr(precedence); // Left-Associative
    return node;
}

AST_node_t *parse_statement();
AST_node_t *parse_block();

AST_node_t *parse_top_level()
{
    char name[LEXEME_LENGTH];
    strcpy(name, current_token.lexeme);
    eat(TOK_ID);

    if (TOK_ASSIGN == current_token.type)
    {
        // It's a manifest definition so we capture raw text bypassing the lexer.
        int i = 0;
        char m_text[LEXEME_LENGTH];

        while (*source_code != ';' && *source_code != '\0' && i < LEXEME_LENGTH - 1)
        {
            m_text[i++] = *source_code++;
        }
        m_text[i] = '\0';

        if (';' == *source_code)
        {
            source_code++;
        }

        strcpy(manifests[manifest_count].name, name);
        strcpy(manifests[manifest_count].text, m_text);
        manifest_count++;

        advance();
        return NULL; // Manifests don't generate assembly AST nodes.
    }

    if (TOK_LPAREN == current_token.type)
    {
        // It is a function.
        AST_node_t *node = create_node(AST_FUNC_DECL);
        strcpy(node->lexeme, name);
        eat(TOK_LPAREN);

        AST_node_t *arg_head = NULL;
        AST_node_t *current_arg = NULL;

        while (TOK_RPAREN != current_token.type)
        {
            AST_node_t *new_arg = create_node(AST_UNKNOWN);
            new_arg->left = create_node(AST_ID);
            strcpy(new_arg->left->lexeme, current_token.lexeme);
            eat(TOK_ID);

            if (!arg_head)
            {
                arg_head = new_arg;
            }
            if (current_arg)
            {
                current_arg->right = new_arg;
            }
            current_arg = new_arg;

            if (TOK_COMMA == current_token.type)
            {
                eat(TOK_COMMA);
            }
        }

        eat(TOK_RPAREN);
        node->left = arg_head;
        node->right = parse_block();
        return node;
    }
    else
    {
        AST_node_t *head = create_node(AST_BLOCK);
        AST_node_t *curr = head;

        while (1)
        {
            AST_node_t *node = create_node(AST_GLOBAL_DEF);
            strcpy(node->lexeme, name);

            if (TOK_LBRACK == current_token.type)
            {
                eat(TOK_LBRACK);
                node->left = create_node(AST_NUM);
                strcpy(node->left->lexeme, current_token.lexeme);
                eat(TOK_NUM);
                eat(TOK_RBRACK);
                node->offset = 1; // Mark as vector
            }
            else if (TOK_LBRACE == current_token.type)
            {
                eat(TOK_LBRACE);
                node->left = create_node(AST_NUM);
                strcpy(node->left->lexeme, current_token.lexeme);
                eat(TOK_NUM);
                eat(TOK_RBRACE);
                node->offset = 0;
            }

            curr->left = node;

            if (TOK_COMMA == current_token.type)
            {
                eat(TOK_COMMA);
                strcpy(name, current_token.lexeme);
                eat(TOK_ID);
                curr->right = create_node(AST_BLOCK);
                curr = curr->right;
            }
            else
            {
                break;
            }
        }

        eat(TOK_SEMI);
        return head;
    }
}

// Parse a block of statements wrapped in {}...
AST_node_t *parse_block()
{
    eat(TOK_LBRACE);
    AST_node_t *block = create_node(AST_BLOCK);
    AST_node_t *current = block;

    // We loop until we hit the closing brace...
    while (TOK_RBRACE != current_token.type && TOK_EOF != current_token.type)
    {
        current->left = parse_statement();

        // Chain if there are more statements coming.
        if (TOK_RBRACE != current_token.type)
        {
            current->right = create_node(AST_BLOCK);
            current = current->right;
        }
    }

    eat(TOK_RBRACE);
    return block;
}

// Parse a statement, duh. ('auto x;' or 'x = 10 + 20;').
AST_node_t *parse_statement()
{
    if (TOK_AUTO == current_token.type)
    {
        eat(TOK_AUTO);

        AST_node_t *head = create_node(AST_BLOCK);
        AST_node_t *curr = head;

        while (1)
        {
            AST_node_t *decl = create_node(AST_VAR_DECL);
            strcpy(decl->lexeme, current_token.lexeme);
            eat(TOK_ID);

            if (TOK_LBRACK == current_token.type)
            {
                eat(TOK_LBRACK);
                decl->left = create_node(AST_NUM);
                strcpy(decl->left->lexeme, current_token.lexeme);
                eat(TOK_NUM);
                eat(TOK_RBRACK);
            }

            curr->left = decl;

            if (TOK_COMMA == current_token.type)
            {
                eat(TOK_COMMA);
                curr->right = create_node(AST_BLOCK);
                curr = curr->right;
            }
            else
            {
                break;
            }
        }

        eat(TOK_SEMI);
        return head;
    }
    else if (TOK_IF == current_token.type)
    {
        eat(TOK_IF);
        eat(TOK_LPAREN);

        AST_node_t *node = create_node(AST_IF);
        node->left = parse_expr(PREC_NONE);

        eat(TOK_RPAREN);

        // True body.
        if (TOK_LBRACE == current_token.type)
        {
            node->right = parse_block(); // It's a block.
        }
        else
        {
            node->right = parse_statement(); // It's a single statement.
        }

        // Optional false body.
        if (TOK_ELSE == current_token.type)
        {
            eat(TOK_ELSE);
            if (TOK_LBRACE == current_token.type)
            {
                node->extra = parse_block(); // It's a block.
            }
            else
            {
                node->extra = parse_statement(); // It's a single statement.
            }
        }

        return node;
    }
    else if (TOK_WHILE == current_token.type)
    {
        eat(TOK_WHILE);
        eat(TOK_LPAREN);

        AST_node_t *node = create_node(AST_WHILE);
        node->left = parse_expr(PREC_NONE);

        eat(TOK_RPAREN);

        if (TOK_LBRACE == current_token.type)
        {
            node->right = parse_block();
        }
        else
        {
            node->right = parse_statement();
        }

        return node;
    }
    else if (TOK_FOR == current_token.type)
    {
        eat(TOK_FOR);
        eat(TOK_LPAREN);
        AST_node_t *node = create_node(AST_FOR);

        node->left = parse_expr(PREC_NONE); // Init.
        eat(TOK_SEMI);
        node->middle = parse_expr(PREC_NONE); // condition.
        eat(TOK_SEMI);
        node->right = parse_expr(PREC_NONE); // Increment.
        eat(TOK_RPAREN);

        if (TOK_LBRACE == current_token.type)
        {
            node->extra = parse_block();
        }
        else
        {
            node->extra = parse_statement();
        }

        return node;
    }
    else if (TOK_REPEAT == current_token.type)
    {
        eat(TOK_REPEAT);
        AST_node_t *node = create_node(AST_REPEAT);

        if (TOK_LBRACE == current_token.type)
        {
            node->left = parse_block();
        }
        else
        {
            node->left = parse_statement();
        }

        return node;
    }
    else if (TOK_BREAK == current_token.type)
    {
        eat(TOK_BREAK);
        eat(TOK_SEMI);
        return create_node(AST_BREAK);
    }
    else if (TOK_NEXT == current_token.type)
    {
        eat(TOK_NEXT);
        eat(TOK_SEMI);
        return create_node(AST_NEXT);
    }
    else if (TOK_CASE == current_token.type)
    {
        eat(TOK_CASE);
        AST_node_t *node = create_node(AST_CASE);
        strcpy(node->lexeme, current_token.lexeme);
        if (TOK_CHAR == current_token.type)
        {
            eat(TOK_CHAR);
        }
        else
        {
            eat(TOK_NUM);
        }

        eat(TOK_COLON);
        return node;
    }
    else if (TOK_DEFAULT == current_token.type)
    {
        eat(TOK_DEFAULT);
        AST_node_t *node = create_node(AST_DEFAULT);
        eat(TOK_COLON);
        return node;
    }
    else if (TOK_SWITCH == current_token.type)
    {
        eat(TOK_SWITCH);
        eat(TOK_LPAREN);
        AST_node_t *node = create_node(AST_SWITCH);
        node->left = parse_expr(PREC_NONE);
        eat(TOK_RPAREN);

        if (TOK_LBRACE == current_token.type)
        {
            node->right = parse_block();
        }
        else
        {
            node->right = parse_statement();
        }

        return node;
    }
    else if (TOK_RETURN == current_token.type)
    {
        eat(TOK_RETURN);
        AST_node_t *node = create_node(AST_RETURN);

        // In B, 'return;' returns nothing, but 'return (expr);' returns a value.
        // We will support a simpler `return expr;` for ease of parsing for now...
        // Yes, TODO.
        if (TOK_SEMI != current_token.type)
        {
            node->left = parse_expr(PREC_NONE);
        }
        eat(TOK_SEMI);
        return node;
    }
    else if (TOK_GOTO == current_token.type)
    {
        eat(TOK_GOTO);
        AST_node_t *node = create_node(AST_GOTO);
        strcpy(node->lexeme, current_token.lexeme);
        eat(TOK_ID);
        eat(TOK_SEMI);
        return node;
    }
    else if (TOK_EXTRN == current_token.type)
    {
        eat(TOK_EXTRN);
        AST_node_t *node = create_node(AST_EXTRN);

        AST_node_t *head = NULL;
        AST_node_t *curr = NULL;

        while (TOK_SEMI != current_token.type)
        {
            AST_node_t *var = create_node(AST_ID);
            strcpy(var->lexeme, current_token.lexeme);
            eat(TOK_ID);

            if (!head)
            {
                head = var;
            }
            if (curr)
            {
                curr->right = var;
            }
            curr = var;

            if (TOK_COMMA == current_token.type)
            {
                eat(TOK_COMMA);
            }
        }

        eat(TOK_SEMI);
        node->left = head;
        return node;
    }
    // Null Statement: https://www.thinkage.ca/gcos/expl/b/manu/manu.html#Section5_2
    else if (TOK_SEMI == current_token.type)
    {
        eat(TOK_SEMI);
        return NULL;
    }
    else
    {
        AST_node_t *expr = parse_expr(PREC_NONE);

        if (AST_ID == expr->type && TOK_COLON == current_token.type)
        {
            eat(TOK_COLON);
            expr->type = AST_LABEL;
            return expr;
        }

        AST_node_t *node = create_node(AST_EXPR_STMT);
        node->left = expr;
        eat(TOK_SEMI);
        return node;
    }
}

// Some memory management.
// Fun fact: Plan 9 C compiler doesn't free all allocated memory.
//           Some will find it sloppy... But it actually makes sense.
//           You know, it's faster, makes everything simpler, modern
//           OS reclaims memory immediately after... And, a compiler
//           is not a long-running program, like a server, some leaks
//           for a few seconds at most are harmless.
void free_ast(AST_node_t *node)
{
    if (NULL == node)
    {
        return;
    }

    // Recursively free children first (if not you are dummy).
    free_ast(node->left);
    free_ast(node->middle);
    free_ast(node->right);
    free_ast(node->extra);

    free(node);
}

// Codegen moment: because after all we want it to work.
//                 x86-64 assembly, just in case.

// We need a symbol table first. We could go around throwing
// '.bss' globals, but we are true compiler engineers (yes,
// I am writing all this for the future tutorial and it's 1 AM),
// what we have to do is remember and track the variables and go
// to the stack. (:

typedef enum sym_type_t
{
    SYM_LOCAL,
    SYM_GLOBAL
} sym_type_t;

typedef struct symbol_t
{
    char name[LEXEME_LENGTH];
    int offset;
    sym_type_t type;
} symbol_t;

symbol_t sym_table[1000];
int sym_count = 0;
int current_offset = 0; // To track the stack size.

char called_functions[500][LEXEME_LENGTH];
int called_func_count = 0;

void track_call(const char *name)
{
    for (int i = 0; i < called_func_count; ++i)
    {
        if (0 == strcmp(called_functions[i], name))
        {
            return;
        }
    }
    strcpy(called_functions[called_func_count++], name);
}

symbol_t *get_symbol(const char *name)
{
    for (int i = 0; i < sym_count; ++i)
    {
        if (0 == strcmp(sym_table[i].name, name))
        {
            return &sym_table[i];
        }
    }
    compile_error(ERR_SEMANTIC, "Undeclared variable '%s'", name);
}

// Helper to find where a variable lives on the stack.
int get_offset(const char *name)
{
    for (int i = 0; i < sym_count; ++i)
    {
        if (0 == strcmp(sym_table[i].name, name))
        {
            return sym_table[i].offset;
        }
    }

    fprintf(stderr, "Semantic Error: Undeclared variable '%s'\n", name);
    exit(EXIT_FAILURE);
}

void emit_switch_jumps(AST_node_t *node, int end_label, int *default_label)
{
    if (!node)
    {
        return;
    }

    if (AST_CASE == node->type)
    {
        node->offset = label_counter++;
        emit("cmp rax, %s", node->lexeme);
        emit("je .L_CASE_%d", node->offset);
    }
    else if (AST_DEFAULT == node->type)
    {
        node->offset = label_counter++;
        *default_label = node->offset;
    }

    emit_switch_jumps(node->left, end_label, default_label);
    emit_switch_jumps(node->middle, end_label, default_label);
    emit_switch_jumps(node->right, end_label, default_label);
    emit_switch_jumps(node->extra, end_label, default_label);
}

void emit(const char *format, ...)
{
    // We indent assembly instructions.
    printf("  ");

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("\n");
}

void emit_label(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf(":\n");
}

void generate_code(AST_node_t *node)
{
    if (NULL == node)
    {
        return;
    }

    switch (node->type)
    {
    // Push the number onto the stack.
    case AST_NUM:
        // x86-64 physically cannot push a 64-bit immediate directly.
        // Therefore we must load it first.
        emit("mov rax, %s", node->lexeme);
        emit("push rax");
        break;
    case AST_ID:
    {
        symbol_t *sym = get_symbol(node->lexeme);

        if (SYM_LOCAL == sym->type)
        {
            emit("push qword [rbp - %d] ; %s", sym->offset, sym->name);
        }
        else
        {
            emit("push qword [%s] ; global %s", sym->name, sym->name);
        }
        break;
    }

    case AST_BINOP:
        generate_code(node->left);
        generate_code(node->right);

        // Pop both into registers
        emit("pop rbx"); // Right operand
        emit("pop rax"); // Left '''

        // math moment.
        // TODO: turn into a switch case.
        if ('+' == node->op)
        {
            emit("add rax, rbx");
        }
        else if ('-' == node->op)
        {
            emit("sub rax, rbx");
        }
        else if ('*' == node->op)
        {
            emit("imul rax, rbx");
        }
        else if ('/' == node->op)
        {
            emit("cqo");
            emit("idiv rbx");
        }
        else if ('%' == node->op)
        {
            emit("cqo");
            emit("idiv rbx");
            emit("mov rax, rdx");
        }
        else if ('&' == node->op)
        {
            emit("and rax, rbx");
        }
        else if ('|' == node->op)
        {
            emit("or rax, rbx");
        }
        else if ('^' == node->op)
        {
            emit("xor rax, rbx");
        }
        else if ('!' == node->op)
        {
            emit("not rax, rbx");
        }
        else if ('L' == node->op)
        {
            emit("mov rcx, rbx");
            emit("shl rax, cl");
        }
        else if ('R' == node->op)
        {
            emit("mov rcx, rbx");
            emit("sar rax, cl");
        }

        // Push result back.
        emit("push rax");
        break;

    case AST_ASSIGN:
        // Evaluate the right side (the math), which leaves the result on the stack...
        generate_code(node->right);

        // Where are we storing it?
        if (AST_ID == node->left->type)
        {
            // Standard variable assignment.
            symbol_t *sym = get_symbol(node->left->lexeme);
            emit("pop rax");

            if (SYM_LOCAL == sym->type)
            {
                emit("mov qword[rbp - %d], rax ; %s =", sym->offset, sym->name);
            }
            else
            {
                emit("mov qword[%s], rax ; global %s=", sym->name, sym->name);
            }
            emit("mov rdi, rax");
        }
        else if (AST_DEREF == node->left->type)
        {
            // Pointer assignment.
            // The left side of the DEREF node is the pointer itself.
            generate_code(node->left->left);

            emit("pop rbx");
            emit("pop rax");
            emit("shl rbx, 3");

            emit("mov qword [rbx], rax ; *ptr =");
            emit("mov rdi, rax");
        }
        else
        {
            compile_error(ERR_SEMANTIC, "Invalid left-hand side of assignment");
        }

        emit("push rax");
        break;

    case AST_COMPOUND_ASSIGN:
    {
        // First we calculate the address of the lvalue and push it.
        if (AST_ID == node->left->type)
        {
            symbol_t *sym = get_symbol(node->left->lexeme);
            if (SYM_LOCAL == sym->type)
            {
                emit("lea rax, [rbp - %d]", sym->offset);
            }
            else
            {
                emit("lea rax, [%s]", sym->name);
            }
            emit("shr rax, 3");
            emit("push rax");
        }
        else if (AST_DEREF == node->left->type)
        {
            generate_code(node->left->left);
        }

        // Then we readd the current value from that address.
        emit("pop rax");
        emit("push rax ; We save it for the final write.");
        emit("shl rax, 3 ; Convert to physical for read");
        emit("mov rbx, qword [rax]");
        emit("push rbx ; Push the content.");

        generate_code(node->right); // We evaluate the Rvalue.

        emit("pop rbx ; Rvalue");
        emit("pop rax ; Lvalue's contents.");

        switch (node->op)
        {
        case '+':
            emit("add rax, rbx");
            break;
        case '-':
            emit("sub rax, rbx");
            break;
        case '*':
            emit("imul rax, rbx");
            break;
        case '/':
            emit("cqo");
            emit("idiv rbx");
            break;
        case '%':
            emit("cqo");
            emit("idiv rbx");
            emit("mov rax, rdx");
            break;
        case '&':
            emit("and rax, rbx");
            break;
        case '|':
            emit("or rax, rbx");
            break;
        case '^':
            emit("xor rax, rbx");
            break;
        case 'L':
            emit("mov rcx, rbx");
            emit("shl rax, cl");
            break;
        case 'R':
            emit("mov rcx, rbx");
            emit("sar rax, cl");
            break;
        }

        // We write back to the saved address.
        emit("pop rcx");
        emit("shl rcx, 3 ; Convert to physical for write");
        emit("mov qword [rcx], rax");

        emit("push rax");
        break;
    }

    case AST_VAR_DECL:
    {
        int array_size = 0;
        if (node->left)
        {
            array_size = atoi(node->left->lexeme);
        }

        // First we allocate space for the pointer variable itself.
        current_offset += WORD_LENGTH;
        strcpy(sym_table[sym_count].name, node->lexeme);
        sym_table[sym_count].offset = current_offset;
        sym_table[sym_count].type = SYM_LOCAL;
        sym_count++;

        if (array_size > 0)
        {
            emit("; auto %s[%d]", node->lexeme, array_size);
        }
        else
        {
            emit("; auto %s", node->lexeme);
        }

        emit("sub rsp, 8");

        // If it is a vector we allocate the extra space
        // and initialize the pointer.
        if (array_size > 0)
        {
            emit("sub rsp, %d ; allocate %d words for vector", array_size * WORD_LENGTH,
                 array_size);
            current_offset += array_size * WORD_LENGTH;

            // Point the variable to the start of the newly allocated memory...
            int ptr_offset = sym_table[sym_count - 1].offset;
            int total_bytes = array_size * WORD_LENGTH;
            emit("lea rax, [rbp - %d]", ptr_offset + total_bytes);
            emit("shr rax, 3 ; Convert physical address to Word Address");
            emit("mov qword [rbp - %d], rax ; initialize vector pointer", ptr_offset);
        }
        break;
    }

    case AST_IF:
    {
        int else_label = label_counter++;
        int end_label = label_counter++;

        // Evaluate the condition, which pushes 1 or 0.
        generate_code(node->left);
        emit("pop rax");
        emit("cmp rax, 0");

        if (node->extra)
        {
            // Has an else branch.
            emit("je .L_ELSE_%d", else_label);
            generate_code(node->right); // True body.
            emit("jmp .L_END_%d", end_label);

            emit_label(".L_ELSE_%d", else_label);
            generate_code(node->extra); // False body.
            emit_label(".L_END_%d", end_label);
        }
        else
        {
            // Doesn't.
            emit("je .L_END_%d", end_label);
            generate_code(node->right);
            emit_label(".L_END_%d", end_label);
        }

        break;
    }

    case AST_WHILE:
    {
        int next_label = label_counter++;
        int end_label = label_counter++;

        int prev_break = current_break_label;
        int prev_next = current_next_label;

        current_break_label = end_label;
        current_next_label = next_label;

        emit_label(".L_NEXT_%d", next_label);

        generate_code(node->left);

        emit("pop rax");
        emit("cmp rax, 0");

        // If false, jump out...
        emit("je .L_END_%d", end_label);

        generate_code(node->right);

        // We go back.
        emit("jmp .L_NEXT_%d", next_label);
        emit_label(".L_END_%d", end_label);

        current_break_label = prev_break;
        current_next_label = prev_next;
        break;
    }

    case AST_FOR:
    {
        int start_label = label_counter++;
        int next_label = label_counter++;
        int end_label = label_counter++;

        int prev_break = current_break_label;
        int prev_next = current_next_label;

        current_break_label = end_label;
        current_next_label = next_label;

        // Initialization.
        generate_code(node->left);
        emit("add rsp, 8 ; discard init result");

        emit_label(".L_START_%d", start_label);

        // Condition check
        generate_code(node->middle);
        emit("pop rax");
        emit("cmp rax, 0");
        emit("je .L_END_%d", end_label); // Exit if false

        generate_code(node->extra);

        // Increment
        emit_label(".L_NEXT_%d", next_label);
        generate_code(node->right);
        emit("add rsp, 8 ; discard increment result");

        emit("jmp .L_START_%d", start_label);

        emit_label(".L_END_%d", end_label);

        current_break_label = prev_break;
        current_next_label = prev_next;

        break;
    }

    case AST_REPEAT:
    {
        int next_label = label_counter++;
        int end_label = label_counter++;

        int prev_break = current_break_label;
        int prev_next = current_next_label;

        current_break_label = end_label;
        current_next_label = next_label;

        emit_label(".L_NEXT_%d", next_label);

        generate_code(node->left);

        emit("jmp .L_NEXT_%d", next_label);
        emit_label(".L_END_%d", end_label);

        current_break_label = prev_break;
        current_next_label = prev_next;
        break;
    }

    case AST_SWITCH:
    {
        int end_label = label_counter++;

        int prev_break = current_break_label;
        current_break_label = end_label;

        // Evaluate switch condition.
        generate_code(node->left);
        emit("pop rax");

        // Pre-scan.
        int default_label = -1;
        emit_switch_jumps(node->right, end_label, &default_label);

        if (default_label != -1)
        {
            emit("jmp .L_DEFAULT_%d", default_label);
        }
        else
        {
            emit("jmp .L_END_%d", end_label);
        }

        generate_code(node->right);

        emit_label(".L_END_%d", end_label);

        current_break_label = prev_break;
        break;
    }

    case AST_CASE:
        emit_label(".L_CASE_%d", node->offset);
        break;

    case AST_DEFAULT:
        emit_label(".L_DEFAULT_%d", node->offset);
        break;

    case AST_BREAK:
        if (-1 == current_break_label)
        {
            compile_error(ERR_SEMANTIC, "Break outside of loop/switch");
        }
        emit("jmp .L_END_%d", current_break_label);
        break;

    case AST_NEXT:
        if (-1 == current_next_label)
        {
            compile_error(ERR_SEMANTIC, "Next outside of loop");
        }
        emit("jmp .L_NEXT_%d", current_next_label);
        break;

    case AST_CMP:
        generate_code(node->left);
        generate_code(node->right);
        emit("pop rbx");
        emit("pop rax");
        emit("cmp rax, rbx");

        if (0 == strcmp(node->lexeme, "=="))
        {
            emit("sete al");
        }
        else if (0 == strcmp(node->lexeme, "!="))
        {
            emit("setne al");
        }
        else if (0 == strcmp(node->lexeme, "<"))
        {
            emit("setl al");
        }
        else if (0 == strcmp(node->lexeme, ">"))
        {
            emit("setg al");
        }
        else if (0 == strcmp(node->lexeme, "<="))
        {
            emit("setle al");
        }
        else if (0 == strcmp(node->lexeme, ">="))
        {
            emit("setge al");
        }

        emit("movzx rax, al");
        emit("push rax");
        break;

    case AST_BLOCK:
        generate_code(node->left);  // Execute the current statement...
        generate_code(node->right); // ... and execute the next statement in the chain.
        break;

    case AST_EXPR_STMT:
        generate_code(node->left);

        // Discard that item, so the stack doesn't overflow.
        emit("add rsp, 8 ; discard expression result");
        break;

    case AST_ADDR:
        // We calculate the address and load it into rax.
        emit("lea rax, [rbp - %d] ; &%s", get_offset(node->lexeme), node->lexeme);
        emit("shr rax, 3 ; Convert physical address to Word Address");
        emit("push rax");
        break;

    case AST_DEREF:
        // We evaluate whatever is inside the pointer,
        // which leaves an address on the stack.
        generate_code(node->left);

        emit("pop rax");
        emit("shl rax, 3 ; Convert Word Address back to physical");
        emit("mov rax, qword [rax] ; dereference");
        emit("push rax");
        break;

    case AST_PRE_INC:
    case AST_PRE_DEC:
    case AST_POST_INC:
    case AST_POST_DEC:
    {
        if (AST_ID == node->left->type)
        {
            symbol_t *sym = get_symbol(node->left->lexeme);
            if (SYM_LOCAL == sym->type)
            {
                emit("lea rax, [rbp - %d]", sym->offset);
            }
            else
            {
                emit("lea rax, [%s]", sym->name);
            }
        }
        else if (AST_DEREF == node->left->type)
        {
            generate_code(node->left->left);
            emit("pop rax");
            emit("shl rax, 3 ; Convert Word Address to Physical");
        }
        else
        {
            compile_error(ERR_SEMANTIC, "Increment/Decrement requires an lvalue");
        }

        emit("mov rbx, qword [rax]");

        emit("mov rcx, rbx");
        if (AST_PRE_INC == node->type || AST_POST_INC == node->type)
        {
            emit("add rcx, 1");
        }
        else
        {
            emit("sub rcx, 1");
        }
        emit("mov qword [rax], rcx ; Write new value back to memory");

        if (AST_POST_INC == node->type || AST_POST_DEC == node->type)
        {
            emit("push rbx");
        }
        else
        {
            emit("push rcx");
        }
        break;
    }

    case AST_LOGICAL_AND:
    {
        int false_label = label_counter++;
        int end_label = label_counter++;

        generate_code(node->left);
        emit("pop rax");
        emit("cmp rax, 0");
        emit("je .L_FALSE_%d", false_label);

        generate_code(node->right);
        emit("pop rax");
        emit("cmp rax, 0");
        emit("je .L_FALSE_%d", false_label);

        emit("push 1 ; Both true");
        emit("jmp .L_END_%d", end_label);

        emit_label(".L_FALSE_%d", false_label);
        emit("push 0 ; One was false");

        emit_label(".L_END_%d", end_label);
        break;
    }

    case AST_LOGICAL_OR:
    {
        int true_label = label_counter++;
        int end_label = label_counter++;

        generate_code(node->left);
        emit("pop rax");
        emit("cmp rax, 0");
        emit("jne .L_TRUE_%d", true_label);

        generate_code(node->right);
        emit("pop rax");
        emit("cmp rax, 0");
        emit("jne .L_TRUE_%d", true_label);

        emit("push 0 ; Both false");
        emit("jmp .L_END_%d", end_label);

        emit_label(".L_TRUE_%d", true_label);
        emit("push 1 ; One was true");

        emit_label(".L_END_%d", end_label);
        break;
    }

    case AST_FUNC_DECL:
    {
        strcpy(sym_table[sym_count].name, node->lexeme);
        sym_table[sym_count].type = SYM_GLOBAL;
        sym_count++;

        int new_sym_count = 0;
        for (int i = 0; i < sym_count; ++i)
        {
            if (sym_table[i].type == SYM_GLOBAL)
            {
                sym_table[new_sym_count++] = sym_table[i];
            }
        }
        sym_count = new_sym_count;
        current_offset = 0;

        printf("\nglobal %s\n", node->lexeme);
        emit_label("%s", node->lexeme);
        emit("push rbp");
        emit("mov rbp, rsp");

        for (int i = 5; i >= 0; i--)
        {
            emit("push %s ; Spilling potential arg %d", abi_regs[i], i + 1);
        }
        current_offset = 48; // 6 registers * 8 bytes

        AST_node_t *param = node->left;
        int param_idx = 0;
        while (param)
        {
            strcpy(sym_table[sym_count].name, param->left->lexeme);
            if (param_idx < 6)
            {
                sym_table[sym_count].offset = 48 - (param_idx * 8);
            }
            else
            {
                // Stack arguments are above the return address and saved RBP.
                // [rbp + 16] is the 7th argument.
                sym_table[sym_count].offset = -16 - ((param_idx - 6) * 8);
            }
            sym_table[sym_count].type = SYM_LOCAL;
            sym_count++;

            param = param->right;
            param_idx++;
        }

        generate_code(node->right);

        emit("mov rsp, rbp");
        emit("pop rbp");
        emit("ret");
        break;
    }

    case AST_FUNC_CALL:
    {
        AST_node_t *arg = node->left;
        int arg_count = 0;
        while (arg)
        {
            arg_count++;
            arg = arg->right;
        }

        // x86-64 SysV ABI: arguments 1-6 in registers, rest on stack.
        // Stack must be 16-byte aligned before the call...
        int stack_args = arg_count > 6 ? arg_count - 6 : 0;
        int padding = 0;
        if (stack_args % 2 != 0)
        {
            padding = 1;
            emit("push rax");
        }

        // Push stack arguments (7..n) in reverse order.
        if (stack_args > 0)
        {
            for (int i = 0; i < stack_args; i++)
            {
                AST_node_t *target = node->left;
                // Move to the (arg_count - i)th argument.
                for (int j = 0; j < arg_count - 1 - i; j++)
                {
                    target = target->right;
                }
                generate_code(target->left);
            }
        }

        int regs_to_push = arg_count > 6 ? 6 : arg_count;
        arg = node->left;
        for (int i = 0; i < regs_to_push; i++)
        {
            generate_code(arg->left);
            arg = arg->right;
        }

        for (int i = regs_to_push - 1; i >= 0; --i)
        {
            emit("pop %s", abi_regs[i]);
        }

        track_call(node->lexeme);

        emit("xor rax, rax ; For variadic functions.");
        emit("call %s", node->lexeme);

        // Clean up stack arguments and padding.
        if (stack_args + padding > 0)
        {
            emit("add rsp, %d", (stack_args + padding) * 8);
        }

        // The return value lives in rax.
        emit("push rax");
        break;
    }

    case AST_RETURN:
        if (node->left)
        {
            generate_code(node->left);
            emit("pop rax");
        }
        else
        {
            emit("xor rax, rax ; Return 0 by default.");
        }

        emit("mov rsp, rbp");
        emit("pop rbp");
        emit("ret");
        break;

    case AST_GLOBAL_DEF:
        // Add to symbol table so our functions don't crash...
        strcpy(sym_table[sym_count].name, node->lexeme);
        sym_table[sym_count].type = SYM_GLOBAL;
        sym_count++;

        if (1 == node->offset)
        {
            // Global vector.
            int size = atoi(node->left->lexeme);
            printf("\nsection .bss\n");
            emit_label("VEC_%s", node->lexeme);
            emit("resq %d ; The raw memory", size);
            printf("section .data\n");
            emit_label("%s", node->lexeme);
            emit("dq VEC_%s ; The pointer to the memory", node->lexeme);

            strcpy(global_vectors[global_vector_count++], node->lexeme);
        }
        else
        {
            // Global variable.
            printf("\nsection .data\n");
            emit_label("%s", node->lexeme);
            if (node->left)
            {
                emit("dq %s", node->left->lexeme);
            }
            else
            {
                emit("dq 0");
            }
        }
        printf("section .text\n");
        break;

    case AST_EXTRN:
    {
        // 'extrn' doesn't generate assembly instructions.
        // It just updates the symbol table.
        AST_node_t *var = node->left;

        while (var)
        {
            strcpy(sym_table[sym_count].name, var->lexeme);
            sym_table[sym_count].type = SYM_GLOBAL;
            sym_count++;
            emit("extern %s", var->lexeme);
            var = var->right;
        }
        break;
    }

    case AST_STRING:
    {
        int str_id = string_counter++;

        emit("section .data");
        emit("align 8 ;");
        emit_label(".STR_%d", str_id);

        char *str = node->lexeme;
        int len = strlen(str);
        int i = 0;
        int done = 0;

        while (!done)
        {
            unsigned long long word_val = 0;
            int chars_in_word = 0;

            while (chars_in_word < 8)
            {
                unsigned char c = 0;

                if (i <= len)
                {
                    c = str[i];
                    if ('\0' == c)
                    {
                        done = 1;
                    }
                    i++;
                }

                word_val |= ((unsigned long long)c << (chars_in_word * 8));
                chars_in_word++;
            }

            emit("dq %llu", word_val);
        }

        emit("section .text");

        emit("lea rax, [.STR_%d]", str_id);
        emit("shr rax, 3 ; Convert physical string address to Word Address");
        emit("push rax");
        break;
    }

    case AST_NEG:
        generate_code(node->left);
        emit("pop rax");
        emit("neg rax");
        emit("push rax");
        break;

    case AST_NOT:
        generate_code(node->left);
        emit("pop rax");
        emit("cmp rax, 0");
        emit("sete al");
        emit("movzx rax, al");
        emit("push rax");
        break;

    case AST_COMPLEMENT:
        generate_code(node->left);
        emit("pop rax");
        emit("not rax");
        emit("push rax");
        break;

    case AST_LABEL:
        emit_label(".B_USER_%s", node->lexeme);
        break;

    case AST_GOTO:
        emit("jmp .B_USER_%s", node->lexeme);
        break;

    case AST_TERNARY:
    {
        int false_label = label_counter++;
        int end_label = label_counter++;

        generate_code(node->left);
        emit("pop rax");
        emit("cmp rax, 0");

        emit("je .L_FALSE_%d", false_label);

        generate_code(node->middle);
        emit("jmp .L_END_%d", end_label);

        emit_label(".L_FALSE_%d", false_label);
        generate_code(node->right);

        emit_label(".L_END_%d", end_label);
        break;
    }

    default:
        break;
    }
}

// So... string dummies are fun, but we are doing real engineering.
char *read_file(const char *filename)
{
    FILE *file = fopen(filename, "r");

    if (!file)
    {
        compile_error(ERR_FATAL, "Could not open file '%s'", filename);
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = malloc(length + 1);
    if (!buffer)
    {
        compile_error(ERR_FATAL, "Memory allocation failed");
    }

    size_t read_size = fread(buffer, 1, length, file);
    buffer[read_size] = '\0';

    fclose(file);
    return buffer;
}

// Main...
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: %s <source_file.b>\n", argv[0]);
        return EXIT_FAILURE;
    }

    color_init();

    char *file_contents = read_file(argv[1]);
    source_code = file_contents;

    printf("; Compiling B file: '%s'\n\n", argv[1]);

    advance();

    // Main compiler loop
    while (current_token.type != TOK_EOF)
    {
        AST_node_t *topl = parse_top_level();
        if (topl)
        {
            generate_code(topl);
            free_ast(topl);
        }
    }

    printf("\n;BOOTSTRAP ROUTINE\n");
    printf("global _start\n");
    printf("section .text\n");
    emit_label("_start");

    for (int i = 0; i < global_vector_count; ++i)
    {
        emit("shr qword [%s], 3", global_vectors[i]);
    }

    emit("mov rdi, [rsp]      ; arg 1: argc");
    emit("lea rsi, [rsp+8]    ; arg 2: argv");
    emit("extern b_init_argv");
    emit("call b_init_argv");
    emit("mov rsi, rax");
    emit("mov rdi, [rsp]");

    emit("call main");
    emit("mov rdi, rax");
    emit("extern exit");
    emit("call exit");

    printf("\n;UNRESOLVED EXTERNAL SYMBOLS\n");
    for (int i = 0; i < called_func_count; ++i)
    {
        int is_defined = 0;
        for (int j = 0; j < sym_count; j++)
        {
            if (SYM_GLOBAL == sym_table[j].type &&
                0 == strcmp(sym_table[j].name, called_functions[i]))
            {
                is_defined = 1;
                break;
            }
        }
        if (!is_defined)
        {
            emit("extern %s", called_functions[i]);
        }
    }

    free(file_contents);

    return 0;
}
