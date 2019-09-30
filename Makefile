build: clean
	${CC} -o json2tsv json2tsv.c ${CFLAGS} ${LDFLAGS}

clean:
	rm -f json2tsv
