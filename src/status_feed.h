/* status_feed.h: shared status-stream plumbing for the w3ld waybar plugins.
 *
 * Connects to w3ld's control socket ($XDG_RUNTIME_DIR/w3ld-$WAYLAND_DISPLAY.sock),
 * sends `subscribe`, and delivers each newline-delimited JSON line to a callback
 * on the GTK main loop. If w3ld is not up yet, or later restarts, the feed keeps
 * retrying until it reconnects. Both the workspaces and window plugins embed one.
 */
#pragma once

#include <glib.h>
#include <gio/gio.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define STATUS_FEED_RECONNECT_SECONDS 2

typedef struct {
	int fd;                  /* status socket, or -1 */
	GIOChannel *channel;     /* wraps fd while connected, else NULL */
	guint watch_id;          /* socket watch, or 0 */
	guint reconnect_id;      /* retry timer, or 0 */
	void (*on_line)(const char *line, void *user);
	void *user;
} status_feed;

/* Connect to w3ld's control socket and subscribe. Returns the fd, or -1. */
static inline int status_feed_connect (void) {
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	const char *wayland_display = getenv("WAYLAND_DISPLAY");
	if (!runtime_dir || !wayland_display)
		return -1;

	struct sockaddr_un address = { .sun_family = AF_UNIX };
	int written = snprintf(address.sun_path, sizeof address.sun_path,
			"%s/w3ld-%s.sock", runtime_dir, wayland_display);
	if (written < 0 || (size_t)written >= sizeof address.sun_path)
		return -1;

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	if (connect(fd, (struct sockaddr *)&address, sizeof address) < 0
			|| write(fd, "subscribe\n", 10) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static gboolean status_feed_readable (GIOChannel *, GIOCondition, gpointer);
static void status_feed_schedule_reconnect (status_feed *feed);

/* Attempt a connection; on success wrap the socket and watch it. No-op when
 * already connected. Returns TRUE once connected. */
static inline gboolean status_feed_ensure (status_feed *feed) {
	if (feed->channel)
		return TRUE;
	int fd = status_feed_connect();
	if (fd < 0)
		return FALSE;
	feed->fd = fd;
	feed->channel = g_io_channel_unix_new(fd);
	g_io_channel_set_flags(feed->channel, G_IO_FLAG_NONBLOCK, NULL);
	g_io_channel_set_close_on_unref(feed->channel, TRUE);
	feed->watch_id = g_io_add_watch(feed->channel,
			G_IO_IN | G_IO_HUP | G_IO_ERR, status_feed_readable, feed);
	return TRUE;
}

static inline gboolean status_feed_on_reconnect (gpointer data) {
	status_feed *feed = data;
	if (status_feed_ensure(feed)) {
		feed->reconnect_id = 0;
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static inline void status_feed_schedule_reconnect (status_feed *feed) {
	if (!feed->reconnect_id)
		feed->reconnect_id = g_timeout_add_seconds(
				STATUS_FEED_RECONNECT_SECONDS, status_feed_on_reconnect, feed);
}

/* GTK main-loop callback: drain complete lines and hand each to on_line. On
 * disconnect, drop the channel and start retrying. */
static inline gboolean status_feed_readable (
	GIOChannel *source,
	GIOCondition condition,
	gpointer data
) {
	status_feed *feed = data;
	if (condition & (G_IO_HUP | G_IO_ERR)) {
		feed->watch_id = 0; /* removed by returning below */
		g_io_channel_unref(feed->channel);
		feed->channel = NULL;
		feed->fd = -1;
		status_feed_schedule_reconnect(feed);
		return G_SOURCE_REMOVE;
	}

	gchar *line = NULL;
	while (g_io_channel_read_line(source, &line, NULL, NULL, NULL)
			== G_IO_STATUS_NORMAL) {
		feed->on_line(line, feed->user);
		g_free(line);
		line = NULL;
	}
	g_free(line);
	return G_SOURCE_CONTINUE;
}

/* Begin feeding lines to on_line(.., user); retries until w3ld is reachable. */
static inline void status_feed_start (
	status_feed *feed,
	void (*on_line)(const char *line, void *user),
	void *user
) {
	feed->fd = -1;
	feed->on_line = on_line;
	feed->user = user;
	if (!status_feed_ensure(feed))
		status_feed_schedule_reconnect(feed);
}

static inline void status_feed_stop (status_feed *feed) {
	if (feed->reconnect_id)
		g_source_remove(feed->reconnect_id);
	if (feed->watch_id)
		g_source_remove(feed->watch_id);
	if (feed->channel) {
		g_io_channel_shutdown(feed->channel, FALSE, NULL);
		g_io_channel_unref(feed->channel); /* closes fd (close-on-unref) */
	} else if (feed->fd >= 0) {
		close(feed->fd);
	}
}
