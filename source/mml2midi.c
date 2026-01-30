// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 virtualgrub39

#include "mml2midi.h"

#include <stdio.h>

int
main (int argc, char *argv[])
{
    if (argc < 3) return 1;

    char *source = mml_read_all (argv[1]);
    if (!source) return 2;
    token *tokens = mml_tokenize (source, 0);
    if (!tokens) return 3;
    // token *t = tokens;

    // while (t->kind != MML_EOF)
    // {
    //     printf ("[%.*s]\n", (int)t->view.size, t->view.data);
    //     t++;
    // }

    mml_sequence sequence = {};
    mml_parse (tokens, &sequence);
    printf ("sequence.len: %zu\n", sequence.size);

    for (size_t i = 0; i < sequence.size; ++i)
    {
        mml_event ev = sequence.items[i];
        switch (ev.kind)
        {
        case MML_EV_NOTE:
            printf ("NOTE %c {", ev.as.note.pitch);
            printf ("%d %d %d", ev.as.note.length, ev.as.note.acc, ev.as.note.dots);
            printf ("};\n");
            break;
        case MML_EV_CTL: printf ("CTL %c {%d}\n", ev.as.ctl.cmd, ev.as.ctl.value); break;
        case MML_EV_EOT: printf ("END OF TRACK\n"); break;
        }
    }

    free (tokens);
    free (source);

    return 0;
}
