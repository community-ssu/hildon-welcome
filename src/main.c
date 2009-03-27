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

#define DEFAULT_VIDEO_PIPELINE_STR " playbin2 uri=file://%s " /* " flags=99 " <-- doesn't work with still images */
#define DEFAULT_AUDIO_PIPELINE_STR " filesrc location=%s ! decodebin2 ! autoaudiosink "
#define DEFAULT_SHUSH_PIPELINE_STR " audiotestsrc ! volume volume=0 ! autoaudiosink "

static char *video_pipeline_str = DEFAULT_VIDEO_PIPELINE_STR;
static char *audio_pipeline_str = DEFAULT_AUDIO_PIPELINE_STR;
static char *shush_pipeline_str = DEFAULT_SHUSH_PIPELINE_STR;

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

static GstElement *
create_pipeline(GstElement **p_audio_bin, GstElement **p_video_bin, GstElement **p_shush_bin)
{
  GstElement *pipeline = NULL;

  pipeline = gst_pipeline_new();

  (*p_audio_bin) = gst_element_make("playbin2", "audio_bin");
  (*p_video_bin) = gst_element_make("playbin2", "video_bin");

  return pipeline;
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
  Window dst_window = 0, xcomposite_window = 0;

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
  if ((dst_window = DefaultRootWindow(display)) == 0)
    g_error("main: Failed to obtain root window\n");

  if ((xcomposite_window = XCompositeGetOverlayWindow(display, dst_window)) != 0)
    dst_window = xcomposite_window;

  if ((itr = conf_file_iterator_new())) {
    while (conf_file_iterator_get(itr, &video, &audio, &duration)) {
      g_debug("main: video = %s, audio = %s, duration = %d\n", video, audio, duration);
      g_free(video); video = NULL;
      g_free(audio); audio = NULL;
      duration = 0;
    }
    conf_file_iterator_destroy(itr);
  }

  if (xcomposite_window)
    XCompositeReleaseOverlayWindow(display, xcomposite_window);

  XCloseDisplay(display);

  gst_deinit();

  return 0;
}
