/* honey-gamma: a waybar CFFI plugin controlling honey's integrated night light.
 *
 * A day/night toggle over two presets. honey owns the current applied gamma and
 * broadcasts every change on its status stream, so this module is a live
 * reflector: click toggles between day and night, scroll adjusts brightness,
 * and the display always mirrors honey's real state; however it was driven
 * (this module, a hotkey, or a direct `honeyctl gamma`).
 *
 * Asymmetric by design. Temperature is the mode's identity (day = neutral,
 * night = warm) and comes only from config, restored on every toggle.
 * Brightness is the ridable trim: scroll changes it, and any live change is
 * adopted into the active mode so it survives toggles; until the bar restarts,
 * when in-memory state resets to the configured defaults.
 *
 * The widget is an event box (no button hover chrome) holding two labels so the
 * icon and the value can be styled independently:
 *   #honey-gamma            the module (carries the day / night / override class)
 *   #honey-gamma-icon       the {icon} glyph  (e.g. colour it purple)
 *   #honey-gamma-value      the temperature / brightness text (default colour)
 *
 * Config keys (waybar module JSON):
 *   temperature-day / temperature-night   mode temperatures in Kelvin
 *   brightness-day  / brightness-night    starting per-mode brightness (percent)
 *   step                                  scroll step (percent)
 *   icon-day        / icon-night          per-mode glyph for {icon}
 *   format          tokens {icon} {temperature} {brightness}; omit any to hide
 *
 * The brightness clamp (min/max) lives in honey (`honeyctl gamma min|max <pct>`),
 * so it is universal to scroll and hotkeys alike.
 */
#include "waybar_cffi_module.h"

#include "status_feed.h"
#include "status_json.h"
#include "waybar_config.h"

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
	int live_temp, live_bright;     /* last values reported by honey (display) */
	double scroll_accum;            /* smooth-scroll accumulator */

	GtkWidget *root;                /* event box (#honey-gamma) */
	GtkWidget *icon;                /* #honey-gamma-icon */
	GtkWidget *value;               /* #honey-gamma-value */
	status_feed feed;
} honey_gamma;

/* Redraw the labels from the format template and set the day/night (+ override)
 * class. {icon} goes to the icon label; {temperature}/{brightness} and any
 * literals go to the value label, so the two can be coloured separately. */
static void render (honey_gamma *self) {
	const char *glyph = self->mode == MODE_DAY ? self->icon_day : self->icon_night;
	GString *icon = g_string_new(NULL);
	GString *value = g_string_new(NULL);
	for (const char *p = self->format; *p; ) {
		if (!strncmp(p, "{icon}", 6)) {
			g_string_append(icon, glyph ? glyph : "");
			p += 6;
		} else if (!strncmp(p, "{temperature}", 13)) {
			g_string_append_printf(value, "%d", self->live_temp);
			p += 13;
		} else if (!strncmp(p, "{brightness}", 12)) {
			g_string_append_printf(value, "%d", self->live_bright);
			p += 12;
		} else {
			g_string_append_c(value, *p);
			p++;
		}
	}
	gtk_label_set_text(GTK_LABEL(self->icon), icon->str);
	gtk_label_set_text(GTK_LABEL(self->value), value->str);
	gtk_widget_set_visible(self->icon, icon->len > 0);
	gtk_widget_set_visible(self->value, value->len > 0);
	g_string_free(icon, TRUE);
	g_string_free(value, TRUE);

	GtkStyleContext *style = gtk_widget_get_style_context(self->root);
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
static void apply_mode (honey_gamma *self) {
	int temp = self->mode == MODE_DAY ? self->temp_day : self->temp_night;
	int bright = self->mode == MODE_DAY ? self->bright_day : self->bright_night;
	self->live_temp = temp;
	self->live_bright = bright;
	render(self);

	char *command = g_strdup_printf("honeyctl gamma %d %d", temp, bright);
	g_spawn_command_line_async(command, NULL);
	g_free(command);
}

/* A gamma event from honey (any source): reflect it, and adopt the brightness
 * into the active mode so a live edit sticks across toggles. Temperature is the
 * mode's identity and is left untouched. */
static void on_line (const char *line, void *user) {
	honey_gamma *self = user;
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

/* Left click: flip day <-> night and apply that mode's preset. */
static gboolean on_button_press (
	GtkWidget *widget,
	GdkEventButton *event,
	gpointer data
) {
	(void)widget;
	if (event->button != 1)
		return FALSE;
	honey_gamma *self = data;
	self->mode = self->mode == MODE_DAY ? MODE_NIGHT : MODE_DAY;
	apply_mode(self);
	return TRUE;
}

/* Scroll: nudge brightness via honey's relative op, so its clamp and the adopt
 * path apply uniformly; the broadcast updates the display. */
static gboolean on_scroll (
	GtkWidget *widget,
	GdkEventScroll *event,
	gpointer data
) {
	(void)widget;
	honey_gamma *self = data;
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

	char *command = g_strdup_printf("honeyctl gamma brightness %s%d",
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
	honey_gamma *self = g_malloc0(sizeof *self);
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
	self->root = gtk_event_box_new();
	gtk_widget_set_name(self->root, "honey-gamma");
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(self->root), TRUE);
	gtk_widget_add_events(self->root,
			GDK_BUTTON_PRESS_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
	g_signal_connect(self->root, "button-press-event",
			G_CALLBACK(on_button_press), self);
	g_signal_connect(self->root, "scroll-event", G_CALLBACK(on_scroll), self);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	self->icon = gtk_label_new("");
	gtk_widget_set_name(self->icon, "honey-gamma-icon");
	self->value = gtk_label_new("");
	gtk_widget_set_name(self->value, "honey-gamma-value");
	gtk_box_pack_start(GTK_BOX(box), self->icon, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), self->value, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(self->root), box);
	gtk_container_add(root, self->root);
	gtk_widget_show_all(self->root);

	/* Bar start resets to the day preset; the broadcast then confirms it. */
	apply_mode(self);
	status_feed_start(&self->feed, on_line, self);
	return self;
}

void wbcffi_deinit (void *instance) {
	honey_gamma *self = instance;
	if (!self)
		return;
	status_feed_stop(&self->feed);
	g_free(self->icon_day);
	g_free(self->icon_night);
	g_free(self->format);
	g_free(self);
}
