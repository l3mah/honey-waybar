/* w3ld-gamma: a waybar CFFI plugin controlling w3ld's integrated night light.
 *
 * A day/night toggle over two presets. w3ld owns the current applied gamma and
 * broadcasts every change on its status stream, so this module is a live
 * reflector: click toggles between day and night, scroll adjusts brightness,
 * and the display always mirrors w3ld's real state — however it was driven
 * (this module, a hotkey, or a direct `w3ldctl gamma`).
 *
 * Asymmetric by design. Temperature is the mode's identity (day = neutral,
 * night = warm) and comes only from config, restored on every toggle.
 * Brightness is the ridable trim: scroll changes it, and any live change is
 * adopted into the active mode so it survives toggles — until the bar restarts,
 * when in-memory state resets to the configured defaults.
 *
 * Config keys (waybar module JSON):
 *   temperature-day / temperature-night   mode temperatures in Kelvin
 *   brightness-day  / brightness-night    starting per-mode brightness (percent)
 *   step                                  scroll step (percent)
 *   icon-day        / icon-night          per-mode glyph for {icon}
 *   format          tokens {icon} {temperature} {brightness}; omit any to hide
 *
 * The brightness clamp (min/max) lives in w3ld (`w3ldctl gamma min|max <pct>`),
 * so it is universal to scroll and hotkeys alike.
 */
#include "waybar_cffi_module.h"

#include "status_feed.h"
#include "status_json.h"

const size_t wbcffi_version = 2;

typedef enum {
	MODE_DAY,
	MODE_NIGHT,
} gamma_mode;

typedef struct {
	int temp_day, temp_night;       /* mode identity temperatures (Kelvin) */
	int bright_day, bright_night;   /* per-mode brightness trim (percent) */
	int step;                       /* scroll step (percent) */
	char *icon_day, *icon_night;
	char *format;

	gamma_mode mode;
	int live_temp, live_bright;     /* last values reported by w3ld (display) */
	double scroll_accum;            /* smooth-scroll accumulator */

	GtkWidget *button;
	status_feed feed;
} w3ld_gamma;

/* Copy a CFFI config value into a plain string. In ABI v2 the value is the JSON
 * representation (a string arrives quoted, e.g. "☀"); trim whitespace, then
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

/* Redraw the label from the format template and set the day/night (+ override)
 * style classes. {temperature}/{brightness} render w3ld's live values. */
static void render (w3ld_gamma *self) {
	const char *icon = self->mode == MODE_DAY ? self->icon_day : self->icon_night;
	GString *out = g_string_new(NULL);
	for (const char *p = self->format; *p; ) {
		if (!strncmp(p, "{icon}", 6)) {
			g_string_append(out, icon ? icon : "");
			p += 6;
		} else if (!strncmp(p, "{temperature}", 13)) {
			g_string_append_printf(out, "%d", self->live_temp);
			p += 13;
		} else if (!strncmp(p, "{brightness}", 12)) {
			g_string_append_printf(out, "%d", self->live_bright);
			p += 12;
		} else {
			g_string_append_c(out, *p);
			p++;
		}
	}
	gtk_button_set_label(GTK_BUTTON(self->button), out->str);
	g_string_free(out, TRUE);

	GtkStyleContext *style = gtk_widget_get_style_context(self->button);
	gtk_style_context_remove_class(style, "day");
	gtk_style_context_remove_class(style, "night");
	gtk_style_context_remove_class(style, "override");
	gtk_style_context_add_class(style, self->mode == MODE_DAY ? "day" : "night");

	/* Off-preset: a manual temperature override is in effect for this mode. */
	int mode_temp = self->mode == MODE_DAY ? self->temp_day : self->temp_night;
	if (self->live_temp != mode_temp)
		gtk_style_context_add_class(style, "override");
}

/* Apply the active mode's preset (its temperature + retained brightness). The
 * resulting broadcast confirms it; render provisionally for a snappy toggle. */
static void apply_mode (w3ld_gamma *self) {
	int temp = self->mode == MODE_DAY ? self->temp_day : self->temp_night;
	int bright = self->mode == MODE_DAY ? self->bright_day : self->bright_night;
	self->live_temp = temp;
	self->live_bright = bright;
	render(self);

	char *command = g_strdup_printf("w3ldctl gamma %d %d", temp, bright);
	g_spawn_command_line_async(command, NULL);
	g_free(command);
}

/* A gamma event from w3ld (any source): reflect it, and adopt the brightness
 * into the active mode so a live edit sticks across toggles. Temperature is the
 * mode's identity and is left untouched. */
static void on_line (const char *line, void *user) {
	w3ld_gamma *self = user;
	if (!strstr(line, "\"ev\":\"gamma\""))
		return;

	self->live_temp = json_get_int(line, "temperature");
	self->live_bright = json_get_int(line, "brightness");
	if (self->mode == MODE_DAY)
		self->bright_day = self->live_bright;
	else
		self->bright_night = self->live_bright;
	render(self);
}

/* Click: flip day <-> night and apply that mode's preset. */
static void on_clicked (
	GtkButton *button,
	gpointer data
) {
	(void)button;
	w3ld_gamma *self = data;
	self->mode = self->mode == MODE_DAY ? MODE_NIGHT : MODE_DAY;
	apply_mode(self);
}

/* Scroll: nudge brightness via w3ld's relative op, so its clamp and the adopt
 * path apply uniformly; the broadcast updates the display. */
static gboolean on_scroll (
	GtkWidget *widget,
	GdkEventScroll *event,
	gpointer data
) {
	(void)widget;
	w3ld_gamma *self = data;
	const char *sign = NULL;

	if (event->direction == GDK_SCROLL_UP)
		sign = "+";
	else if (event->direction == GDK_SCROLL_DOWN)
		sign = "-";
	else if (event->direction == GDK_SCROLL_SMOOTH) {
		/* Touchpads emit many small deltas; step once per unit crossed
		 * (delta_y < 0 is up / brighter). */
		self->scroll_accum += event->delta_y;
		if (self->scroll_accum <= -1.0) {
			sign = "+";
			self->scroll_accum = 0;
		} else if (self->scroll_accum >= 1.0) {
			sign = "-";
			self->scroll_accum = 0;
		}
	}
	if (!sign)
		return TRUE;

	char *command = g_strdup_printf("w3ldctl gamma brightness %s%d",
			sign, self->step);
	g_spawn_command_line_async(command, NULL);
	g_free(command);
	return TRUE;
}

void *wbcffi_init (
	const wbcffi_init_info *init_info,
	const wbcffi_config_entry *config_entries,
	size_t config_entries_len
) {
	w3ld_gamma *self = g_malloc0(sizeof *self);
	self->temp_day = 6500;
	self->temp_night = 4000;
	self->bright_day = 100;
	self->bright_night = 60;
	self->step = 5;
	self->icon_day = g_strdup("☀");
	self->icon_night = g_strdup("☾");
	self->format = g_strdup("{icon} {temperature}K {brightness}%");
	self->mode = MODE_DAY;

	for (size_t i = 0; i < config_entries_len; i++) {
		const char *key = config_entries[i].key;
		const char *value = config_entries[i].value;
		if (!strcmp(key, "temperature-day"))
			self->temp_day = atoi(value);
		else if (!strcmp(key, "temperature-night"))
			self->temp_night = atoi(value);
		else if (!strcmp(key, "brightness-day"))
			self->bright_day = atoi(value);
		else if (!strcmp(key, "brightness-night"))
			self->bright_night = atoi(value);
		else if (!strcmp(key, "step"))
			self->step = atoi(value);
		else if (!strcmp(key, "icon-day")) {
			g_free(self->icon_day);
			self->icon_day = config_string(value);
		} else if (!strcmp(key, "icon-night")) {
			g_free(self->icon_night);
			self->icon_night = config_string(value);
		} else if (!strcmp(key, "format")) {
			g_free(self->format);
			self->format = config_string(value);
		}
	}
	if (self->step < 1)
		self->step = 1;

	GtkContainer *root = init_info->get_root_widget(init_info->obj);
	self->button = gtk_button_new_with_label("");
	gtk_button_set_relief(GTK_BUTTON(self->button), GTK_RELIEF_NONE);
	gtk_widget_set_name(self->button, "w3ld-gamma");
	gtk_widget_add_events(self->button, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
	g_signal_connect(self->button, "clicked", G_CALLBACK(on_clicked), self);
	g_signal_connect(self->button, "scroll-event", G_CALLBACK(on_scroll), self);
	gtk_container_add(root, self->button);
	gtk_widget_show_all(self->button);

	/* Bar start resets to the day preset; the broadcast then confirms it. */
	apply_mode(self);
	status_feed_start(&self->feed, on_line, self);
	return self;
}

void wbcffi_deinit (void *instance) {
	w3ld_gamma *self = instance;
	if (!self)
		return;
	status_feed_stop(&self->feed);
	g_free(self->icon_day);
	g_free(self->icon_night);
	g_free(self->format);
	g_free(self);
}
