INCLUDE = -I./include
MACOS_VER=10.15


cflags += -std=c++11 -mmacosx-version-min=$(MACOS_VER) $(INCLUDE)
ldflags += -lz ../lib/libsqlite3.a lib/libSQLiteCpp.a

lib.name = sql3

class.sources := sql3.cpp

datafiles = help-sql3.pd

include ../Makefile.pdlibbuilder


