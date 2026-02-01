// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 virtualgrub39

#include "mml2midi.h"
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <uchar.h>

#define MIDI_WRITER_IMPLEMENTATION
#include <midi-codec/midi-writer.h>
#define MIDI_PARSER_IMPLEMENTATION
#include <midi-codec/midi-parser.h>

static int
pitch_to_midi_note (char32_t pitch, uint8_t octave, int accidental)
{
    pitch = tolower (pitch);

    int base_note;
    switch (pitch)
    {
    case 'c': base_note = 0; break;
    case 'd': base_note = 2; break;
    case 'e': base_note = 4; break;
    case 'f': base_note = 5; break;
    case 'g': base_note = 7; break;
    case 'a': base_note = 9; break;
    case 'b': base_note = 11; break;
    case 'r': return -1;
    default: return -1;
    }

    int midi_note = (octave + 1) * 12 + base_note + accidental;

    if (midi_note < 0) midi_note = 0;
    if (midi_note > 127) midi_note = 127;

    return midi_note;
}

static uint32_t
calculate_duration (uint32_t length, uint32_t dots, uint32_t ticks_per_quarter)
{
    if (length == 0) return 0;

    uint32_t base_ticks = (4 * ticks_per_quarter) / length;

    uint32_t total_ticks = base_ticks;
    uint32_t dot_add = base_ticks;

    for (uint32_t i = 0; i < dots; i++)
    {
        dot_add /= 2;
        total_ticks += dot_add;
    }

    return total_ticks;
}

typedef struct
{
    midi_writer_t *mw;
    uint8_t last_status;
    const mml_sequence *events;
    size_t offset;

    uint32_t tempo_us;
    uint32_t ticks_per_quarter;
    size_t current_tick;
    uint32_t default_length;
    int octave;
    uint8_t velocity;
    uint8_t channel;

    bool active_notes[128];
} mml_context;

void
ctx_reset (mml_context *ctx, uint32_t ticks_per_quarter, uint8_t channel)
{
    ctx->ticks_per_quarter = ticks_per_quarter;
    ctx->current_tick = 0;
    ctx->default_length = 4; /* Quarter note */
    ctx->tempo_us = 500000;  /* 120 BPM */
    ctx->octave = 4;         /* Middle octave */
    ctx->velocity = 100;     /* Default velocity */
    ctx->channel = channel;  /* Assign channel */
}

int
write_tempo (midi_writer_t *mw, uint32_t delta, uint32_t tempo_us)
{
    const uint8_t data[] = {
        (tempo_us >> 16) & 0xff,
        (tempo_us >> 8) & 0xff,
        tempo_us & 0xff,
    };
    track_event_t ev = 
    {
        .kind = EV_META,
        .delta = delta,
        .as.meta = 
        {
            .type = 0x51,
            .length = 3,
            .data = data,
        },
    };

    uint8_t buffer[16];
    int result = track_event_to_bytes (&ev, buffer);
    if (result < 0) return -1;

    return mw_track_append (mw, buffer, result);
}

int
write_midi (mml_context *ctx, uint32_t delta, midi_event_t midiev)
{
    uint8_t buffer[16];
    uint8_t status = (midiev.kind << 4) | (midiev.channel & 0x0F);

    int n = midi_vlq_encode (delta, buffer);
    if (n < 0) return -1;

    int result = midi_event_to_bytes (&midiev, buffer + n, ctx->last_status == status);
    if (result < 0) return -1;

    mw_track_append (ctx->mw, buffer, n + result);

    ctx->last_status = status;
    return n + result;
}

int
write_end_of_track (midi_writer_t *mw, uint32_t delta)
{
    track_event_t ev = 
    {
        .kind = EV_META,
        .delta = delta,
        .as.meta = 
        {
            .type = 0x2F,
            .length = 0,
            .data = NULL,
        },
    };

    uint8_t buffer[16];
    int result = track_event_to_bytes (&ev, buffer);
    if (result < 0) return -1;

    return mw_track_append (mw, buffer, result);
}

void
process_control (mml_context *ctx, char32_t cmd, unsigned arg)
{
    switch (cmd)
    {
    case 't':
        ctx->tempo_us = 60000000 / arg;
        write_tempo (ctx->mw, 0, ctx->tempo_us);
        break;
    case 'o': ctx->octave = arg; break;
    case 'v': ctx->velocity = arg % 127; break;
    case 'l': ctx->default_length = arg; break;
    case '>': ctx->octave += 1; break;
    case '<': ctx->octave -= 1; break;
    default: abort ();
    }
}

typedef struct
{
    uint8_t midi_note;
    bool is_tied;
} chord_note_t;

uint32_t
process_track (mml_context *ctx)
{
    uint32_t last_tick = 0;

    memset (ctx->active_notes, 0, sizeof (ctx->active_notes));

    for (;;)
    {
        if (ctx->offset >= ctx->events->size) break;

        mml_event ev = ctx->events->items[ctx->offset];

        if (ev.kind != MML_EV_NOTE)
        {
            ctx->offset++;
            switch (ev.kind)
            {
            case MML_EV_EOT: return last_tick;
            case MML_EV_CTL: process_control (ctx, ev.as.ctl.cmd, ev.as.ctl.value); break;
            default: break;
            }
            continue;
        }

        chord_note_t batch[128];
        int batch_count = 0;
        uint32_t step_duration = 0;
        bool step_complete = false;

        while (!step_complete && ctx->offset < ctx->events->size)
        {
            mml_event *nev = &ctx->events->items[ctx->offset];
            if (nev->kind != MML_EV_NOTE) break;

            int note = pitch_to_midi_note (nev->as.note.pitch, ctx->octave, nev->as.note.acc);

            if (note >= 0)
            {
                batch[batch_count].midi_note = (uint8_t)note;
                batch[batch_count].is_tied = nev->as.note.tie;
                batch_count++;
            }

            if (!nev->as.note.chord_link)
            {
                if (nev->as.note.length == 0) nev->as.note.length = ctx->default_length;
                step_duration = calculate_duration (nev->as.note.length, nev->as.note.dots, ctx->ticks_per_quarter);
                step_complete = true;
            }
            ctx->offset++;
        }

        uint32_t delta = ctx->current_tick - last_tick;

        for (int i = 0; i < batch_count; i++)
        {
            uint8_t note = batch[i].midi_note;
            if (!ctx->active_notes[note])
            {
                midi_event_t mev = { .kind = MIDI_NOTE_ON,
                                     .channel = ctx->channel,
                                     .as.note_on = { .note = note, .velocity = ctx->velocity } };
                write_midi (ctx, (i == 0) ? delta : 0, mev);
                if (i == 0) delta = 0;
                ctx->active_notes[note] = true;
            }
        }

        if (batch_count > 0) { last_tick = ctx->current_tick; }

        ctx->current_tick += step_duration;

        bool first_off = true;
        uint32_t off_delta = ctx->current_tick - last_tick;

        for (int i = 0; i < batch_count; i++)
        {
            uint8_t note = batch[i].midi_note;
            bool is_tied = batch[i].is_tied;

            if (!is_tied)
            {
                midi_event_t mev
                    = { .kind = MIDI_NOTE_ON, .channel = ctx->channel, .as.note_on = { .note = note, .velocity = 0 } };

                write_midi (ctx, first_off ? off_delta : 0, mev);
                if (first_off)
                {
                    off_delta = 0;
                    last_tick = ctx->current_tick;
                    first_off = false;
                }
                ctx->active_notes[note] = false;
            }
        }
    }

    return last_tick;
}

int
mml_write_midi (const mml_sequence *events, const char *out_path)
{
    if (!events || !out_path) return -1;

    FILE *file = fopen (out_path, "wb");
    if (!file) return -1;

    midi_writer_t mw = { 0 };
    mml_context ctx = {
        .mw = &mw,
        .last_status = 0,
        .events = events,
        .offset = 0,
        .ticks_per_quarter = 480,
    };

    mw_begin (&mw, file, MIDI_FMT_MTRACK, ctx.ticks_per_quarter);

    for (;;)
    {
        if (ctx.offset >= ctx.events->size) break;

        ctx.current_tick = 0;
        ctx.default_length = 4;
        ctx.tempo_us = 500000;
        ctx.octave = 4;
        ctx.velocity = 100;
        ctx.last_status = 0;

        mw_track_begin (&mw);
        write_tempo (&mw, 0, ctx.tempo_us);

        uint32_t t = process_track (&ctx);

        write_end_of_track (&mw, t);
        mw_track_end (&mw);

        ctx.channel += 1;
        if (ctx.channel >= 16) break;
    }
    mw_end (&mw);
    fclose (file);
    return 0;
}
