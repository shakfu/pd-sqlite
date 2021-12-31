INCLUDE = -I../../include
LIBS=$(wildcard ../../lib/*)
MACOS_VER=10.15


cflags += -std=c99 -ftree-vectorize -mmacosx-version-min=$(MACOS_VER) $(INCLUDE)
ldflags += -lz $(LIBS)

lib.name = sqlite3db

class.sources := sqlite3db.c

datafiles = $(wildcard *.pd)

PDLIBBUILDER_DIR=../../pd-lib-builder
include $(PDLIBBUILDER_DIR)/Makefile.pdlibbuilder


