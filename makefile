# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 virtualgrub39

CFLAGS += -Wall -Wextra
CFLAGS += -std=c23
# CFLAGS += -O2
CFLAGS += -ggdb
CFLAGS += -Iextern

all: mml2midi

lexer.o: source/mml-lexer.c source/mml2midi.h
	$(CC) -c -o $@ $(CFLAGS) $<

reader.o: source/mml-reader.c source/mml2midi.h
	$(CC) -c -o $@ $(CFLAGS) $<

parser.o: source/mml-parser.c source/mml2midi.h
	$(CC) -c -o $@ $(CFLAGS) $<

mml2midi: lexer.o reader.o parser.o source/mml2midi.c
	$(CC) -o $@ $(CFLAGS) $^
