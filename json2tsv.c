#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __OpenBSD__
#include <unistd.h>
#else
#define pledge(a,b) 0
#endif

#include "json.h"

static int showindices = 0; /* -n flag: show indices count for arrays */

void
printvalue(const char *s)
{
	for (; *s; s++) {
		/* escape some chars */
		switch (*s) {
		case '\n': putchar('\\'); putchar('n'); break;
		case '\\': putchar('\\'); putchar('\\'); break;
		case '\t': putchar('\\'); putchar('t'); break;
		default:
			/* ignore other control chars */
			if (iscntrl((unsigned char)*s))
				continue;
			putchar(*s);
		}
	}
}

void
processnode(struct json_node *nodes, size_t depth, const char *value)
{
	size_t i;

	for (i = 0; i < depth; i++) {
		printvalue(nodes[i].name);

		if (i + 1 == depth &&
		    (nodes[i].type == TYPE_OBJECT || nodes[i].type == TYPE_ARRAY))
			continue;

		if (nodes[i].type == TYPE_OBJECT) {
			putchar('.');
		} else if (nodes[i].type == TYPE_ARRAY) {
			if (showindices) {
				printf("[%zu]", nodes[i].index);
			} else {
				putchar('[');
				putchar(']');
			}
		}
	}

	putchar('\t');
	putchar(nodes[depth - 1].type);
	putchar('\t');
	printvalue(value);
	putchar('\n');
}

int
main(int argc, char *argv[])
{
	if (pledge("stdio", NULL) == -1) {
		fprintf(stderr, "pledge stdio: %s\n", strerror(errno));
		return 1;
	}

	if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'n')
		showindices = 1;

	switch (parsejson(processnode)) {
	case JSON_ERROR_MEM:
		fputs("error: cannot allocate enough memory\n", stderr);
		return 2;
	case JSON_ERROR_INVALID:
		fputs("error: invalid JSON\n", stderr);
		return 1;
	}

	return 0;
}
