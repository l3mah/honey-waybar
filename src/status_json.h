/* status_json.h: minimal readers for w3ld's status stream.
 *
 * w3ld's `subscribe` schema is fixed and versioned (v:1), so values are found by
 * locating known keys rather than with a general JSON parser. */
#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Point just past `"key":` in a line, or NULL if the key is absent. */
static inline const char *json_find (
	const char *line,
	const char *key
) {
	char needle[32];
	snprintf(needle, sizeof needle, "\"%s\":", key);
	const char *at = strstr(line, needle);
	return at ? at + strlen(needle) : NULL;
}

/* Read the JSON string value of key into out (un-escaped), returning false if
 * the key is absent. */
static inline bool json_get_string (
	const char *line,
	const char *key,
	char *out,
	size_t capacity
) {
	const char *value = json_find(line, key);
	if (!value || *value != '"') {
		if (capacity)
			out[0] = '\0';
		return false;
	}
	value++;
	size_t length = 0;
	while (*value && *value != '"' && length + 1 < capacity) {
		if (*value == '\\' && value[1]) {
			value++;
			switch (*value) {
			case 'n': out[length++] = '\n'; break;
			case 't': out[length++] = '\t'; break;
			default:  out[length++] = *value; break; /* \" and \\ */
			}
		} else {
			out[length++] = *value;
		}
		value++;
	}
	out[length] = '\0';
	return true;
}

static inline int json_get_int (
	const char *line,
	const char *key
) {
	const char *value = json_find(line, key);
	return value ? atoi(value) : 0;
}

static inline bool json_get_bool (
	const char *line,
	const char *key
) {
	const char *value = json_find(line, key);
	return value && strncmp(value, "true", 4) == 0;
}

/* Parse the "occupied":[...] array of workspace numbers into out (sorted by the
 * producer). Returns the count. */
static inline int json_get_occupied (
	const char *line,
	int *out,
	int capacity
) {
	const char *value = json_find(line, "occupied");
	if (!value || *value != '[')
		return 0;
	value++;
	int count = 0;
	while (*value && *value != ']') {
		if (*value >= '0' && *value <= '9') {
			if (count < capacity)
				out[count++] = atoi(value);
			while (*value >= '0' && *value <= '9')
				value++;
		} else {
			value++;
		}
	}
	return count;
}

/* Read a workspace's name from "names":{"N":"...",...} into out, or false. */
static inline bool json_get_name (
	const char *line,
	int number,
	char *out,
	size_t capacity
) {
	const char *names = strstr(line, "\"names\":{");
	if (!names)
		return false;
	char key[16];
	snprintf(key, sizeof key, "\"%d\":", number);
	const char *at = strstr(names, key);
	if (!at)
		return false;
	at += strlen(key);
	if (*at != '"')
		return false;
	at++;
	size_t length = 0;
	while (*at && *at != '"' && length + 1 < capacity) {
		if (*at == '\\' && at[1])
			at++;
		out[length++] = *at++;
	}
	out[length] = '\0';
	return true;
}
