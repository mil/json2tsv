#include <ctype.h>
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
	TYPE_UNKNOWN = 0,
	TYPE_PRIMITIVE,
	TYPE_STRING,
	TYPE_ARRAY,
	TYPE_OBJECT
};

#define JSON_MAX_NODE_DEPTH 32

struct json_node {
	char *name;
	size_t namesiz;
	enum JSONType type;
	size_t index; /* count/index for TYPE_ARRAY and TYPE_OBJECT */
};

static int showindices = 0; /* -n flag: show indices count for arrays */

void
fatal(const char *s)
{
	fputs(s, stderr);
	exit(1);
}

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

void
capacity(char **value, size_t *sz, size_t cur, size_t inc)
{
	size_t need, newsiz;

	/* check addition overflow */
	need = cur + inc;
	if (cur > SIZE_MAX - need || *sz > SIZE_MAX - 16384)
		fatal("overflow\n");

	if (*sz == 0 || need > *sz) {
		for (newsiz = *sz; newsiz < need; newsiz += 16384)
			;
		if (!(*value = realloc(*value, newsiz)))
			fatal("realloc\n");
		*sz = newsiz;
	}
}

void
parsejson(void (*cb)(struct json_node *, size_t, const char *))
{
	struct json_node nodes[JSON_MAX_NODE_DEPTH] = { 0 };
	long cp;
	size_t depth = 0, v = 0, vz = 0;
	int c, escape;
	char *value = NULL;

	capacity(&(nodes[0].name), &(nodes[0].namesiz), 0, 1);
	nodes[0].name[0] = '\0';

	while ((c = GETNEXT()) != EOF) {
		/* not whitespace or control-character */
		if (c <= 0x20 || c == 0x7f)
			continue;

		switch (c) {
		case ':':
			nodes[depth].type = TYPE_PRIMITIVE;
			if (v) {
				if (!depth || nodes[depth - 1].type != TYPE_OBJECT)
					fatal("object member, but not in an object\n");
				value[v] = '\0';
				capacity(&(nodes[depth].name), &(nodes[depth].namesiz), v, 1);
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
						if ((c = GETNEXT()) == EOF || !isxdigit(c))
							fatal("invalid codepoint\n");
						cp = (hexdigit(c) << 12);
						if ((c = GETNEXT()) == EOF || !isxdigit(c))
							fatal("invalid codepoint\n");
						cp |= (hexdigit(c) << 8);
						if ((c = GETNEXT()) == EOF || !isxdigit(c))
							fatal("invalid codepoint\n");
						cp |= (hexdigit(c) << 4);
						if ((c = GETNEXT()) == EOF || !isxdigit(c))
							fatal("invalid codepoint\n");
						cp |= (hexdigit(c));

						capacity(&value, &vz, v, 5);
						v += codepointtoutf8(cp, &value[v]);
						continue;
					default:
						continue; /* ignore unknown escape char */
					}
					capacity(&value, &vz, v, 2);
					value[v++] = c;
				} else if (c == '\\') {
					escape = 1;
				} else if (c == '"') {
					break;
				} else {
					capacity(&value, &vz, v, 2);
					value[v++] = c;
				}
			}
			capacity(&value, &vz, v, 1);
			value[v] = '\0';
			break;
		case '[':
		case '{':
			if (depth + 1 >= JSON_MAX_NODE_DEPTH)
				fatal("max depth reached\n");

			nodes[depth].index = 0;
			nodes[depth].type = c == '{' ? TYPE_OBJECT : TYPE_ARRAY;

			cb(nodes, depth + 1, "");
			v = 0;

			depth++;
			nodes[depth].index = 0;
			capacity(&(nodes[depth].name), &(nodes[depth].namesiz), v, 1);
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
			    (nodes[depth - 1].type == TYPE_OBJECT && c == ']') ||
			    (nodes[depth - 1].type == TYPE_ARRAY && c == '}'))
				fatal("unbalanced nodes\n");

			if (c == ']' || c == '}') {
				nodes[--depth].index++;
			} else if (c == ',') {
				nodes[depth - 1].index++;
				nodes[depth].type = TYPE_PRIMITIVE;
			}
			break;
		default:
			capacity(&value, &vz, v, 2);
			value[v++] = c;
		}
	}

	for (depth = 0; depth < sizeof(nodes) / sizeof(nodes[0]); depth++)
		free(nodes[depth].name);
	free(value);
}

void
printvalue(const char *s)
{
	for (; *s; s++) {
		/* escape some chars */
		switch (*s) {
		case '\n': fputs("\\n",  stdout); break;
		case '\\': fputs("\\\\", stdout); break;
		case '\t': fputs("\\t",  stdout); break;
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
			if (showindices)
				printf("[%zu]", nodes[i].index);
			else
				fputs("[]", stdout);
		}
	}

	switch (nodes[depth - 1].type) {
	case TYPE_UNKNOWN:   fatal("unknown type\n");  return;
	case TYPE_ARRAY:     fputs("\ta\t\n", stdout); return;
	case TYPE_OBJECT:    fputs("\to\t\n", stdout); return;
	case TYPE_PRIMITIVE: fputs("\tp\t",   stdout); break;
	case TYPE_STRING:    fputs("\ts\t",   stdout); break;
	}
	printvalue(value);
	putchar('\n');
}

int
main(int argc, char *argv[])
{
	if (pledge("stdio", NULL) == -1)
		fatal("pledge stdio\n");

	if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'n')
		showindices = 1;

	parsejson(processnode);

	return 0;
}
