// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 virtualgrub39

#ifndef MML2MIDI_H
#define MML2MIDI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <uchar.h>

#define DA_INIT_CAPACITY 32

#define da_reserve(da, new_cap)                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((new_cap) > (da)->capacity)                                                                                \
        {                                                                                                              \
            if ((da)->capacity == 0) (da)->capacity = DA_INIT_CAPACITY;                                                \
            while ((new_cap) > (da)->capacity) (da)->capacity *= 2;                                                    \
            (da)->items = realloc ((da)->items, (da)->capacity * sizeof (*(da)->items));                               \
        }                                                                                                              \
    } while (0)

#define da_append(da, item)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        da_reserve ((da), (da)->size + 1);                                                                             \
        (da)->items[(da)->size++] = (item);                                                                            \
    } while (0)

#define da_append_many(da, new_items, count)                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        da_reserve ((da), (da)->size + (count));                                                                       \
        memcpy ((da)->items + (da)->size, (new_items), (count) * sizeof (*(da)->items));                               \
        (da)->size += (count);                                                                                         \
    } while (0)

typedef struct
{
    const char *data;
    size_t size;
} string_view;

typedef enum
{
    MML_NUMBER,
    MML_EXPANSION,
    MML_DEFINITION,
    MML_COMMAND,
    MML_NOTE,
    MML_PLUS,
    MML_MINUS,
    MML_DOT,
    MML_SCOLON,
    MML_LBRACKET,
    MML_RBRACKET,
    MML_COLON,
    MML_LBRACE,
    MML_RBRACE,
    MML_UNKNOWN,
    MML_EOF,
} token_kind;

typedef struct
{
    token_kind kind;
    string_view view;
} token;

typedef enum
{
    MML_EV_NOTE,
    MML_EV_CTL,
    MML_EV_EOT,
} mml_event_kind;

typedef struct
{
    mml_event_kind kind;

    union
    {
        struct
        {
            char32_t pitch;
            unsigned length; // 0 = default
            unsigned dots;   // n. of dots
            int acc;         // +1 = sharp; -1 = flat
            // bool tie;
        } note;
        struct
        {
            char32_t cmd;
            unsigned value; // 0 = not specified
        } ctl;
    } as;
} mml_event;

typedef struct
{
    mml_event *items;
    size_t size, capacity;
} mml_sequence;

char *mml_read_all (const char *path);
token *mml_tokenize (const char *source, size_t length);
int mml_parse (const token *tokens, mml_sequence *out_sequence);
int mml_write_midi (const mml_sequence *events, const char *out_path);

#endif
