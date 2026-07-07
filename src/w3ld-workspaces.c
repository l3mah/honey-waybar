/* w3ld-workspaces: a waybar CFFI plugin rendering one clickable button per
 * w3ld workspace.
 *
 * Unlike a waybar "custom/..." module (a single label with one click handler), a
 * CFFI plugin builds real GTK widgets, so each workspace is an individually
 * clickable, individually styleable button inside one module — with the
 * active / occupied / empty distinction the ext-workspace protocol can't express.
 *
 * Each button is named "ws{N}" and carries the style class active, occupied, or
 * empty. Clicking a button runs `w3ldctl workspace N`.
 *
 * Config keys (waybar module JSON):
 *   output   connector to track (e.g. "DP-1"); omit to follow the focused output
 *   count    number of workspace buttons to show (default 10)
 */
#include "waybar_cffi_module.h"

#include "status_feed.h"
#include "status_json.h"

const size_t wbcffi_version = 2;

#define MAX_WORKSPACES 32

typedef struct {
	char *output;                       /* connector to track, or NULL = focused */
	int count;                          /* number of buttons */
	GtkWidget *buttons[MAX_WORKSPACES];
	status_feed feed;
} w3ld_workspaces;

/* Does this event describe the tracked output? With no output set, follow the
 * focused output (focused == true); otherwise match the connector name. */
static gboolean line_for_output (
	w3ld_workspaces *self,
	const char *line
) {
	if (!self->output)
		return json_get_bool(line, "focused");
	char output[64];
	json_get_string(line, "output", output, sizeof output);
	return strcmp(output, self->output) == 0;
}

/* Set each button's style class + label from a workspaces event: active for the
 * active workspace, occupied for ones with windows, empty otherwise; label is
 * the workspace name if set, else its number. */
static void apply_states (
	w3ld_workspaces *self,
	const char *line
) {
	int active = json_get_int(line, "active");
	int occupied[MAX_WORKSPACES];
	int occupied_count = json_get_occupied(line, occupied, MAX_WORKSPACES);

	for (int index = 0; index < self->count; index++) {
		int number = index + 1;
		const char *state = "empty";
		if (number == active) {
			state = "active";
		} else {
			for (int i = 0; i < occupied_count; i++)
				if (occupied[i] == number)
					state = "occupied";
		}

		GtkWidget *button = self->buttons[index];
		GtkStyleContext *style = gtk_widget_get_style_context(button);
		gtk_style_context_remove_class(style, "empty");
		gtk_style_context_remove_class(style, "occupied");
		gtk_style_context_remove_class(style, "active");
		gtk_style_context_add_class(style, state);

		char name[64], label[16];
		if (!json_get_name(line, number, name, sizeof name))
			snprintf(name, sizeof name, "%d", number);
		snprintf(label, sizeof label, "%d", number);
		gtk_button_set_label(GTK_BUTTON(button),
				name[0] ? name : label);
	}
}

static void on_line (const char *line, void *user) {
	w3ld_workspaces *self = user;
	if (strstr(line, "\"ev\":\"workspaces\"") && line_for_output(self, line))
		apply_states(self, line);
}

/* Button handler: switch w3ld to the clicked workspace. */
static void on_workspace_clicked (
	GtkButton *button,
	gpointer data
) {
	(void)data;
	int number = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "w3ld-ws"));
	char *command = g_strdup_printf("w3ldctl workspace %d", number);
	g_spawn_command_line_async(command, NULL);
	g_free(command);
}

/* Copy a CFFI config value into a plain string. In ABI v2 the value is the JSON
 * representation (a string arrives quoted, e.g. "DP-1"); trim whitespace, then
 * strip the surrounding quotes. */
static char *config_string (const char *value) {
	if (!value)
		return NULL;
	while (*value == ' ' || *value == '\t' || *value == '\n' || *value == '\r')
		value++;
	size_t length = strlen(value);
	while (length > 0 && (value[length - 1] == ' ' || value[length - 1] == '\t'
			|| value[length - 1] == '\n' || value[length - 1] == '\r'))
		length--;
	if (length >= 2 && value[0] == '"' && value[length - 1] == '"')
		return g_strndup(value + 1, length - 2);
	return g_strndup(value, length);
}

void *wbcffi_init (
	const wbcffi_init_info *init_info,
	const wbcffi_config_entry *config_entries,
	size_t config_entries_len
) {
	w3ld_workspaces *self = g_malloc0(sizeof *self);
	self->count = 10;

	for (size_t i = 0; i < config_entries_len; i++) {
		if (!strcmp(config_entries[i].key, "output"))
			self->output = config_string(config_entries[i].value);
		else if (!strcmp(config_entries[i].key, "count"))
			self->count = atoi(config_entries[i].value);
	}
	if (self->count < 1)
		self->count = 1;
	if (self->count > MAX_WORKSPACES)
		self->count = MAX_WORKSPACES;
	if (self->output && self->output[0] == '\0') {
		g_free(self->output);
		self->output = NULL;
	}

	GtkContainer *root = init_info->get_root_widget(init_info->obj);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_name(box, "w3ld-workspaces");

	for (int index = 0; index < self->count; index++) {
		char label[8], name[16];
		snprintf(label, sizeof label, "%d", index + 1);
		snprintf(name, sizeof name, "ws%d", index + 1);

		GtkWidget *button = gtk_button_new_with_label(label);
		gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
		gtk_widget_set_name(button, name);
		gtk_style_context_add_class(gtk_widget_get_style_context(button), "empty");
		g_object_set_data(G_OBJECT(button), "w3ld-ws", GINT_TO_POINTER(index + 1));
		g_signal_connect(button, "clicked",
				G_CALLBACK(on_workspace_clicked), NULL);
		gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
		self->buttons[index] = button;
	}

	gtk_container_add(root, box);
	gtk_widget_show_all(box);

	status_feed_start(&self->feed, on_line, self);
	return self;
}

void wbcffi_deinit (void *instance) {
	w3ld_workspaces *self = instance;
	if (!self)
		return;
	status_feed_stop(&self->feed);
	g_free(self->output);
	g_free(self);
}
