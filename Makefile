INCLUDE = -I./include
LIBS=$(wildcard lib/*)
MACOS_VER=10.15


cflags += -std=c99 -ftree-vectorize -mmacosx-version-min=$(MACOS_VER) $(INCLUDE)
ldflags += -lz $(LIBS)

lib.name = sql3

class.sources := sql3.c

datafiles = help-sql3.pd

include Makefile.pdlibbuilder


