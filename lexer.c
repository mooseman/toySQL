


/* lexer.c  */ 

#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "lexer.h"
#include "util.h"
#include "lice.h"
#include "opt.h"


static list_t *lexer_buffer = &SENTINEL_LIST;


typedef struct {
    char   *file;
    size_t  line;
    FILE   *fp;
} lexer_file_t;


static int          lexer_continuation = -1;
static lexer_file_t lexer_file;


__attribute__((constructor)) void lexer_init(void) {
    lexer_file.file = "(stdin)";
    lexer_file.line = 1;
    lexer_file.fp   = stdin;
}


static void lexer_file_unget(int ch) {
    if (ch == '\n')
        lexer_file.line --;
    if (lexer_continuation >= 0)
        ungetc(lexer_continuation, lexer_file.fp);
    lexer_continuation = ch;
}


static int lexer_file_get(void) {
    int ch = (lexer_continuation < 0) ? getc(lexer_file.fp) : lexer_continuation;
    lexer_continuation = -1;
    if (ch == '\\') {
        if ((ch = getc(lexer_file.fp)) == '\n') {
            lexer_file.line ++;
            return lexer_file_get();
        }
        lexer_file_unget(ch);
        return '\\';

    }
    if (ch == '\n')
        lexer_file.line ++;

    return ch;
}


static lexer_token_t *lexer_token_copy(lexer_token_t *token) {
    return memcpy(malloc(sizeof(lexer_token_t)), token, sizeof(lexer_token_t));
}

static lexer_token_t *lexer_identifier(string_t *str) {
    return lexer_token_copy(&(lexer_token_t){
        .type      = LEXER_TOKEN_IDENTIFIER,
        .string    = string_buffer(str)
    });
}

static lexer_token_t *lexer_strtok(string_t *str) {
    return lexer_token_copy(&(lexer_token_t){
        .type      = LEXER_TOKEN_STRING,
        .string    = string_buffer(str)
    });
} 

static lexer_token_t *lexer_punct(int punct) {
    return lexer_token_copy(&(lexer_token_t){
        .type      = LEXER_TOKEN_PUNCT,
        .punct     = punct
    });
} 

static lexer_token_t *lexer_number(char *string) {
    return lexer_token_copy(&(lexer_token_t){
        .type      = LEXER_TOKEN_NUMBER,
        .string    = string
    });
}
static lexer_token_t *lexer_char(char value) {
    return lexer_token_copy(&(lexer_token_t){
        .type      = LEXER_TOKEN_CHAR,
        .character = value
    });
}


static int lexer_skip(void) {
    int c;
    while ((c = lexer_file_get()) != EOF) {
        if (isspace(c) || c == '\n' || c == '\r')
            continue;
        lexer_file_unget(c);
        return c;
    }
    return EOF;
}

static lexer_token_t *lexer_read_number(int c) {
    string_t *string = string_create();
    string_cat(string, c);
    for (;;) {
        int p = lexer_file_get();
        if (!isdigit(p) && !isalpha(p) && p != '.') {
            lexer_file_unget(p);
            return lexer_number(string_buffer(string));
        }
        string_cat(string, p);
    }
    return NULL;
}


static int lexer_read_character_escaped(void) {
    int c = lexer_file_get();

    switch (c) {
        case '\'':        return '\'';
        case '"':         return '"';
        case '?':         return '?';
        case '\\':        return '\\';
        case 'a':         return '\a';
        case 'b':         return '\b';
        case 'f':         return '\f';
        case 'n':         return '\n';
        case 'r':         return '\r';
        case 't':         return '\t';
        case 'v':         return '\v';
        case 'e':         return '\033';        
        case EOF:
            compile_error("malformatted escape sequence");
        default:
            return c;
    }
}


static lexer_token_t *lexer_read_character(void) {
    int c = lexer_file_get();
    int r = (c == '\\') ? lexer_read_character_escaped() : c;

    if (lexer_file_get() != '\'')
        compile_error("unterminated character");

    return lexer_char((char)r);
}


static lexer_token_t *lexer_read_string(void) {
    string_t *string = string_create();
    for (;;) {
        int c = lexer_file_get();
        if (c == EOF)
            compile_error("Expected termination for string literal");

        if (c == '"')
            break;
        if (c == '\\')
            c = lexer_read_character_escaped();
        string_cat(string, c);
    }
    return lexer_strtok(string);
}


static lexer_token_t *lexer_read_identifier(int c1) {
    string_t *string = string_create();
    string_cat(string, (char)c1);

    for (;;) {
        int c2 = lexer_file_get();
        if (isalnum(c2) || c2 == '_' || c2 == '$') {
            string_cat(string, c2);
        } else {
            lexer_file_unget(c2);
            return lexer_identifier(string);
        }
    }
    return NULL;
}


static lexer_token_t *lexer_read_reclassify_one(int expect1, int a, int e) {
    int c = lexer_file_get();
    if (c == expect1)
        return lexer_punct(a);
    lexer_file_unget(c);
    return lexer_punct(e);
}


static lexer_token_t *lexer_read_reclassify_two(int expect1, int a, int expect2, int b, int e) {
    int c = lexer_file_get();
    if (c == expect1)
        return lexer_punct(a);
    if (c == expect2)
        return lexer_punct(b);
    lexer_file_unget(c);
    return lexer_punct(e);
}



static lexer_token_t *lexer_read_token(void) {
    int c;
    int n;

    lexer_skip();

    switch ((c = lexer_file_get())) {
        case '0' ... '9':  return lexer_read_number(c);
        case '"':          return lexer_read_string();
        case '\'':         return lexer_read_character();
        case 'a' ... 'z':
        case 'A' ... 'Z':      
        case '_':
            return lexer_read_identifier(c);
        case '(': case ')':
        case ',': case ';':
        case '[': case ']':
        case '{': case '}':
        case '?': case ':':
        case '~':
            return lexer_punct(c);

        case '=': return lexer_read_reclassify_one('=', LEXER_TOKEN_EQUAL,        '=');
        case '!': return lexer_read_reclassify_one('=', LEXER_TOKEN_NEQUAL,       '!');
        case '^': return lexer_read_reclassify_one('=', LEXER_TOKEN_COMPOUND_XOR, '^');

        case '<':
            if ((c = lexer_file_get()) == '=')
                return lexer_punct(LEXER_TOKEN_LEQUAL);
            lexer_file_unget(c);
            return lexer_punct('<');

        case '>':
            if ((c = lexer_file_get()) == '=')
                return lexer_punct(LEXER_TOKEN_GEQUAL);
            lexer_file_unget(c);
            return lexer_punct('>');

        case '.':
            n = lexer_file_get();
            if (isdigit(n)) {
                lexer_file_unget(n);
                return lexer_read_number(c);
            }
            if (n == '.') {
                string_t *str = string_create();
                string_catf(str, "..%c", lexer_file_get());
                return lexer_identifier(str);
            }
            lexer_file_unget(n);
            return lexer_punct('.');

        case EOF:
            return NULL;

        default:
            compile_error("Unexpected character: `%c`", c);
    }
    return NULL;
}


bool lexer_ispunct(lexer_token_t *token, int c) {
    return token && (token->type == LEXER_TOKEN_PUNCT) && (token->punct == c);
}


void lexer_unget(lexer_token_t *token) {
    if (!token)
        return;
    list_push(lexer_buffer, token);
}


lexer_token_t *lexer_next(void) {
    if (list_length(lexer_buffer) > 0)
        return list_pop(lexer_buffer);
    return lexer_read_token();
}


lexer_token_t *lexer_peek(void) {
    lexer_token_t *token = lexer_next();
    lexer_unget(token);
    return token;
}


char *lexer_token_string(lexer_token_t *token) {
    string_t *string = string_create();
    if (!token)
        return "(null)";
    switch (token->type) {
        case LEXER_TOKEN_PUNCT:
            if (token->punct == LEXER_TOKEN_EQUAL) {
                string_catf(string, "==");
                return string_buffer(string);
            }
        case LEXER_TOKEN_CHAR:
            string_cat(string, token->character);
            return string_buffer(string);
        case LEXER_TOKEN_NUMBER:
            string_catf(string, "%d", token->integer);
            return string_buffer(string);
        case LEXER_TOKEN_STRING:
            string_catf(string, "\"%s\"", token->string);
            return string_buffer(string);
        case LEXER_TOKEN_IDENTIFIER:
            return token->string;
        default:
            break;
    }
    compile_ice("unexpected token");
    return NULL;
}


char *lexer_marker(void) {
    string_t *string = string_create();
    string_catf(string, "%s:%zu", lexer_file.file, lexer_file.line);
    return string_buffer(string);
}


