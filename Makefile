.POSIX:

NAME = json2tsv
VERSION = 0.3

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/man
DOCPREFIX = ${PREFIX}/share/doc/${NAME}

RANLIB = ranlib

BIN = ${NAME}
SRC = ${BIN:=.c}
HDR = json.h
MAN1 = ${BIN:=.1}
DOC = \
	LICENSE\
	README

LIBJSON = libjson.a
LIBJSONSRC = json.c
LIBJSONOBJ = ${LIBJSONSRC:.c=.o}

LIB = ${LIBJSON}

all: ${BIN}

${BIN}: ${LIB} ${@:=.o}

OBJ = ${SRC:.c=.o} ${LIBJSONOBJ}

${OBJ}: ${HDR}

.o:
	${CC} ${LDFLAGS} -o $@ $< ${LIB}

.c.o:
	${CC} ${CFLAGS} ${CPPFLAGS} -o $@ -c $<

${LIBJSON}: ${LIBJSONOBJ}
	${AR} rc $@ $?
	${RANLIB} $@

dist:
	rm -rf "${NAME}-${VERSION}"
	mkdir -p "${NAME}-${VERSION}"
	cp -f ${MAN1} ${DOC} ${HDR} \
		${SRC} ${LIBJSONSRC} Makefile "${NAME}-${VERSION}"
	# make tarball
	tar -cf - "${NAME}-${VERSION}" | gzip -c > "${NAME}-${VERSION}.tar.gz"
	rm -rf "${NAME}-${VERSION}"

clean:
	rm -f ${BIN} ${OBJ} ${LIB}

install: all
	# installing executable files.
	mkdir -p "${DESTDIR}${PREFIX}/bin"
	cp -f ${BIN} "${DESTDIR}${PREFIX}/bin"
	for f in ${BIN}; do chmod 755 "${DESTDIR}${PREFIX}/bin/$$f"; done
	# installing example files.
	mkdir -p "${DESTDIR}${DOCPREFIX}"
	cp -f README "${DESTDIR}${DOCPREFIX}"
	# installing manual pages for general commands: section 1.
	mkdir -p "${DESTDIR}${MANPREFIX}/man1"
	cp -f ${MAN1} "${DESTDIR}${MANPREFIX}/man1"
	for m in ${MAN1}; do chmod 644 "${DESTDIR}${MANPREFIX}/man1/$$m"; done

uninstall:
	# removing executable files.
	for f in ${BIN}; do rm -f "${DESTDIR}${PREFIX}/bin/$$f"; done
	# removing example files.
	rm -f "${DESTDIR}${DOCPREFIX}/README"
	-rmdir "${DESTDIR}${DOCPREFIX}"
	# removing manual pages.
	for m in ${MAN1}; do rm -f "${DESTDIR}${MANPREFIX}/man1/$$m"; done

.PHONY: all clean dist install uninstall
