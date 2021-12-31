INCLUDE = -I./include
LIBS=$(wildcard lib/*)
MACOS_VER=10.15


cflags += -std=c99 -ftree-vectorize -mmacosx-version-min=$(MACOS_VER) $(INCLUDE)
ldflags += -lz $(LIBS)

lib.name = sql

class.sources := sql.c

datafiles = help-sql.pd

PDLIBBUILDER_DIR=pd-lib-builder
include $(PDLIBBUILDER_DIR)/Makefile.pdlibbuilder


