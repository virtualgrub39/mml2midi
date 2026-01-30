// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 virtualgrub39

#include "mml2midi.h"

#include <stdio.h>
#include <stdlib.h>

char *
mml_read_all (const char *path)
{
    if (path == NULL) return NULL;

    FILE *mmlf = fopen (path, "rb");
    if (!mmlf)
    {
        perror ("mml: Failed to open file for reading");
        return NULL;
    }

    fseek (mmlf, 0, SEEK_END);
    long len = ftell (mmlf);
    fseek (mmlf, 0, SEEK_SET);

    char *bytes = malloc (len * sizeof *bytes + 1);

    if (fread (bytes, sizeof *bytes, len, mmlf) != len)
    {
        perror ("mml: Failed to read from file");
        free (bytes);
        return NULL;
    }

    bytes[len] = 0;

    return bytes;
}
