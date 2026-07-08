/* w3ld-window: a waybar CFFI plugin showing the focused window's title.
 *
 * The CFFI sibling of the workspaces and gamma plugins: it connects to w3ld's
 * status socket directly (via status_feed) instead of running a piped
 * `w3ldctl subscribe` subprocess, and auto-reconnects if w3ld restarts. Shows
 * the focused window's title, falling back to its app-id, with the app-id in
 * the tooltip; the class is active when a window is focused, else empty.
 *
 * Config keys (waybar module JSON):
 *   output       connector to track (e.g. "DP-1"); omit to follow the focused
 *                output / window
 *   max-length   ellipsize the title past this many characters (0 = no limit)
 */
#include "waybar_cffi_module.h"

#include "status_feed.h"
#include "status_json.h"
#include "waybar_config.h"

const size_t wbcffi_version = 2;

typedef struct {
	char *output;           /* connector to track, or NULL = focused */
	int max_length;         /* ellipsize past this many chars, 0 = no limit */
	GtkWidget *label;
	status_feed feed;
} w3ld_window;

/* Does this event describe the tracked output? With no output set, follow the
 * focused output (focused == true); otherwise match the connector name. */
static gboolean line_for_output (
	w3ld_window *self,
	const char *line
) {
	if (!self->output)
		return json_get_bool(line, "focused");
	char output[64];
	json_get_string(line, "output", output, sizeof output);
	return strcmp(output, self->output) == 0;
}

/* Update the label from a window event: title (or app-id if the title is
 * empty), the app-id as tooltip, and the active/empty class. */
static void on_line (const char *line, void *user) {
	w3ld_window *self = user;
	if (!strstr(line, "\"ev\":\"window\"") || !line_for_output(self, line))
		return;

	char app_id[256], title[512];
	json_get_string(line, "app_id", app_id, sizeof app_id);
	json_get_string(line, "title", title, sizeof title);

	gtk_label_set_text(GTK_LABEL(self->label), title[0] ? title : app_id);
	gtk_widget_set_tooltip_text(self->label, app_id[0] ? app_id : NULL);

	GtkStyleContext *style = gtk_widget_get_style_context(self->label);
	gtk_style_context_remove_class(style, "active");
	gtk_style_context_remove_class(style, "empty");
	gtk_style_context_add_class(style, app_id[0] ? "active" : "empty");
}

void *wbcffi_init (
	const wbcffi_init_info *init_info,
	const wbcffi_config_entry *config_entries,
	size_t config_entries_len
) {
	w3ld_window *self = g_malloc0(sizeof *self);

	for (size_t i = 0; i < config_entries_len; i++) {
		if (!strcmp(config_entries[i].key, "output"))
			self->output = config_string(config_entries[i].value);
		else if (!strcmp(config_entries[i].key, "max-length"))
			self->max_length = atoi(config_entries[i].value);
	}
	if (self->output && self->output[0] == '\0') {
		g_free(self->output);
		self->output = NULL;
	}
	if (self->max_length < 0)
		self->max_length = 0;

	GtkContainer *root = init_info->get_root_widget(init_info->obj);
	self->label = gtk_label_new("");
	gtk_widget_set_name(self->label, "w3ld-window");
	if (self->max_length > 0) {
		gtk_label_set_max_width_chars(GTK_LABEL(self->label), self->max_length);
		gtk_label_set_ellipsize(GTK_LABEL(self->label), PANGO_ELLIPSIZE_END);
	}
	gtk_style_context_add_class(gtk_widget_get_style_context(self->label), "empty");
	gtk_container_add(root, self->label);
	gtk_widget_show_all(self->label);

	status_feed_start(&self->feed, on_line, self);
	return self;
}

void wbcffi_deinit (void *instance) {
	w3ld_window *self = instance;
	if (!self)
		return;
	status_feed_stop(&self->feed);
	g_free(self->output);
	g_free(self);
}
