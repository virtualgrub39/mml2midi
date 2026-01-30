// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 virtualgrub39

#include "mml2midi.h"

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    string_view name;
    mml_sequence *body;
} macro;

typedef struct
{
    macro *items;
    size_t size, capacity;
} macross;

typedef struct
{
    const token *tokens;
    size_t idx;
    mml_sequence *out_sequence;
    macross macro_table;
} parser_context;

macro *
macro_search (parser_context *ctx, string_view name)
{
    for (size_t i = 0; i < ctx->macro_table.size; ++i)
    {
        string_view curr = ctx->macro_table.items[i].name;
        if (curr.size == name.size && memcmp (curr.data, name.data, name.size) == 0) return &ctx->macro_table.items[i];
    }

    return NULL;
}

token
advance (parser_context *ctx)
{
    return ctx->tokens[ctx->idx++];
}

bool
expect (parser_context *ctx, token_kind kind)
{
    if (ctx->tokens[ctx->idx].kind != kind) return false;
    advance (ctx);
    return true;
}

token_kind
peek_kind (const parser_context *ctx)
{
    return ctx->tokens[ctx->idx].kind;
}

bool
parse_expansion (parser_context *ctx)
{
    if (peek_kind (ctx) != MML_EXPANSION) return false;

    token def = advance (ctx);
    string_view ident = (string_view){ .data = def.view.data + 1, .size = def.view.size - 1 };
    if (ident.size == 0)
    {
        fprintf (stderr, "mml: expected identifier after '@'\n");
        abort ();
    }

    macro *m = macro_search (ctx, ident);
    if (!m)
    {
        fprintf (stderr, "mml: macro `%.*s` is not defined\n", (int)ident.size, ident.data);
        abort ();
    }

    da_append_many (ctx->out_sequence, m->body->items, m->body->size);

    return true;
}

bool
parse_note (parser_context *ctx)
{
    if (peek_kind (ctx) != MML_NOTE) return false;

    token note = advance (ctx);
    char pitch = note.view.data[0]; // THIS LIMITS PITCH TO ASCII!!!

    /* accidental */
    int acc = 0;

    if (peek_kind (ctx) == MML_PLUS)
    {
        acc += 1;
        advance (ctx);
    }
    else if (peek_kind (ctx) == MML_MINUS)
    {
        acc -= 1;
        advance (ctx);
    }

    /* length */
    unsigned length = 0;

    if (peek_kind (ctx) == MML_NUMBER)
    {
        token lentok = advance (ctx);
        char buffer[32];
        memcpy (buffer, lentok.view.data, lentok.view.size);
        buffer[lentok.view.size] = 0;
        char *endptr = NULL;
        length = strtoul (buffer, &endptr, 10);
        assert (*endptr == 0); // tokenizer failed, if false.
    }

    /* dots */
    unsigned dots = 0;

    for (;;)
    {
        if (peek_kind (ctx) != MML_DOT) break;
        dots++;
        advance (ctx);
    }

    mml_event ev =
    {
        .kind = MML_EV_NOTE,
        .as.note = 
        {
            .pitch = pitch,
            .acc = acc,
            .length = length,
            .dots = dots,
        },
    };

    da_append (ctx->out_sequence, ev);

    return true;
}

bool
parse_command (parser_context *ctx)
{
    if (peek_kind (ctx) != MML_COMMAND) return false;

    token cmdtok = advance (ctx);

    char cmd = cmdtok.view.data[0]; // this limits commands to ASCII !!!

    /* numerical argument */
    unsigned arg = 0;
    if (peek_kind (ctx) == MML_NUMBER)
    {
        token argtok = advance (ctx);

        char buffer[32];
        memcpy (buffer, argtok.view.data, argtok.view.size);
        buffer[argtok.view.size] = 0;

        char *endptr = NULL;
        arg = strtoul (buffer, &endptr, 10);
        assert (*endptr == 0); // tokenizer failed, if false.
    }

    mml_event ev = {
        .kind = MML_EV_CTL,
        .as.ctl = {
            .cmd = cmd,
            .value = arg,
        },
    };

    da_append (ctx->out_sequence, ev);

    return true;
}

bool parse_action (parser_context *ctx);

bool
parse_loop (parser_context *ctx)
{
    if (peek_kind (ctx) != MML_LBRACKET) return false;
    advance (ctx);

    mml_sequence *parent_seq = ctx->out_sequence;

    mml_sequence *loop_seq = calloc (1, sizeof (mml_sequence));
    ctx->out_sequence = loop_seq;

    for (;;)
    {
        token_kind kind = peek_kind (ctx);
        if (kind == MML_EOF || kind == MML_RBRACKET || kind == MML_COLON) break;

        if (!parse_action (ctx))
        {
            token t = advance (ctx);
            fprintf (stderr, "mml: Unexpected token in loop body: `%.*s` (%d)\n", (int)t.view.size, t.view.data,
                     t.kind);
            abort ();
        }
    }

    mml_sequence *break_seq = calloc (1, sizeof (mml_sequence));

    if (peek_kind (ctx) == MML_COLON)
    {
        advance (ctx);
        ctx->out_sequence = break_seq;

        for (;;)
        {
            token_kind kind = peek_kind (ctx);
            if (kind == MML_EOF || kind == MML_RBRACKET) break;

            if (!parse_action (ctx))
            {
                token t = advance (ctx);
                fprintf (stderr, "mml: Unexpected token in loop break body: `%.*s` (%d)\n", (int)t.view.size,
                         t.view.data, t.kind);
                abort ();
            }
        }
    }

    ctx->out_sequence = parent_seq;

    if (!expect (ctx, MML_RBRACKET))
    {
        fprintf (stderr, "mml: expected closing bracket ']'\n");
        abort ();
    }

    if (peek_kind (ctx) != MML_NUMBER)
    {
        fprintf (stderr, "mml: expected number after loop body\n");
        abort ();
    }

    token itok = advance (ctx);

    char buffer[32];
    memcpy (buffer, itok.view.data, itok.view.size);
    buffer[itok.view.size] = 0;

    char *endptr = NULL;
    unsigned loopi = strtoul (buffer, &endptr, 10);
    assert (*endptr == 0); // tokenizer failed, if false.

    for (unsigned i = 0; i < loopi - 1; ++i)
    {
        da_append_many (parent_seq, loop_seq->items, loop_seq->size);
        if (break_seq->size > 0) da_append_many (parent_seq, break_seq->items, break_seq->size);
    }
    da_append_many (parent_seq, loop_seq->items, loop_seq->size);

    return true;
}

bool
parse_action (parser_context *ctx)
{
    if (peek_kind (ctx) == MML_EOF) return false;
    if (parse_note (ctx)) return true;
    if (parse_command (ctx)) return true;
    if (parse_loop (ctx)) return true;
    if (parse_expansion (ctx)) return true;
    return false;
}

bool
parse_definition (parser_context *ctx)
{
    if (peek_kind (ctx) != MML_DEFINITION) return false;

    token def = advance (ctx);
    string_view ident = (string_view){ .data = def.view.data + 1, .size = def.view.size - 1 };
    if (ident.size == 0)
    {
        fprintf (stderr, "mml: expected identifier after '@'\n");
        abort ();
    }

    if (!expect (ctx, MML_LBRACE))
    {
        fprintf (stderr, "mml: expected '{' after DEFINITION\n");
        abort ();
    }

    mml_sequence *parent_seq = ctx->out_sequence;

    mml_sequence *macro_seq = malloc (sizeof (mml_sequence));
    ctx->out_sequence = macro_seq;

    for (;;)
    {
        token_kind kind = peek_kind (ctx);
        if (kind == MML_EOF || kind == MML_RBRACE) break;

        if (!parse_action (ctx))
        {
            fprintf (stderr, "mml: Unexpected token in definition body\n");
            abort ();
        }
    }

    ctx->out_sequence = parent_seq;

    if (!expect (ctx, MML_RBRACE))
    {
        fprintf (stderr, "mml: expected closing brace '}'\n");
        abort ();
    }

    if (macro_seq->size == 0)

    {
        printf ("mml: warning: empty definition `%.*s`\n", (int)ident.size, ident.data);
        return true;
    }

    // printf ("mml: parsed definition `%.*s`, with length of %zu\n", (int)ident.size, ident.data, macro_seq->size);

    macro m = { .name = ident, .body = macro_seq };
    da_append (&ctx->macro_table, m);

    return true;
}

void
parse_track (parser_context *ctx)
{
    for (;;)
    {
        token_kind kind = peek_kind (ctx);
        switch (kind)
        {
        case MML_EOF:
        case MML_SCOLON: return;
        case MML_DEFINITION: parse_definition (ctx); break;
        default:
            if (!parse_action (ctx))
            {
                token t = advance (ctx);
                fprintf (stderr, "mml: Unexpected token in track body: `%.*s` (%d)\n", (int)t.view.size, t.view.data,
                         t.kind);
                abort ();
            }
            break;
        }
    }
}

int
mml_parse (const token *tokens, mml_sequence *out_sequence)
{
    if (!tokens || (*tokens).kind == MML_EOF)
    {
        errno = EINVAL;
        return -1;
    }

    parser_context ctx = { tokens, 0, out_sequence, .macro_table = { 0 } };

    for (;;)
    {
        token_kind kind = peek_kind (&ctx);
        switch (kind)
        {
        case MML_EOF: return 0;
        case MML_SCOLON: {
            mml_event ev = { .kind = MML_EV_EOT };
            da_append (out_sequence, ev);
            advance (&ctx);
            break;
        }
        default: parse_track (&ctx); break;
        }
    }

    return 0;
}
