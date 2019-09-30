# customize below to fit your system

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/man
DOCPREFIX = ${PREFIX}/share/doc/json2tsv

# compiler and linker
CC = cc
AR = ar
RANLIB = ranlib

# use system flags.
JSON2TSV_CFLAGS = ${CFLAGS}
JSON2TSV_LDFLAGS = ${LDFLAGS}
JSON2TSV_CPPFLAGS = 
