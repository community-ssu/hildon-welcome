/*
 * This file is part of hildon-welcome 
 *
 * Copyright (C) 2009 Nokia Corporation.
 * 
 * Author: Gabriel Schulhof <gabriel.schulhof@nokia.com>
 * Contact: Karoliina T. Salminen <karoliina.t.salminen@nokia.com>
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <gst/interfaces/xoverlay.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <gst/gst.h>
#ifdef HAVE_MCE
# include <dbus/dbus.h>
# include <mce/dbus-names.h>
#endif /* HAVE_MCE */
#include "conffile.h"

#define KILL_TO_LENGTH_MS 60000

#define DEFAULT_VIDEO_PIPELINE_STR " playbin2 uri=file://%s " /* " flags=99 " <-- doesn't work with still images */
#define DEFAULT_AUDIO_PIPELINE_STR " filesrc location=%s ! decodebin2 ! autoaudiosink "
#define DEFAULT_SHUSH_PIPELINE_STR " audiotestsrc ! volume volume=0 ! autoaudiosink "

static char *video_pipeline_str = DEFAULT_VIDEO_PIPELINE_STR;
static char *audio_pipeline_str = DEFAULT_AUDIO_PIPELINE_STR;
static char *shush_pipeline_str = DEFAULT_SHUSH_PIPELINE_STR;
static Window dst_window = 0;

#define TIMEOUT_PARAMS_STATIC_INIT { \
  .timer = NULL,                     \
  .to_ms = 0,                        \
  .pipeline = NULL,                  \
  .timeout_id = 0,                   \
  .warning = NULL                    \
}

typedef struct
{
  GTimer *timer;
  guint to_ms;
  GstElement *pipeline;
  guint timeout_id;
  const char *warning;
} TimeoutParams;

static void
my_log_func(const gchar *log_domain, GLogLevelFlags log_level, const char *message, gpointer null)
{
  char *new_msg = NULL;
  static GTimer *timer = NULL;

  if (G_UNLIKELY(!timer))
    timer = g_timer_new();

  new_msg = g_strdup_printf("[%lf]: %s", timer ? g_timer_elapsed(timer, NULL) : G_MAXDOUBLE, message);
  g_log_default_handler (log_domain, log_level, (const char *)new_msg, NULL);
  g_free(new_msg);
}

static Window
get_dst_window(Display *dpy) {
  Window ret = 0, xcomposite_window = 0;
  if ((ret = DefaultRootWindow(dpy)) != 0)
    if ((xcomposite_window = XCompositeGetOverlayWindow(dpy, ret)) != 0) {
      g_debug("get_dst_window: Acquired XComposite overlay window %d\n", (int)xcomposite_window);
      ret = xcomposite_window;
    }

  return ret;
}

static Window
release_dst_window(Display *dpy, Window wnd)
{
  Window window = 0;
  if ((window = DefaultRootWindow(dpy)) != 0)
    if (wnd != window) {
      g_debug("release_dst_window: Releasing XComposite overlay window %d\n", (int)wnd);
      XCompositeReleaseOverlayWindow(dpy, wnd);
    }

  return (Window)0;
}

static gboolean
post_eos(TimeoutParams *tp)
{
  if (tp->timer) {
    double diff_ms = ((double)(tp->to_ms)) - (g_timer_elapsed(tp->timer, NULL) * 1000.0);
    if (diff_ms > 0) {
      g_warning("post_eos: False alarm! %lf ms () left\n", diff_ms);
      tp->timeout_id = g_timeout_add((guint)diff_ms, (GSourceFunc)post_eos, tp);
      return FALSE;
    }
  }
  if (tp->warning) {
    g_warning("post_eos: FATAL: Exiting: cannot play further logos: %s", tp->warning);
    _Exit(1);
  }
  gst_bus_post(gst_pipeline_get_bus(GST_PIPELINE(tp->pipeline)), gst_message_new_eos(GST_OBJECT(tp->pipeline)));

  tp->timeout_id = 0;
  if (tp->timer) {
    g_timer_destroy(tp->timer);
    tp->timer = NULL;
  }
  return FALSE;
}

static void
post_eos_timeout_add(guint to_ms, GstElement *pipeline, char *warning, TimeoutParams *params)
{
  params->to_ms = to_ms;
  params->pipeline = pipeline;
  params->warning = warning;
  params->timer = g_timer_new();
  params->timeout_id = g_timeout_add(to_ms, (GSourceFunc)post_eos, params);
}

static void
post_eos_timeout_remove(TimeoutParams *params)
{
  if (params->timeout_id) {
    g_source_remove(params->timeout_id);
    params->timeout_id = 0;
  }
  if (params->timer) {
    g_timer_destroy(params->timer);
    params->timer = NULL;
  }
}

static void
maybe_fork()
{
  int fork_result = -1;
  static gboolean already_forked = FALSE;

  if (!already_forked) {
    if ((fork_result = fork())) return 0;
    else {
      g_debug("maybe_fork: Forked into the background with fork_result %d\n", fork_result);
      already_forked = TRUE;
    }
  }
}

static void
wait_for_eos(GstElement *pipeline, Display *dpy, int duration, TimeoutParams *play_to)
{
  GError *err = NULL;
  char *debug = NULL;
  gboolean keep_looping = TRUE;
  GstMessage *message = NULL;

  while (keep_looping) {
    message = gst_bus_poll(gst_pipeline_get_bus(GST_PIPELINE(pipeline)), GST_MESSAGE_ANY, -1);
    if (!message) break;

    switch(GST_MESSAGE_TYPE(message)) {
      case GST_MESSAGE_ASYNC_DONE:
        g_debug("wait_for_eos: Ready to play: duration = %d\n", duration);
        maybe_fork();
        if ((duration > 500) && !(play_to->timeout_id))
          post_eos_timeout_add(duration, pipeline, NULL, play_to);
        break;

      case GST_MESSAGE_ERROR:
        gst_message_parse_error(message, &err, &debug);
        g_warning("wait_for_eos: %s: %s %s\n", GST_MESSAGE_TYPE_NAME(message), err ? err->message : "", debug ? debug : "");
        if (err)
          g_error_free(err);
        g_free(debug);
        /* fall through */
      case GST_MESSAGE_EOS:
        keep_looping = FALSE;
        gst_element_set_state(pipeline, GST_STATE_PAUSED);
        g_debug("wait_for_eos: Gst message: %s", GST_MESSAGE_TYPE_NAME(message));
        break;

      case GST_MESSAGE_ELEMENT:
        if (gst_structure_has_name(message->structure, "prepare-xwindow-id")) {
          if (0 == dst_window)
            dst_window = get_dst_window(dpy);
          if (dst_window)
            gst_x_overlay_set_xwindow_id(GST_X_OVERLAY(GST_MESSAGE_SRC(message)), dst_window);
        }
        break;

      default:
        break;
    }
    gst_message_unref(message);
  }
}

static void
unblank_screen()
{
#ifdef HAVE_MCE
  DBusConnection *conn = NULL;

  if ((conn = dbus_bus_get(DBUS_BUS_SYSTEM, NULL)) != NULL) {
    DBusMessage *message, *reply;

    if ((message = dbus_message_new_method_call(MCE_SERVICE, MCE_REQUEST_PATH, MCE_REQUEST_IF, MCE_DISPLAY_ON_REQ)) != NULL) {
      if ((reply = dbus_connection_send_with_reply_and_block(conn, message, -1, NULL)) != NULL)
        dbus_message_unref(reply);
      dbus_message_unref(message);
    }
  }
#endif /* HAVE_MCE */
}

static GstElement *
play_logo(Display *dpy, char *video, char *audio, int duration)
{
  GstElement* pipeline = NULL;
  GString *pipeline_str = g_string_new("");

  g_debug("play_logo: playing (video = '%s', audio = '%s', duration = '%d')", video, audio, duration);

  if (video && video[0])
    g_string_append_printf(pipeline_str, video_pipeline_str, video);

  if (audio && audio[0]) {
    if ('s' == audio[0] && 0 == audio[1])
      g_string_append_printf(pipeline_str, shush_pipeline_str);
    else
      g_string_append_printf(pipeline_str, audio_pipeline_str, audio);
  }

  pipeline = gst_parse_launch(pipeline_str->str, NULL);
  g_string_free(pipeline_str, TRUE);

  if (pipeline) {
    TimeoutParams kill_to = TIMEOUT_PARAMS_STATIC_INIT,
                  play_to = TIMEOUT_PARAMS_STATIC_INIT;

    post_eos_timeout_add(KILL_TO_LENGTH_MS, pipeline, "Absolute timeout reached!\n", &kill_to);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    unblank_screen();
    wait_for_eos(pipeline, dpy, duration, &play_to);

    post_eos_timeout_remove(&kill_to);
    post_eos_timeout_remove(&play_to);
  }

  return pipeline;
}

/* Paint a black filled rectangle over the given window */
static void
draw_black(Display *dpy, Window wnd)
{
  XGCValues vals;
  Window root_window;
  int x, y;
  unsigned int cx, cy, cx_border, depth;

  if (!XGetGeometry(dpy, wnd, &root_window, &x, &y, &cx, &cy, &cx_border, &depth)) {
     x =   0;  y =   0;
    cx = 800; cy = 480;
  }

  vals.foreground = BlackPixel(dpy, 0);
  vals.background = BlackPixel(dpy, 0);
  GC gc = XCreateGC(dpy, wnd, GCForeground | GCBackground, &vals);
  XFillRectangle(dpy, wnd, gc, x, y, cx, cy);
  XFreeGC(dpy, gc);
  XFlush(dpy);
}

int
main(int argc, char **argv)
{
  GOptionEntry options[] = {
    { 
      .long_name = "audio", 
      .short_name = 'a',
      .flags = G_OPTION_FLAG_OPTIONAL_ARG, 
      .arg = G_OPTION_ARG_STRING,
      .arg_data = &audio_pipeline_str,
      .description = "Audio pipeline string. May contain %s for the filename.",
      .arg_description = "'" DEFAULT_AUDIO_PIPELINE_STR "'"
    },
    {
      .long_name = "video", 
      .short_name = 'v',
      .flags = G_OPTION_FLAG_OPTIONAL_ARG, 
      .arg = G_OPTION_ARG_STRING,
      .arg_data = &video_pipeline_str,
      .description = "Video pipeline string. May contain %s for the filename.",
      .arg_description = "'" DEFAULT_VIDEO_PIPELINE_STR "'"
    },
    {
      .long_name = "silence", 
      .short_name = 's',
      .flags = G_OPTION_FLAG_OPTIONAL_ARG, 
      .arg = G_OPTION_ARG_STRING,
      .arg_data = &shush_pipeline_str,
      .description = "Silence pipeline string.",
      .arg_description = "'" DEFAULT_SHUSH_PIPELINE_STR "'"
    },
    { NULL }
  };

  GOptionContext *ctx;
  GError *err;
  Display *display = NULL;
  char *video = NULL, *audio = NULL;
  int duration;
  ConfFileIterator *itr;
  GstElement *new_pipeline = NULL, *old_pipeline = NULL;

  g_setenv("PULSE_PROP_media.role", "animation", TRUE);

  if (!g_thread_supported ()) g_thread_init(NULL);

  g_log_set_default_handler(my_log_func, NULL);

  ctx = g_option_context_new(NULL);
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group());
  if (!g_option_context_parse (ctx, &argc, &argv, &err))
    g_error ("main: Error parsing command line: %s\n", err ? err->message : "Unknown error\n");
  g_option_context_free (ctx);

  gst_init(&argc, &argv);

  if (!(display = XOpenDisplay(NULL)))
    g_error("main: Failed to open display\n");

  if ((itr = conf_file_iterator_new())) {
    while (conf_file_iterator_get(itr, &video, &audio, &duration)) {
      new_pipeline = play_logo(display, video, audio, duration);
      g_free(video); video = NULL;
      g_free(audio); audio = NULL;
      duration = 0;
      if (old_pipeline) {
        gst_element_set_state(old_pipeline, GST_STATE_NULL);
        gst_object_unref(old_pipeline);
      }
      old_pipeline = new_pipeline;
    }
    conf_file_iterator_destroy(itr);
  }

  /* Prevent the green flash before the application quits */
  if (dst_window)
    draw_black(display, dst_window);

  gst_element_set_state(new_pipeline, GST_STATE_NULL);
  gst_object_unref(new_pipeline);

  if (dst_window)
    release_dst_window(display, dst_window);

  XCloseDisplay(display);

  gst_deinit();

  return 0;
}
