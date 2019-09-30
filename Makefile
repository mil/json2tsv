.POSIX:

include config.mk

NAME = json2tsv
VERSION = 0.1

BIN = json2tsv

SRC = ${BIN:=.c}

MAN1 = ${BIN:=.1}

DOC = \
	LICENSE\
	README

all: ${BIN}

${BIN}: ${@:=.o}

OBJ = ${SRC:.c=.o}

${OBJ}: config.mk

.o:
	${CC} ${JSON2TSV_LDFLAGS} -o $@ $<

.c.o:
	${CC} ${JSON2TSV_CFLAGS} ${JSON2TSV_CPPFLAGS} -o $@ -c $<

dist:
	rm -rf "${NAME}-${VERSION}"
	mkdir -p "${NAME}-${VERSION}"
	cp -f ${MAN1} ${DOC} ${SRC} Makefile config.mk \
		"${NAME}-${VERSION}"
	# make tarball
	tar -cf - "${NAME}-${VERSION}" | \
		gzip -c > "${NAME}-${VERSION}.tar.gz"
	rm -rf "${NAME}-${VERSION}"

clean:
	rm -f ${BIN} ${OBJ}

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
	rm -f \
		"${DESTDIR}${DOCPREFIX}/README"
	-rmdir "${DESTDIR}${DOCPREFIX}"
	# removing manual pages.
	for m in ${MAN1}; do rm -f "${DESTDIR}${MANPREFIX}/man1/$$m"; done

.PHONY: all clean dist install uninstall
