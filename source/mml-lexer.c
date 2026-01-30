// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 virtualgrub39

#include "mml2midi.h"

#include <ctype.h>
#include <string.h>

static int
utf8_char_len (unsigned char c)
{
    if ((c & 0x80) == 0) return 1;    /* ASCII (0xxxxxxx) */
    if ((c & 0xE0) == 0xC0) return 2; /* 2-byte (110xxxxx) */
    if ((c & 0xF0) == 0xE0) return 3; /* 3-byte (1110xxxx) */
    if ((c & 0xF8) == 0xF0) return 4; /* 4-byte (11110xxx) */
    return 1;                         /* Fallback (invalid or continuation byte) */
}

static bool
is_space (char c)
{
    return isspace (c);
}

static bool
is_digit (char c)
{
    return isdigit (c);
}

static bool
is_ident_char (char c)
{
    unsigned char uc = (unsigned char)c;
    return isalnum (uc) || (uc >= 0x80);
}

typedef struct
{
    size_t offset, size;
    const char *data;
} mmlexer;

static void
skip_whitespace (mmlexer *lexer)
{
    while (lexer->offset < lexer->size && is_space (lexer->data[lexer->offset])) lexer->offset += 1;
}

static token
read_next_token (mmlexer *lexer)
{
    size_t u8char_len;
    token tok;

    if (lexer == NULL) return (token){ MML_UNKNOWN, { 0 } };

    skip_whitespace (lexer);

    size_t offset = lexer->offset;

    if (offset == lexer->size) return (token){ MML_EOF, { 0 } };

    u8char_len = utf8_char_len (lexer->data[offset]);
    if (offset + u8char_len > lexer->size) u8char_len = lexer->size - offset;

    tok.kind = MML_UNKNOWN;
    tok.view = (string_view){ lexer->data + lexer->offset, u8char_len };

    switch (lexer->data[offset])
    {
    case 'a' ... 'g':
    case 'r': tok.kind = MML_NOTE; break;
    case 'o':
    case '<':
    case '>':
    case 'l':
    case 'v':
    case 't': tok.kind = MML_COMMAND; break;
    case '+': tok.kind = MML_PLUS; break;
    case '-': tok.kind = MML_MINUS; break;
    case '.': tok.kind = MML_DOT; break;
    case ';': tok.kind = MML_SCOLON; break;
    case '}': tok.kind = MML_RBRACE; break;
    case '{': tok.kind = MML_LBRACE; break;
    case ']': tok.kind = MML_RBRACKET; break;
    case '[': tok.kind = MML_LBRACKET; break;
    case ':': tok.kind = MML_COLON; break;

    case '@': {
        size_t length = 1;
        char c = lexer->data[offset + length];
        while (offset + length < lexer->size && is_ident_char (c))
        {
            length += 1;
            c = lexer->data[offset + length];
        }

        tok.kind = MML_EXPANSION;
        tok.view.size = length;

        break;
    }

    case '!': {
        size_t length = 1;
        char c = lexer->data[offset + length];
        while (offset + length < lexer->size && is_ident_char (c) && !is_space (c) && c != '{')
        {
            length += 1;
            c = lexer->data[offset + length];
        }

        tok.kind = MML_DEFINITION;
        tok.view.size = length;

        break;
    }

    default: {
        if (is_digit (lexer->data[offset]))
        {
            size_t length = 0;
            char c = lexer->data[offset + length];
            while (offset + length < lexer->size && is_digit (c))
            {

                length += 1;
                c = lexer->data[offset + length];
            }

            tok.kind = MML_NUMBER;
            tok.view.size = length;

            break;
        }
    }
    }

    if (tok.kind == MML_UNKNOWN) { __asm ("int3"); }

    lexer->offset += tok.view.size;
    return tok;
}

token *
mml_tokenize (const char *source, size_t length)
{
    if (!source) return NULL;
    if (length == 0) length = strlen (source);

    token t;
    mmlexer lexer = {
        .data = source,
        .offset = 0,
        .size = length,
    };

    struct
    {
        token *items;
        size_t capacity, size;
    } tokens = { 0 };

    for (;;)
    {
        t = read_next_token (&lexer);
        da_append (&tokens, t);
        if (t.kind == MML_EOF) break;
    }

    return tokens.items;
}
