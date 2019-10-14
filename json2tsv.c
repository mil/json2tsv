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

#define GETNEXT getchar

enum JSONType {
	TYPE_PRIMITIVE = 'p',
	TYPE_STRING    = 's',
	TYPE_ARRAY     = 'a',
	TYPE_OBJECT    = 'o'
};

#define JSON_MAX_NODE_DEPTH 32

struct json_node {
	enum JSONType type;
	char *name;
	size_t namesiz;
	size_t index; /* count/index for TYPE_ARRAY and TYPE_OBJECT */
};

static int showindices = 0; /* -n flag: show indices count for arrays */

int
codepointtoutf8(long r, char *s)
{
	if (r == 0) {
		return 0; /* NUL byte */
	} else if (r <= 0x7F) {
		/* 1 byte: 0aaaaaaa */
		s[0] = r;
		return 1;
	} else if (r <= 0x07FF) {
		/* 2 bytes: 00000aaa aabbbbbb */
		s[0] = 0xC0 | ((r & 0x0007C0) >>  6); /* 110aaaaa */
		s[1] = 0x80 |  (r & 0x00003F);        /* 10bbbbbb */
		return 2;
	} else if (r <= 0xFFFF) {
		/* 3 bytes: aaaabbbb bbcccccc */
		s[0] = 0xE0 | ((r & 0x00F000) >> 12); /* 1110aaaa */
		s[1] = 0x80 | ((r & 0x000FC0) >>  6); /* 10bbbbbb */
		s[2] = 0x80 |  (r & 0x00003F);        /* 10cccccc */
		return 3;
	} else {
		/* 4 bytes: 000aaabb bbbbcccc ccdddddd */
		s[0] = 0xF0 | ((r & 0x1C0000) >> 18); /* 11110aaa */
		s[1] = 0x80 | ((r & 0x03F000) >> 12); /* 10bbbbbb */
		s[2] = 0x80 | ((r & 0x000FC0) >>  6); /* 10cccccc */
		s[3] = 0x80 |  (r & 0x00003F);        /* 10dddddd */
		return 4;
	}
}

int
hexdigit(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (c >= 'a' && c <= 'f')
		return 10 + (c - 'a');
	else if (c >= 'A' && c <= 'F')
		return 10 + (c - 'A');
	return 0;
}

int
capacity(char **value, size_t *sz, size_t cur, size_t inc)
{
	size_t need, newsiz;
	char *newp;

	/* check addition overflow */
	if (cur > SIZE_MAX - inc) {
		errno = EOVERFLOW;
		return -1;
	}
	need = cur + inc;

	if (need > *sz) {
		if (need > SIZE_MAX - 4096) {
			newsiz = SIZE_MAX;
		} else {
			for (newsiz = *sz; newsiz < need; newsiz += 4096)
				;
		}
		if (!(newp = realloc(*value, newsiz)))
			return -1; /* up to caller to free *value */
		*value = newp;
		*sz = newsiz;
	}
	return 0;
}

int
parsejson(void (*cb)(struct json_node *, size_t, const char *), const char **errstr)
{
	struct json_node nodes[JSON_MAX_NODE_DEPTH] = { 0 };
	size_t depth = 0, v = 0, vz = 0;
	long cp, hi, lo;
	int c, i, escape, ret = -1;
	char *value = NULL;

	*errstr = "cannot allocate enough memory";
	if (capacity(&(nodes[0].name), &(nodes[0].namesiz), 0, 1) == -1)
		goto end;
	nodes[0].name[0] = '\0';

	while ((c = GETNEXT()) != EOF) {
		/* not whitespace or control-character */
		if (c <= 0x20 || c == 0x7f)
			continue;

		switch (c) {
		case ':':
			nodes[depth].type = TYPE_PRIMITIVE;
			if (v) {
				if (!depth || nodes[depth - 1].type != TYPE_OBJECT) {
					*errstr = "object member, but not in an object";
					goto end;
				}
				value[v] = '\0';
				if (capacity(&(nodes[depth].name), &(nodes[depth].namesiz), v, 1) == -1)
					goto end;
				memcpy(nodes[depth].name, value, v);
				nodes[depth].name[v] = '\0';
				v = 0;
			}
			break;
		case '"':
			v = 0;
			nodes[depth].type = TYPE_STRING;
			for (escape = 0; (c = GETNEXT()) != EOF;) {
				if (iscntrl(c))
					continue;

				if (escape) {
					escape = 0;

					switch (c) {
					case '"': /* FALLTHROUGH */
					case '\\':
					case '/': break;
					case 'b': c = '\b'; break;
					case 'f': c = '\f'; break;
					case 'n': c = '\n'; break;
					case 'r': c = '\r'; break;
					case 't': c = '\t'; break;
					case 'u': /* hex hex hex hex */
						for (i = 12, cp = 0; i >= 0; i -= 4) {
							if ((c = GETNEXT()) == EOF || !isxdigit(c)) {
								*errstr = "invalid codepoint";
								goto end;
							}
							cp |= (hexdigit(c) << i);
						}
						/* See also:
						 * RFC7159 - 7. Strings and
						 * https://unicode.org/faq/utf_bom.html#utf8-4
						 * 0xd800 - 0xdb7f - high surrogates (no private use range) */
						if (cp >= 0xd800 && cp <= 0xdb7f) {
							if (GETNEXT() != '\\' || GETNEXT() != 'u') {
								*errstr = "invalid codepoint";
								goto end;
							}
							for (hi = cp, i = 12, lo = 0; i >= 0; i -= 4) {
								if ((c = GETNEXT()) == EOF || !isxdigit(c)) {
									*errstr = "invalid codepoint";
									goto end;
								}
								lo |= (hexdigit(c) << i);
							}
							/* 0xdc00 - 0xdfff - low surrogates: must follow after high surrogate */
							if (!(lo >= 0xdc00 && lo <= 0xdfff)) {
								*errstr = "invalid codepoint";
								goto end;
							}
							cp = (hi << 10) + lo - 56613888; /* - offset */
						}
						if (capacity(&value, &vz, v, 5) == -1)
							goto end;
						v += codepointtoutf8(cp, &value[v]);
						continue;
					default:
						continue; /* ignore unknown escape char */
					}
					if (capacity(&value, &vz, v, 2) == -1)
						goto end;
					value[v++] = c;
				} else if (c == '\\') {
					escape = 1;
				} else if (c == '"') {
					break;
				} else {
					if (capacity(&value, &vz, v, 2) == -1)
						goto end;
					value[v++] = c;
				}
			}
			if (capacity(&value, &vz, v, 1) == -1)
				goto end;
			value[v] = '\0';
			break;
		case '[':
		case '{':
			if (depth + 1 >= JSON_MAX_NODE_DEPTH) {
				*errstr = "max node depth reached";
				goto end;
			}

			nodes[depth].index = 0;
			nodes[depth].type = c == '{' ? TYPE_OBJECT : TYPE_ARRAY;

			cb(nodes, depth + 1, "");
			v = 0;

			depth++;
			nodes[depth].index = 0;
			if (capacity(&(nodes[depth].name), &(nodes[depth].namesiz), v, 1) == -1)
				goto end;
			nodes[depth].name[0] = '\0';
			nodes[depth].type = TYPE_PRIMITIVE;
			break;
		case ']':
		case '}':
		case ',':
			if (v &&
			    (nodes[depth].type == TYPE_STRING ||
			    nodes[depth].type == TYPE_PRIMITIVE)) {
				value[v] = '\0';
				cb(nodes, depth + 1, value);
				v = 0;
			}
			if (!depth ||
			    (c == ']' && nodes[depth - 1].type != TYPE_ARRAY) ||
			    (c == '}' && nodes[depth - 1].type != TYPE_OBJECT)) {
				*errstr = "unbalanced nodes";
				goto end;
			}

			if (c == ']' || c == '}') {
				nodes[--depth].index++;
			} else if (c == ',') {
				nodes[depth - 1].index++;
				nodes[depth].type = TYPE_PRIMITIVE;
			}
			break;
		default:
			if (capacity(&value, &vz, v, 2) == -1)
				goto end;
			value[v++] = c;
		}
	}
	if (depth) {
		*errstr = "unbalanced nodes";
		goto end;
	}

	ret = 0; /* success */
	*errstr = NULL;
end:
	for (depth = 0; depth < sizeof(nodes) / sizeof(nodes[0]); depth++)
		free(nodes[depth].name);
	free(value);

	return ret;
}

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
	const char *errstr;

	if (pledge("stdio", NULL) == -1) {
		fprintf(stderr, "pledge stdio: %s\n", strerror(errno));
		return 1;
	}

	if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'n')
		showindices = 1;

	if (parsejson(processnode, &errstr) == -1) {
		fprintf(stderr, "error: %s\n", errstr);
		return 1;
	}

	return 0;
}
