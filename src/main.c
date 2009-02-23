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

#define KILL_TO_LENGTH_MS 30000

typedef struct
{
  GstElement *pipeline;
  guint timeout_id;
  char *error;
} TimeoutParams;

static gboolean
post_eos(TimeoutParams *tp)
{
  if (tp->error)
    g_error("%s", tp->error);
  gst_bus_post(gst_pipeline_get_bus(GST_PIPELINE(tp->pipeline)), gst_message_new_eos(GST_OBJECT(tp->pipeline)));
  tp->timeout_id = 0;
  return FALSE;
}

static void
wait_for_eos(GstElement *pipeline, Window dst_window, int duration, TimeoutParams *play_to)
{
  GstMessage *message = NULL;

  while ((message = gst_bus_poll(gst_pipeline_get_bus(GST_PIPELINE(pipeline)), GST_MESSAGE_ANY, -1))) {
    if ((GST_MESSAGE_ASYNC_DONE == GST_MESSAGE_TYPE(message) ||
         GST_MESSAGE_DURATION   == GST_MESSAGE_TYPE(message)) && 
        (duration > 500)) {
      g_debug("GST_MESSAGE_DURATION = %d", duration);
      play_to->timeout_id = g_timeout_add(duration, (GSourceFunc)post_eos, play_to);
    } 
    else 
    if ((GST_MESSAGE_EOS        == GST_MESSAGE_TYPE(message)) ||
        (GST_MESSAGE_ERROR      == GST_MESSAGE_TYPE(message))) {
      /* on error we need to skip broken media file */
      if (GST_MESSAGE_ERROR == GST_MESSAGE_TYPE(message)) {
        GError *err = NULL;
        char *debug = NULL;

        gst_message_parse_error(message, &err, &debug);
        g_warning("wait_for_eos: %s: %s %s\n", GST_MESSAGE_TYPE_NAME(message),
          err ? err->message : "", debug ? debug : "");
        if (err)
          g_error_free(err);
        g_free(debug);
      }
      else
        g_debug("Gst message: %s", GST_MESSAGE_TYPE_NAME(message));

      gst_element_set_state(pipeline, GST_STATE_PAUSED);
      gst_message_unref(message);
      break;
    } 
    else 
    if (GST_MESSAGE_ELEMENT     == GST_MESSAGE_TYPE(message) && 
        gst_structure_has_name(message->structure, "prepare-xwindow-id"))
      gst_x_overlay_set_xwindow_id(GST_X_OVERLAY(GST_MESSAGE_SRC(message)), dst_window);

    gst_message_unref(message);
  }
}

static void
unblank_screen()
{
#ifdef HAVE_MCE
  DBusError err;
  DBusConnection *conn = NULL;

  dbus_error_init(&err);
  if ((conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err)) != NULL) {
    DBusMessage *message = dbus_message_new_method_call(MCE_SERVICE, MCE_REQUEST_PATH, MCE_REQUEST_IF, MCE_DISPLAY_ON_REQ);
    if (message) {
      dbus_connection_send(conn, message, NULL);
      dbus_connection_flush(conn);
    }
  }

  if (dbus_error_is_set(&err))
    g_warning("unblank_screen: %s: %s\n", err.name, err.message);
  dbus_error_free(&err);
#endif /* HAVE_MCE */
}

static GstElement *
play_logo(Window dst_window, char *video, char *audio, int duration)
{
  GstElement* pipeline = NULL;
  GString *pipeline_str = g_string_new("");

  g_debug("PLAYING (video = '%s', audio = '%s', duration = '%d')", video, audio, duration);

  /*
   * "flags=99" for playbin2 is an optimization suggested by Stefan Kost. "flags" is a bitfield. The values are described at
   * http://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-playbin2.html#GstPlayFlags
   *
   */
  if (video && video[0])
    g_string_append_printf(pipeline_str, " playbin2 uri=file://%s flags=99", video);

  if (audio && audio[0]) {
    if ('s' == audio[0] && 0 == audio[1])
      g_string_append_printf(pipeline_str, " audiotestsrc ! volume volume=0 ! autoaudiosink ");
    else
      g_string_append_printf(pipeline_str, " filesrc location=%s ! decodebin2 ! autoaudiosink ", audio);
  }

  pipeline = gst_parse_launch(pipeline_str->str, NULL);
  g_string_free(pipeline_str, TRUE);

  if (pipeline) {
    TimeoutParams kill_to = { pipeline, 0, "Absolute timeout reached!\n" }, play_to = { pipeline, 0, NULL };

    kill_to.timeout_id = g_timeout_add(KILL_TO_LENGTH_MS, (GSourceFunc)post_eos, &kill_to);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    unblank_screen();
    wait_for_eos(pipeline, dst_window, duration, &play_to);

    if (kill_to.timeout_id) g_source_remove(kill_to.timeout_id);
    if (play_to.timeout_id) g_source_remove(play_to.timeout_id);
  }

  return pipeline;
}

/* Paint a black filled rectangle over the given window */
static void
draw_black(Display *dpy, Window dst_window)
{
  XGCValues vals;
  Window root_window;
  int x, y;
  unsigned int cx, cy, cx_border, depth;

  if (!XGetGeometry(dpy, dst_window, &root_window, &x, &y, &cx, &cy, &cx_border, &depth)) {
     x =   0;  y =   0;
    cx = 800; cy = 480;
  }

  vals.foreground = BlackPixel(dpy, 0);
  vals.background = BlackPixel(dpy, 0);
  GC gc = XCreateGC(dpy, dst_window, GCForeground | GCBackground, &vals);
  XFillRectangle(dpy, dst_window, gc, x, y, cx, cy);
  XFreeGC(dpy, gc);
  XFlush(dpy);
}

int
main(int argc, char **argv)
{
  Display *display = NULL;
  char *video = NULL, *audio = NULL;
  int duration;
  ConfFileIterator *itr;
  Window dst_window = 0, xcomposite_window = 0;
  GstElement *new_pipeline = NULL, *old_pipeline = NULL;

  if (!(display = XOpenDisplay(NULL)))
    g_error("Failed to open display\n");
  if ((dst_window = DefaultRootWindow(display)) == 0)
    g_error("Failed to obtain root window\n");

  if ((xcomposite_window = XCompositeGetOverlayWindow(display, dst_window)) != 0)
    dst_window = xcomposite_window;

  gst_init(&argc, &argv);

  if ((itr = conf_file_iterator_new())) {
    while (conf_file_iterator_get(itr, &video, &audio, &duration)) {
      new_pipeline = play_logo(dst_window, video, audio, duration);
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
  draw_black(display, dst_window);
  gst_element_set_state(new_pipeline, GST_STATE_NULL);
  gst_object_unref(new_pipeline);

  if (xcomposite_window)
    XCompositeReleaseOverlayWindow(display, xcomposite_window);

  XCloseDisplay(display);

  gst_deinit();

  return 0;
}
