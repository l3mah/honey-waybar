/* waybar_config.h: decode a waybar CFFI (ABI v2) config value.
 *
 * Each config value arrives as the JSON *representation* of the value: a string
 * comes quoted and JSON-escaped, and waybar's serializer escapes non-ASCII, so a
 * Font Awesome glyph in the config reaches the plugin as "" — NOT the
 * literal glyph. config_string strips the quotes and un-escapes the interior
 * (standard escapes plus \uXXXX → UTF-8, with surrogate pairs); a non-string
 * value (number, bool) is returned verbatim. Returns a newly g_malloc'd string.
 */
#pragma once

#include <glib.h>
#include <string.h>

static inline void config_append_utf8 (
	GString *out,
	unsigned int codepoint
) {
	if (codepoint < 0x80) {
		g_string_append_c(out, (char)codepoint);
	} else if (codepoint < 0x800) {
		g_string_append_c(out, (char)(0xC0 | (codepoint >> 6)));
		g_string_append_c(out, (char)(0x80 | (codepoint & 0x3F)));
	} else if (codepoint < 0x10000) {
		g_string_append_c(out, (char)(0xE0 | (codepoint >> 12)));
		g_string_append_c(out, (char)(0x80 | ((codepoint >> 6) & 0x3F)));
		g_string_append_c(out, (char)(0x80 | (codepoint & 0x3F)));
	} else {
		g_string_append_c(out, (char)(0xF0 | (codepoint >> 18)));
		g_string_append_c(out, (char)(0x80 | ((codepoint >> 12) & 0x3F)));
		g_string_append_c(out, (char)(0x80 | ((codepoint >> 6) & 0x3F)));
		g_string_append_c(out, (char)(0x80 | (codepoint & 0x3F)));
	}
}

/* Parse four hex digits at s; returns the value, or 0 on a malformed digit. */
static inline unsigned int config_hex4 (const char *s) {
	unsigned int value = 0;
	for (int i = 0; i < 4; i++) {
		char c = s[i];
		value <<= 4;
		if (c >= '0' && c <= '9')
			value |= (unsigned)(c - '0');
		else if (c >= 'a' && c <= 'f')
			value |= (unsigned)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F')
			value |= (unsigned)(c - 'A' + 10);
		else
			return 0;
	}
	return value;
}

static inline char *config_string (const char *value) {
	if (!value)
		return NULL;
	while (*value == ' ' || *value == '\t' || *value == '\n' || *value == '\r')
		value++;
	size_t length = strlen(value);
	while (length > 0 && (value[length - 1] == ' ' || value[length - 1] == '\t'
			|| value[length - 1] == '\n' || value[length - 1] == '\r'))
		length--;

	/* Not a quoted string (number/bool): hand back verbatim. */
	if (length < 2 || value[0] != '"' || value[length - 1] != '"')
		return g_strndup(value, length);

	GString *out = g_string_new(NULL);
	const char *p = value + 1;
	const char *end = value + length - 1;
	while (p < end) {
		if (*p != '\\' || p + 1 >= end) {
			g_string_append_c(out, *p++);
			continue;
		}
		p++;
		switch (*p) {
		case 'n': g_string_append_c(out, '\n'); p++; break;
		case 't': g_string_append_c(out, '\t'); p++; break;
		case 'r': g_string_append_c(out, '\r'); p++; break;
		case 'b': g_string_append_c(out, '\b'); p++; break;
		case 'f': g_string_append_c(out, '\f'); p++; break;
		case '/': g_string_append_c(out, '/'); p++; break;
		case '"': g_string_append_c(out, '"'); p++; break;
		case '\\': g_string_append_c(out, '\\'); p++; break;
		case 'u':
			if (p + 5 > end) {
				p++;
				break;
			}
			unsigned int cp = config_hex4(p + 1);
			p += 5;
			/* Combine a UTF-16 surrogate pair into one codepoint. */
			if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 <= end
					&& p[0] == '\\' && p[1] == 'u') {
				unsigned int low = config_hex4(p + 2);
				if (low >= 0xDC00 && low <= 0xDFFF) {
					cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
					p += 6;
				}
			}
			config_append_utf8(out, cp);
			break;
		default:
			g_string_append_c(out, *p++);
			break;
		}
	}
	return g_string_free(out, FALSE);
}
