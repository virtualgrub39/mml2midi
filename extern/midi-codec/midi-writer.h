#ifndef MIDI_WRITER_H
#define MIDI_WRITER_H

#define MIDI_FMT_SINGLE 0
#define MIDI_FMT_MTRACK 1
#define MIDI_FMT_MSONG 2

#include <stdint.h>
#include <stdio.h>

/* This structure MUST be zero-initialized before use */
typedef struct
{
    /* writer state */
    FILE *dst;  /* destination file */
    uint32_t i; /* current file offset */
    /* header info */
    uint16_t ntracks; /* count of `mw_track_begin` calls */
    /* track info */
    uint32_t track_offset; /* file offset to begining of event data section of current track */
    /* [ MTrk:4 ][ Track-len:4 ][ Track data:... ] */
    /*                          ^              */
    /* used for patching Track-len, after `mw_track_end` is called */
} midi_writer_t;

/* Initializes MIDI writer context; Tries to write MIDI header bytes;
 * On success, writes the MIDI header to file, with some placeholder data, and returns 0;
 * On failure (write failed, NULL argument), returns -1, without setting any error indicator; */
int mw_begin (midi_writer_t *mw, FILE *dst, uint16_t format, uint16_t tickdiv);

/* Filnalizes MIDI file, by updating the placeholder data in the MIDI header.
 * This function does not end current track, nor checks if the MIDI header has been written, so make sure appropriate
 * functions have been called before calling this function;
 * On success, updates MIDI header placeholders with true values, and returns 0;
 * On failure (seeking or write failed), returns -1, without setting any error indicator; */
int mw_end (midi_writer_t *mw);

/* Begins new MIDI track, by appending track header.
 * This function does not end previous track, nor checks if the MIDI header has been written, so make sure appropriate
 * functions have been called before calling this function;
 * On success, writes MIDI track header, and returns 0;
 * On failure (NULL-argument, write failed) returns -1, without setting any error indicator; */
int mw_track_begin (midi_writer_t *mw);

/* Appends data from buffer into track, as event data.
 * This function appends the raw data from buffer into MIDI track, treating the data as event data;
 * This function does not perform any event format validation, so it can be used to append any arbitrary data;
 * This function does not begin new track, nor checks if the MIDI header or track have been written, so make sure
 * appropriate functions have been called before calling this function;
 * On success appends data to the current track, and returns 0;
 * On failure (NULL-argument, write failed) returns -1 without setting any error indicator; */
int mw_track_append (midi_writer_t *mw, const uint8_t *data, uint32_t len);

/* Finalizes current track, by filling out placeholder data in previous track header;
 * This function does not check, if the track header exists, nor if MIDI header has been written, so make sure
 * appropriate functions have been called before calling this function;
 * On success, updates track header placeholders with true values, and returns 0;
 * On failure (seeking or write failed), returns -1, without setting any error indicator; */
int mw_track_end (midi_writer_t *mw);

#ifdef MIDI_WRITER_H

static int
_mw_write_u32 (midi_writer_t *mw, uint32_t u32)
{
    uint8_t b[4];
    uint32_t n;

    b[0] = u32 >> 24;
    b[1] = u32 >> 16;
    b[2] = u32 >> 8;
    b[3] = u32;

    n = fwrite (b, sizeof *b, 4, mw->dst);
    mw->i += n;
    return n - 4;
}

static int
_mw_write_u16 (midi_writer_t *mw, uint16_t u16)
{
    uint8_t b[2];
    uint32_t n;

    b[0] = u16 >> 8;
    b[1] = u16;

    n = fwrite (b, sizeof *b, 2, mw->dst);
    mw->i += n;
    return n - 2;
}

int
mw_begin (midi_writer_t *mw, FILE *dst, uint16_t format, uint16_t tickdiv)
{
    if (!mw) return -1;
    if (!dst) return -1;

    mw->dst = dst;
    mw->i = 0;
    mw->ntracks = 0;

    if (_mw_write_u32 (mw, 0x4d546864) != 0) return -1; /* magic */
    if (_mw_write_u32 (mw, 6) != 0) return -1;          /* header length */
    if (_mw_write_u16 (mw, format) != 0) return -1;     /* header length */
    if (_mw_write_u16 (mw, 0xAFAF) != 0) return -1;     /* ntracks (placeholder) */
    if (_mw_write_u16 (mw, tickdiv) != 0) return -1;    /* tickdiv */

    return 0;
}

int
mw_track_begin (midi_writer_t *mw)
{
    if (!mw) return -1;

    if (_mw_write_u32 (mw, 0x4d54726b) != 0) return -1; /* magic */
    if (_mw_write_u32 (mw, 0xFAFAFAFA) != 0) return -1; /* track_len (placeholder) */

    mw->track_offset = mw->i;

    return 0;
}

int
mw_track_append (midi_writer_t *mw, const uint8_t *data, uint32_t len)
{
    if (!mw) return -1;
    if (!data || len == 0) return -1;

    if (fwrite (data, sizeof *data, len, mw->dst) != len)
    {
        fseek (mw->dst, mw->i, SEEK_SET);
        return -1;
    }
    mw->i += len;

    return 0;
}

int
mw_track_end (midi_writer_t *mw)
{
    uint32_t track_len;
    size_t saved_i;

    if (!mw) return -1;

    saved_i = mw->i;

    if (fseek (mw->dst, mw->track_offset - 4, SEEK_SET) != 0) return -1;

    track_len = saved_i - mw->track_offset;

    if (_mw_write_u32 (mw, track_len) != 0)
    {
        fseek (mw->dst, saved_i, SEEK_SET);
        return -1;
    }

    mw->i = saved_i;
    if (fseek (mw->dst, saved_i, SEEK_SET) != 0) return -1;
    mw->ntracks += 1;

    return 0;
}

int
mw_end (midi_writer_t *mw)
{
    if (!mw) return -1;
    if (mw->dst)
    {
        fseek (mw->dst, 10, SEEK_SET);
        if (_mw_write_u16 (mw, mw->ntracks) != 0) return -1;
    }
    return 0;
}

#endif /* implementation */

#endif /* include guard */
