#include <stdint.h>

enum JSONType {
	TYPE_ARRAY     = 'a',
	TYPE_OBJECT    = 'o',
	TYPE_STRING    = 's',
	TYPE_BOOL      = 'b',
	TYPE_NULL      = '?',
	TYPE_NUMBER    = 'n'
};

enum JSONError {
	JSON_ERROR_MEM     = -2,
	JSON_ERROR_INVALID = -1
};

#define JSON_MAX_NODE_DEPTH 64

struct json_node {
	enum JSONType type;
	char *name;
	size_t namesiz;
	size_t index; /* count/index for array or object type */
};

int parsejson(void (*cb)(struct json_node *, size_t, const char *));
