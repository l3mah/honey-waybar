/* w3ld-waybar: a waybar custom-module adapter for w3ld's status stream.
 *
 * Reads w3ld's `w3ldctl subscribe` JSON Lines on stdin and prints waybar
 * custom-module JSON (one object per line) for a single module. Usage in a
 * waybar "custom/..." module:
 *
 *   "exec": "w3ldctl subscribe | w3ld-waybar workspaces DP-1"
 *   "exec": "w3ldctl subscribe | w3ld-waybar workspace 3 DP-1"
 *   "exec": "w3ldctl subscribe | w3ld-waybar window"
 *
 * Modules:
 *   workspaces   one module for all workspaces: the occupied numbers with the
 *                active one bold.
 *   workspace N  one module per number: text is N, class is
 *                active|occupied|empty. Use ten of these for separate,
 *                individually clickable and styleable workspace buttons. (For a
 *                single native clickable module, prefer the CFFI plugin.)
 *   window       the focused window's title (falling back to app-id).
 *
 * The optional output argument pins the module to one monitor (its connector
 * name, e.g. DP-1); without it the module follows the focused output.
 *
 * The input schema is fixed and versioned (v:1); the JSON is parsed by locating
 * known keys rather than with a general parser.
 */
#include "status_json.h"

enum module {
	MODULE_WORKSPACES,
	MODULE_WORKSPACE,
	MODULE_WINDOW,
};

/* ------------------------------------------------------------- output escaping */

/* Append src to dest for a waybar text field: JSON-escape (" and \) and
 * Pango-escape (& < >), since waybar renders module text as Pango markup. */
static void append_escaped (
	char *dest,
	size_t capacity,
	const char *src
) {
	size_t length = strlen(dest);
	for (const char *cursor = src; *cursor && length + 6 < capacity; cursor++) {
		switch (*cursor) {
		case '"':  length += (size_t)snprintf(dest + length, capacity - length, "\\\""); break;
		case '\\': length += (size_t)snprintf(dest + length, capacity - length, "\\\\"); break;
		case '&':  length += (size_t)snprintf(dest + length, capacity - length, "&amp;"); break;
		case '<':  length += (size_t)snprintf(dest + length, capacity - length, "&lt;"); break;
		case '>':  length += (size_t)snprintf(dest + length, capacity - length, "&gt;"); break;
		case '\n':
		case '\t': dest[length++] = ' '; dest[length] = '\0'; break;
		default:   dest[length++] = *cursor; dest[length] = '\0'; break;
		}
	}
}

/* -------------------------------------------------------------------- modules */

/* Print the waybar object for a "workspaces" event: the occupied workspaces with
 * the active one bold (Pango), and a class reflecting output focus. */
static void render_workspaces (const char *line) {
	int active = json_get_int(line, "active");
	bool focused = json_get_bool(line, "focused");
	char output[64];
	json_get_string(line, "output", output, sizeof output);

	int occupied[64];
	int count = json_get_occupied(line, occupied, 64);

	/* Ensure the active workspace appears even when it has no windows. */
	bool active_present = false;
	for (int i = 0; i < count; i++)
		if (occupied[i] == active)
			active_present = true;
	if (!active_present && count < 64) {
		int insert = count;
		while (insert > 0 && occupied[insert - 1] > active) {
			occupied[insert] = occupied[insert - 1];
			insert--;
		}
		occupied[insert] = active;
		count++;
	}

	char text[512] = "";
	size_t length = 0;
	for (int i = 0; i < count; i++) {
		const char *separator = i ? " " : "";
		if (occupied[i] == active)
			length += (size_t)snprintf(text + length, sizeof text - length,
					"%s<b>%d</b>", separator, occupied[i]);
		else
			length += (size_t)snprintf(text + length, sizeof text - length,
					"%s%d", separator, occupied[i]);
	}

	char tooltip[128] = "";
	append_escaped(tooltip, sizeof tooltip, output);
	char tail[32];
	snprintf(tail, sizeof tail, ": workspace %d", active);
	append_escaped(tooltip, sizeof tooltip, tail);

	printf("{\"text\":\"%s\",\"class\":\"%s\",\"tooltip\":\"%s\"}\n",
			text, focused ? "focused" : "unfocused", tooltip);
	fflush(stdout);
}

/* Print the waybar object for one workspace number in a "workspaces" event: the
 * text is the number and the class is active/occupied/empty, so each workspace
 * can be its own clickable module with per-state styling. */
static void render_workspace (
	const char *line,
	int number
) {
	int active = json_get_int(line, "active");
	int occupied[64];
	int count = json_get_occupied(line, occupied, 64);

	const char *state = "empty";
	if (number == active) {
		state = "active";
	} else {
		for (int i = 0; i < count; i++)
			if (occupied[i] == number)
				state = "occupied";
	}

	printf("{\"text\":\"%d\",\"class\":\"%s\"}\n", number, state);
	fflush(stdout);
}

/* Print the waybar object for a "window" event: the focused window's title (or
 * app-id), with a class reflecting whether a window is focused. */
static void render_window (const char *line) {
	char app_id[256], title[512];
	json_get_string(line, "app_id", app_id, sizeof app_id);
	json_get_string(line, "title", title, sizeof title);

	char text[1024] = "";
	append_escaped(text, sizeof text, title[0] ? title : app_id);
	char tooltip[512] = "";
	append_escaped(tooltip, sizeof tooltip, app_id);

	printf("{\"text\":\"%s\",\"class\":\"%s\",\"tooltip\":\"%s\"}\n",
			text, app_id[0] ? "active" : "empty", tooltip);
	fflush(stdout);
}

/* Does this event belong to the module's output? With no output name, follow the
 * focused output/window (focused == true); otherwise match the connector name. */
static bool line_matches (
	const char *line,
	const char *output_filter
) {
	if (!output_filter)
		return json_get_bool(line, "focused");
	char output[64];
	json_get_string(line, "output", output, sizeof output);
	return strcmp(output, output_filter) == 0;
}

int main (
	int argc,
	char **argv
) {
	if (argc < 2) {
		fprintf(stderr, "usage: w3ld-waybar "
				"<workspaces|workspace <n>|window> [output]\n");
		return 2;
	}
	enum module module;
	const char *event;
	int target_workspace = 0;
	const char *output_filter;
	if (!strcmp(argv[1], "workspaces")) {
		module = MODULE_WORKSPACES;
		event = "\"ev\":\"workspaces\"";
		output_filter = argc > 2 ? argv[2] : NULL;
	} else if (!strcmp(argv[1], "workspace")) {
		if (argc < 3) {
			fprintf(stderr, "usage: w3ld-waybar workspace <number> [output]\n");
			return 2;
		}
		module = MODULE_WORKSPACE;
		event = "\"ev\":\"workspaces\"";
		target_workspace = atoi(argv[2]);
		output_filter = argc > 3 ? argv[3] : NULL;
	} else if (!strcmp(argv[1], "window")) {
		module = MODULE_WINDOW;
		event = "\"ev\":\"window\"";
		output_filter = argc > 2 ? argv[2] : NULL;
	} else {
		fprintf(stderr, "w3ld-waybar: unknown module '%s' "
				"(workspaces|workspace|window)\n", argv[1]);
		return 2;
	}

	char line[8192];
	while (fgets(line, sizeof line, stdin)) {
		if (!strstr(line, event) || !line_matches(line, output_filter))
			continue;
		switch (module) {
		case MODULE_WORKSPACES:
			render_workspaces(line);
			break;
		case MODULE_WORKSPACE:
			render_workspace(line, target_workspace);
			break;
		case MODULE_WINDOW:
			render_window(line);
			break;
		}
	}
	return 0;
}
