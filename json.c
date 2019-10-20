#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GETNEXT getchar

#include "json.h"

static int
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

static int
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

static int
capacity(char **value, size_t *sz, size_t cur, size_t inc)
{
	size_t need, newsiz;
	char *newp;

	/* check for addition overflow */
	if (cur > SIZE_MAX - inc) {
		errno = EOVERFLOW;
		return -1;
	}
	need = cur + inc;

	if (need > *sz) {
		if (need > SIZE_MAX / 2) {
			newsiz = SIZE_MAX;
		} else {
			for (newsiz = *sz < 64 ? 64 : *sz; newsiz <= need; newsiz *= 2)
				;
		}
		if (!(newp = realloc(*value, newsiz)))
			return -1; /* up to caller to free *value */
		*value = newp;
		*sz = newsiz;
	}
	return 0;
}

#define EXPECT_VALUE         "{[\"-0123456789tfn"
#define EXPECT_STRING        "\""
#define EXPECT_END           "}],"
#define EXPECT_NOTHING       ""
#define EXPECT_OBJECT_STRING EXPECT_STRING "}"
#define EXPECT_ARRAY_VALUE   EXPECT_VALUE "]"

#define JSON_INVALID()       do { ret = JSON_ERROR_INVALID; goto end; } while (0);

int
parsejson(void (*cb)(struct json_node *, size_t, const char *))
{
	struct json_node nodes[JSON_MAX_NODE_DEPTH] = { 0 };
	size_t depth = 0, p = 0, len, sz = 0;
	long cp, hi, lo;
	char pri[128], *str = NULL;
	int c, i, escape, iskey = 0, ret = JSON_ERROR_MEM;
	const char *expect = EXPECT_VALUE;

	if (capacity(&(nodes[0].name), &(nodes[0].namesiz), 0, 1) == -1)
		goto end;
	nodes[0].name[0] = '\0';

	while (1) {
		c = GETNEXT();
handlechr:
		if (c == EOF)
			break;

		if (c && strchr(" \t\n\r", c)) /* (no \v, \f, \b etc) */
			continue;

		if (!c || !strchr(expect, c))
			JSON_INVALID();

		switch (c) {
		case ':':
			/* not in an object or key in object is not a string */
			if (!depth || nodes[depth - 1].type != TYPE_OBJECT ||
			    nodes[depth].type != TYPE_STRING)
				JSON_INVALID();
			iskey = 0;
			expect = EXPECT_VALUE;
			break;
		case '"':
			nodes[depth].type = TYPE_STRING;
			escape = 0;
			len = 0;
			while (1) {
				c = GETNEXT();
chr:
				/* EOF or control char: 0x7f is not defined as a control char in RFC8259 */
				if (c < 0x20)
					JSON_INVALID();

				if (escape) {
escchr:
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
						if (capacity(&str, &sz, len, 4) == -1)
							goto end;
						for (i = 12, cp = 0; i >= 0; i -= 4) {
							if ((c = GETNEXT()) == EOF || !isxdigit(c))
								JSON_INVALID(); /* invalid codepoint */
							cp |= (hexdigit(c) << i);
						}
						/* RFC8259 - 7. Strings - surrogates.
						 * 0xd800 - 0xdb7f - high surrogates */
						if (cp >= 0xd800 && cp <= 0xdb7f) {
							if ((c = GETNEXT()) != '\\') {
								len += codepointtoutf8(cp, &str[len]);
								goto chr;
							}
							if ((c = GETNEXT()) != 'u') {
								len += codepointtoutf8(cp, &str[len]);
								goto escchr;
							}
							for (hi = cp, i = 12, lo = 0; i >= 0; i -= 4) {
								if ((c = GETNEXT()) == EOF || !isxdigit(c))
									JSON_INVALID(); /* invalid codepoint */
								lo |= (hexdigit(c) << i);
							}
							/* 0xdc00 - 0xdfff - low surrogates */
							if (lo >= 0xdc00 && lo <= 0xdfff) {
								cp = (hi << 10) + lo - 56613888; /* - offset */
							} else {
								/* handle graceful: raw invalid output bytes */
								len += codepointtoutf8(hi, &str[len]);
								if (capacity(&str, &sz, len, 4) == -1)
									goto end;
								len += codepointtoutf8(lo, &str[len]);
								continue;
							}
						}
						len += codepointtoutf8(cp, &str[len]);
						continue;
					default:
						JSON_INVALID(); /* invalid escape char */
					}
					if (capacity(&str, &sz, len, 1) == -1)
						goto end;
					str[len++] = c;
				} else if (c == '\\') {
					escape = 1;
				} else if (c == '"') {
					if (capacity(&str, &sz, len, 1) == -1)
						goto end;
					str[len++] = '\0';

					if (iskey) {
						if (capacity(&(nodes[depth].name), &(nodes[depth].namesiz), len, 1) == -1)
							goto end;
						memcpy(nodes[depth].name, str, len);
					} else {
						cb(nodes, depth + 1, str);
					}
					break;
				} else {
					if (capacity(&str, &sz, len, 1) == -1)
						goto end;
					str[len++] = c;
				}
			}
			if (iskey)
				expect = ":";
			else
				expect = EXPECT_END;
			break;
		case '[':
		case '{':
			if (depth + 1 >= JSON_MAX_NODE_DEPTH)
				JSON_INVALID(); /* too deep */

			nodes[depth].index = 0;
			nodes[depth].type = TYPE_OBJECT;
			if (c == '{') {
				iskey = 1;
				nodes[depth].type = TYPE_OBJECT;
				expect = EXPECT_OBJECT_STRING;
			} else if (c == '[') {
				nodes[depth].type = TYPE_ARRAY;
				expect = EXPECT_ARRAY_VALUE;
			}

			cb(nodes, depth + 1, "");

			depth++;
			nodes[depth].index = 0;
			if (capacity(&(nodes[depth].name), &(nodes[depth].namesiz), 0, 1) == -1)
				goto end;
			nodes[depth].name[0] = '\0';
			break;
		case ']':
		case '}':
			if (!depth ||
			   (c == ']' && nodes[depth - 1].type != TYPE_ARRAY) ||
			   (c == '}' && nodes[depth - 1].type != TYPE_OBJECT))
				JSON_INVALID(); /* unbalanced nodes */

			nodes[--depth].index++;
			if (!depth)
				expect = EXPECT_NOTHING;
			else
				expect = EXPECT_END;
			break;
		case ',':
			nodes[depth - 1].index++;
			if (nodes[depth - 1].type == TYPE_OBJECT) {
				iskey = 1;
				expect = EXPECT_STRING;
			} else {
				expect = EXPECT_VALUE;
			}
			break;
		case 't': /* true */
			if (GETNEXT() != 'r' || GETNEXT() != 'u' || GETNEXT() != 'e')
				JSON_INVALID();
			nodes[depth].type = TYPE_BOOL;
			cb(nodes, depth + 1, "true");
			expect = EXPECT_END;
			break;
		case 'f': /* false */
			if (GETNEXT() != 'a' || GETNEXT() != 'l' || GETNEXT() != 's' || GETNEXT() != 'e')
				JSON_INVALID();
			nodes[depth].type = TYPE_BOOL;
			cb(nodes, depth + 1, "false");
			expect = EXPECT_END;
			break;
		case 'n': /* null */
			if (GETNEXT() != 'u' || GETNEXT() != 'l' || GETNEXT() != 'l')
				JSON_INVALID();
			nodes[depth].type = TYPE_NULL;
			cb(nodes, depth + 1, "null");
			expect = EXPECT_END;
			break;
		default: /* number */
			nodes[depth].type = TYPE_NUMBER;
			p = 0;
			pri[p++] = c;
			expect = EXPECT_END;
			while (1) {
				c = GETNEXT();
				if (!c || !strchr("0123456789eE+-.", c) ||
				    c == EOF || p + 1 >= sizeof(pri)) {
					pri[p] = '\0';
					cb(nodes, depth + 1, pri);
					goto handlechr; /* do not read next char, handle this */
				} else {
					pri[p++] = c;
				}
			}
		}
	}
	if (depth)
		JSON_INVALID(); /* unbalanced nodes */

	ret = 0; /* success */
end:
	for (depth = 0; depth < sizeof(nodes) / sizeof(nodes[0]); depth++)
		free(nodes[depth].name);
	free(str);

	return ret;
}
