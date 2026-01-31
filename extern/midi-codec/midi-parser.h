#ifndef MIDI_PARSER_H
#define MIDI_PARSER_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MIDI_NOTE_OFF 0x8
#define MIDI_NOTE_ON 0x9
#define MIDI_POLY_PRESSURE 0xA
#define MIDI_CONTROLLER 0xB
#define MIDI_PROGRAM 0xC
#define MIDI_CHAN_PRESSURE 0xD
#define MIDI_PITCH_BEND 0xE

/* This structure MUST be zero-initialized before use */
typedef struct
{
    /* could be bitfielded together, but the ordering is undefined... */
    uint8_t kind;
    uint8_t channel;

    union
    {
        struct
        {
            uint8_t note, velocity;
        } note_on, note_off;
        struct
        {
            uint8_t note, pressure;
        } poly_pressure;
        struct
        {
            uint8_t controller, value;
        } controller;
        uint8_t program;
        uint8_t chan_pressure;
        uint16_t pitch_bend;
        uint8_t bytes[2];
    } as;
} midi_event_t;

/* This structure MUST be zero-initialized before use */

enum midi_event_kind
{
    EV_MIDI,
    EV_SYSEX,
    EV_META
};

typedef struct
{
    enum midi_event_kind kind;
    uint32_t delta;

    union
    {
        midi_event_t midi;
        struct
        {
            uint32_t length;
            const uint8_t *data;
        } sysex;
        struct
        {
            uint8_t type;
            uint32_t length;
            const uint8_t *data;
        } meta;
    } as;
} track_event_t;

typedef struct
{
    const uint8_t *bytes;
    uint32_t idx, len;
    uint8_t last_status;
} track_parser_t;

int midi_vlq_encode (uint32_t value, uint8_t *out_bytes);
int midi_vlq_decode (const uint8_t *bytes, uint32_t len, uint32_t *out_value);

int midi_event_to_bytes (const midi_event_t *e, uint8_t *out_bytes, int rolling);
int midi_event_from_bytes (midi_event_t *e, const uint8_t *bytes, uint32_t len);

uint32_t track_event_get_storage_size (const track_event_t *e);
int track_event_to_bytes (const track_event_t *e, uint8_t *out_bytes);
int track_event_next (track_parser_t *p, track_event_t *e);

#define MIDI_PARSER_IMPLEMENTATION
#ifdef MIDI_PARSER_IMPLEMENTATION

uint32_t
track_event_get_storage_size (const track_event_t *e)
{
    uint32_t total = 0;

    if (e == NULL) return 0;

    total += midi_vlq_encode (e->delta, NULL); /* delta */
    total += 1;                                /* status */

    switch (e->kind)
    {
    case EV_MIDI:
        switch (e->as.midi.kind)
        {
        case MIDI_NOTE_ON:
        case MIDI_NOTE_OFF:
        case MIDI_POLY_PRESSURE:
        case MIDI_CONTROLLER:
        case MIDI_PITCH_BEND: total += 2; break;
        case MIDI_PROGRAM:
        case MIDI_CHAN_PRESSURE: total += 1; break;
        }
        break;
    case EV_META:
        total += 1;                                         /* type */
        total += midi_vlq_encode (e->as.meta.length, NULL); /* length */
        total += e->as.meta.length;                         /* data */
        break;
    case EV_SYSEX:
        total += midi_vlq_encode (e->as.sysex.length, NULL); /* length */
        total += e->as.sysex.length;                         /* data */
        total += 1;                                          /* 0xF7 */
        break;
    }

    return total;
}

int
midi_event_to_bytes (const midi_event_t *e, uint8_t *out_bytes, int rolling)
{
    int ev_len = 0, i;
    uint8_t ev_bytes[3] = { 0 };

    if (e == NULL || out_bytes == NULL) return -1;

    if (!rolling)
    {
        ev_bytes[0] |= e->kind << 4;
        ev_bytes[0] |= e->channel;
        ev_len += 1;
    }

    switch (e->kind)
    {
    case MIDI_NOTE_ON:
    case MIDI_NOTE_OFF:
    case MIDI_POLY_PRESSURE:
    case MIDI_CONTROLLER:
        ev_bytes[ev_len] = e->as.bytes[0];
        ev_bytes[ev_len + 1] = e->as.bytes[1];
        ev_len += 2;
        break;
    case MIDI_PROGRAM:
    case MIDI_CHAN_PRESSURE:
        ev_bytes[ev_len] = e->as.bytes[0];
        ev_len += 1;
        break;
    case MIDI_PITCH_BEND:
        ev_bytes[ev_len] = e->as.pitch_bend;
        ev_bytes[ev_len + 1] = (e->as.pitch_bend >> 7);
        ev_len += 2;
        break;
    default: return -1;
    }

    for (i = 0; i < ev_len; ++i) out_bytes[i] = ev_bytes[i];
    return ev_len;
}

int
midi_vlq_encode (uint32_t value, uint8_t *out_bytes)
{
    int i = 0;
    uint8_t devnull[5];

    if (out_bytes == NULL) out_bytes = devnull;

    if (value >= (1U << 28)) { out_bytes[i++] = ((value >> 28) & 0x7F) | 0x80; }
    if (value >= (1U << 21)) { out_bytes[i++] = ((value >> 21) & 0x7F) | 0x80; }
    if (value >= (1U << 14)) { out_bytes[i++] = ((value >> 14) & 0x7F) | 0x80; }
    if (value >= (1U << 7)) { out_bytes[i++] = ((value >> 7) & 0x7F) | 0x80; }
    out_bytes[i++] = (value & 0x7F);

    return i;
}

int
midi_vlq_decode (const uint8_t *bytes, uint32_t len, uint32_t *out_value)
{
    uint32_t value = 0;
    uint32_t i;
    uint8_t b;

    if (bytes == NULL || out_value == NULL) return -1;
    if (len == 0) return -1;

    for (i = 0; i < 5 && i < len; ++i)
    {
        b = bytes[i];
        value = (value << 7) | (b & 0x7F);

        if ((b & 0x80) == 0)
        {
            *out_value = value;
            return i + 1;
        }
    }

    return -1;
}

int
track_event_to_bytes (const track_event_t *e, uint8_t *out_bytes)
{
    int n = 0, m;

    if (out_bytes == NULL || e == NULL) return -1;

    m = midi_vlq_encode (e->delta, out_bytes + n);
    if (m <= 0) return -1;
    n += m;

    switch (e->kind)
    {
    case EV_MIDI: return midi_event_to_bytes (&e->as.midi, out_bytes + n, 0) + n;
    case EV_META:
        out_bytes[n++] = 0xFF;
        out_bytes[n++] = e->as.meta.type;
        m = midi_vlq_encode (e->as.meta.length, out_bytes + n);
        if (m <= 0) return -1;
        n += m;
        memcpy (out_bytes + n, e->as.meta.data, e->as.meta.length);
        n += e->as.meta.length;
        break;
    case EV_SYSEX:
        out_bytes[n++] = 0xF0;
        m = midi_vlq_encode (e->as.sysex.length, out_bytes + n);
        if (m <= 0) return -1;
        n += m;
        memcpy (out_bytes + n, e->as.sysex.data, e->as.sysex.length);
        n += e->as.sysex.length;
        break;
    }

    return n;
}

int
midi_event_from_bytes (midi_event_t *e, const uint8_t *bytes, uint32_t len)
{
    uint8_t kind, chan;
    int bytes_used = 1;

    if (e == NULL || bytes == NULL) return -1;
    if (len < 2) return -1;

    kind = (bytes[0] & 0xF0) >> 4;
    chan = bytes[0] & 0x0F;

    switch (kind)
    {
    case MIDI_NOTE_ON:
    case MIDI_NOTE_OFF:
    case MIDI_POLY_PRESSURE:
    case MIDI_CONTROLLER:
        if (len < 3) return -1;
        e->as.bytes[0] = bytes[1];
        e->as.bytes[1] = bytes[2];
        bytes_used += 2;
        break;
    case MIDI_PROGRAM:
    case MIDI_CHAN_PRESSURE:
        e->as.bytes[0] = bytes[1];
        bytes_used += 1;
        break;
    case MIDI_PITCH_BEND:
        if (len < 3) return -1;
        e->as.pitch_bend = bytes[1];
        e->as.pitch_bend |= bytes[2] << 7;
        bytes_used += 2;
        break;
    }

    e->kind = kind;
    e->channel = chan;

    return bytes_used;
}

int
midi_event_from_bytes_rolling (midi_event_t *e, uint8_t status, const uint8_t *bytes, uint32_t len)
{
    uint8_t kind, chan;
    int bytes_used = 0;

    if (e == NULL || bytes == NULL) return -1;
    if (len == 0) return -1;

    kind = (status & 0xF0) >> 4;
    chan = status & 0x0F;

    switch (kind)
    {
    case MIDI_NOTE_ON:
    case MIDI_NOTE_OFF:
    case MIDI_POLY_PRESSURE:
    case MIDI_CONTROLLER:
        if (len < 2) return -1;
        e->as.bytes[0] = bytes[0];
        e->as.bytes[1] = bytes[1];
        bytes_used += 2;
        break;
    case MIDI_PROGRAM:
    case MIDI_CHAN_PRESSURE:
        e->as.bytes[0] = bytes[0];
        bytes_used += 1;
        break;
    case MIDI_PITCH_BEND:
        if (len < 2) return -1;
        e->as.pitch_bend = bytes[0];
        e->as.pitch_bend |= bytes[1] << 7;
        bytes_used += 2;
        break;
    }

    e->kind = kind;
    e->channel = chan;

    return bytes_used;
}

int
track_event_next (track_parser_t *p, track_event_t *e)
{
    uint32_t delta;
    uint32_t bytes_left = 0;
    int32_t ev_len = 0, n = 0;
    uint8_t b;

    if (p == NULL || e == NULL) return -1;

    if ((n = midi_vlq_decode (p->bytes + p->idx, p->len - p->idx, &delta)) <= 0) return -1;

    p->idx += n;
    e->delta = delta;

    bytes_left = p->len - p->idx;
    if (bytes_left == 0) return -1;

    b = p->bytes[p->idx];

    if (b >= 0x80 && b < 0xF0) /* MIDI */
    {
        if ((ev_len = midi_event_from_bytes (&e->as.midi, (const uint8_t *)p->bytes + p->idx, bytes_left)) <= 0)
            return -1;
        e->kind = EV_MIDI;
        p->last_status = b;
    }
    else if (b == 0xF0 || b == 0xF7) /* SYSEX */
    {
        uint32_t vlength;
        if ((n = midi_vlq_decode (p->bytes + p->idx + 1, p->len - p->idx - 1, &vlength)) <= 0) return -1;

        e->kind = EV_SYSEX;
        e->as.sysex.data = (const uint8_t *)p->bytes + p->idx + 1 + n;
        e->as.sysex.length = vlength - 1;

        ev_len = 1 + n + vlength;
    }
    else if (b == 0xFF) /* META */
    {
        uint8_t type = p->bytes[p->idx + 1];
        uint32_t vlength;
        if ((n = midi_vlq_decode (p->bytes + p->idx + 2, p->len - p->idx - 2, &vlength)) <= 0) return -1;

        e->kind = EV_META;
        e->as.meta.type = type;
        e->as.meta.data = (const uint8_t *)p->bytes + p->idx + 2 + n;
        e->as.meta.length = vlength;

        ev_len = 2 + n + vlength;
    }
    else
    {
        if (p->last_status >= 0x80 && p->last_status < 0xF0) /* rolling status */
        {
            ev_len = midi_event_from_bytes_rolling (&e->as.midi, p->last_status, (const uint8_t *)p->bytes + p->idx,
                                                    bytes_left);
            if (ev_len <= 0) return -1;
            e->kind = EV_MIDI;
        }
        else
            return -1;
    }

    p->idx += ev_len;

    return ev_len;
}

#endif /* implementation */

#endif /* include guard */
