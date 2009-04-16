#include <string.h>
#include <stdlib.h>
#include <gst/gst.h>

/*
 * Grab the XComposite overlay window or, failing that, the root window,
 * instead of having GStreamer create a window.
 */
#define GET_WINDOW

#define APP_STATE_STATIC_INIT { \
  .video_src = NULL, \
  .audio_src = NULL, \
  .audio_sink = NULL, \
  .video_sink = NULL, \
  .dst_window = 0, \
  .overlay_window = 0, \
  .display = NULL, \
}

#include <gst/interfaces/xoverlay.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>

typedef struct {
  GstElement *video_src;
  GstElement *audio_src;
  GstElement *audio_sink;
  GstElement *video_sink;
  Window dst_window;
  Window overlay_window;
  Display *display;
} AppState;

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

static void
gst_element_get_has_audio_video(GstElement *src, gboolean *p_has_audio, gboolean *p_has_video)
{
  GstIterator *itr = NULL;
  GstPad *pad = NULL;
  GstCaps *caps = NULL;
  GstStructure *cap_struct = NULL;
  const char *name = NULL;

  (*p_has_audio) = FALSE;
  (*p_has_video) = FALSE;

  if ((itr = gst_element_iterate_src_pads(src)) != NULL) {
    while (GST_ITERATOR_OK == gst_iterator_next(itr, ((gpointer *)(&pad)))) {
      if ((caps = gst_pad_get_caps(pad)) != NULL) {
        if (gst_caps_get_size(caps) > 0) {
          if ((cap_struct = gst_caps_get_structure(caps, 0)) != NULL) {
            name = gst_structure_get_name(cap_struct);
            if (name) {
              if (!strncmp(name, "audio", 5))
                (*p_has_audio) = TRUE;
              else
              if (!strncmp(name, "video", 5))
                (*p_has_video) = TRUE;
            }
          }
        }
        gst_caps_unref(caps);
      }
      gst_object_unref(pad);
    }
    gst_iterator_free(itr);
  }
}

static void
link_to_sink(GstElement *decodebin2, AppState *app_state)
{
  g_debug("link_to_sink: %s has finished pad creation\n", gst_element_get_name(decodebin2));
  g_object_set_data(G_OBJECT(decodebin2), "pad-creation-finished", GINT_TO_POINTER(1));

  if (((!(app_state->video_src)) || GPOINTER_TO_INT(g_object_get_data(G_OBJECT(app_state->video_src), "pad-creation-finished"))) && 
      ((!(app_state->audio_src)) || GPOINTER_TO_INT(g_object_get_data(G_OBJECT(app_state->audio_src), "pad-creation-finished")))) {
    gboolean video_has_video = FALSE, video_has_audio = FALSE,
             audio_has_video = FALSE, audio_has_audio = FALSE;

    if (app_state->video_src) {
      gst_element_get_has_audio_video(app_state->video_src, &video_has_audio, &video_has_video);
      g_debug("link_to_sink: video_src A/V status: audio = %s, video = %s\n", video_has_audio ? "TRUE" : "FALSE", video_has_video ? "TRUE" : "FALSE");

      if (video_has_video) {
        g_debug("link_to_sink: Linking video_src to video_sink\n");
        gst_element_link(app_state->video_src, app_state->video_sink);
      }
      if (video_has_audio) {
        g_debug("link_to_sink: Linking video_src to audio_sink\n");
        gst_element_link(app_state->video_src, app_state->audio_sink);
      }
    }
    if (app_state->audio_src) {
      gst_element_get_has_audio_video(app_state->audio_src, &audio_has_audio, &audio_has_video);
      g_debug("link_to_sink: audio_src A/V status: audio = %s, video = %s\n", audio_has_audio ? "TRUE" : "FALSE", audio_has_video ? "TRUE" : "FALSE");

      if (audio_has_video && !video_has_video) {
        g_debug("link_to_sink: Linking audio_src to video_sink\n");
        gst_element_link(app_state->audio_src, app_state->video_sink);
      }
      if (audio_has_audio && !video_has_audio) {
        g_debug("link_to_sink: Linking audio_src to audio_sink\n");
        gst_element_link(app_state->audio_src, app_state->audio_sink);
      }
    }
  }
}

static GstElement *
create_bin(char *video, char *audio, guint duration, AppState *app_state)
{
  static int counter = 0;
  char *name = NULL;
  GstElement *bin, *filesrc, *decodebin2;

  app_state->video_src = NULL;
  app_state->audio_src = NULL;

  name = g_strdup_printf("bin%d", counter++);
  bin = gst_bin_new(name);
  g_free(name);

  if (video && video[0]) {
    filesrc = gst_element_factory_make("filesrc", "video-src");
    g_object_set(G_OBJECT(filesrc), "location", video, NULL);
    decodebin2 = gst_element_factory_make("decodebin", "video-decoded-src");
    g_signal_connect(G_OBJECT(decodebin2), "no-more-pads", (GCallback)link_to_sink, app_state);
    gst_bin_add_many(GST_BIN(bin), filesrc, decodebin2, NULL);
    gst_element_link(filesrc, decodebin2);
    app_state->video_src = decodebin2;
  }

  if (audio && audio[0]) {
    if (1 == strlen(audio) && 's' == audio[0]) {
      filesrc = gst_element_factory_make("audiotestsrc", "audio-src");
      decodebin2 = gst_element_factory_make("volume", "audio-decoded-src");
      g_object_set(G_OBJECT(decodebin2), "volume", 0, NULL);
      g_signal_connect(G_OBJECT(decodebin2), "no-more-pads", (GCallback)link_to_sink, app_state);
      gst_bin_add_many(GST_BIN(bin), filesrc, decodebin2, NULL);
      gst_element_link(filesrc, decodebin2);
    }
    else {
      filesrc = gst_element_factory_make("filesrc", "audio-src");
      g_object_set(G_OBJECT(filesrc), "location", audio, NULL);
      decodebin2 = gst_element_factory_make("decodebin", "audio-decoded-src");
      g_signal_connect(G_OBJECT(decodebin2), "no-more-pads", (GCallback)link_to_sink, app_state);
      gst_bin_add_many(GST_BIN(bin), filesrc, decodebin2, NULL);
      gst_element_link(filesrc, decodebin2);
    }
    app_state->audio_src = decodebin2;
  }

  return bin;
}

static void
dump_error(GstMessage *msg) {
  GError *err = NULL;
  gchar *debug = NULL;

  gst_message_parse_error(msg, &err, &debug);

  if (err) {
    g_debug("dump_error: error: %s, debug: %s\n", err->message ? err->message : "?", debug ? debug : "?");
    g_error_free(err);
    err = NULL;
  }
  g_free(debug);
  debug = NULL;;
}

static gboolean
send_seek_event(GstElement *pipeline, const char *message, gboolean seek_sent)
{
  if (!seek_sent) {

    g_debug("%s: About to send seek\n", message);

    seek_sent = gst_element_send_event(pipeline,
      gst_event_new_seek(1.0, GST_FORMAT_TIME,
        GST_SEEK_FLAG_SEGMENT | GST_SEEK_FLAG_FLUSH,
        GST_SEEK_TYPE_SET,  G_GUINT64_CONSTANT(0),
        GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE));

    g_debug("%s: Sending seek %s\n", message, seek_sent ? "SUCCEEDED" : "FAILED");

    return seek_sent;
  }
  else
    g_debug("%s: Already sent seek\n", message);

  return TRUE;
}

static void
play_file(GstElement *pipeline, AppState *app_state, char *video, char *audio, guint duration)
{
  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));

  g_debug("play_file: === Entering with (video = \"%s\", audio = \"%s\", duration = %d)\n", video, audio, duration);

  if (bus) {
    gboolean seek_sent = FALSE;
    gboolean keep_looping = TRUE;
    GstMessage *msg = NULL;
    GstElement *new_bin = NULL;

    new_bin = create_bin(video, audio, duration, app_state);
    gst_bin_add(GST_BIN(pipeline), new_bin);
    gst_element_set_state(pipeline, GST_STATE_PAUSED);
//    seek_sent = send_seek_event(pipeline, "Before loop", seek_sent);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    g_debug("play_file: Entering loop\n");

    while (keep_looping) {
      msg = gst_bus_poll(bus, GST_MESSAGE_ANY, -1);
      if (!msg) break;

      switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ASYNC_DONE:
          seek_sent = send_seek_event(pipeline, "ASYNC_DONE", seek_sent);
          break;

        case GST_MESSAGE_STATE_CHANGED:
          if (GST_OBJECT(pipeline) == GST_MESSAGE_SRC(msg))
          {
            GstState oldstate, newstate, pending;
            gst_message_parse_state_changed(msg, &oldstate, &newstate, &pending);
            g_debug("play_file: %s state-changed: old = %s, new = %s, pending = %s\n",
              gst_element_get_name(GST_ELEMENT(GST_MESSAGE_SRC(msg))),
              gst_element_state_get_name(oldstate),
              gst_element_state_get_name(newstate),
              gst_element_state_get_name(pending));
          }
          break;

        case GST_MESSAGE_ERROR:
          dump_error(msg);

          GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "sequence-player");

          /* fall through */
        case GST_MESSAGE_EOS:
        case GST_MESSAGE_SEGMENT_DONE:
          g_debug("play_file: %s\n", GST_MESSAGE_TYPE_NAME(msg));
          keep_looping = FALSE;
          break;
#ifdef GET_WINDOW
        case GST_MESSAGE_ELEMENT:
          if (gst_structure_has_name(msg->structure, "prepare-xwindow-id")) {
            g_debug("play_file: Setting destination window\n");
            gst_x_overlay_set_xwindow_id(GST_X_OVERLAY(GST_MESSAGE_SRC(msg)), app_state->dst_window);
          }
          break;
#endif /* GET_WINDOW */
        default:
          break;
      }

      gst_message_unref(msg);
    }


    g_debug("play_file: Removing bin from pipeline\n");
    gst_element_set_state(new_bin, GST_STATE_NULL);
    gst_bin_remove(GST_BIN(pipeline), new_bin);
    g_debug("play_file: Removed bin from pipeline\n");

    /* Flush the bus before unrefing */
    gst_bus_set_flushing(bus, TRUE);
    gst_bus_set_flushing(bus, FALSE);
    gst_object_unref(bus);
  }

  g_debug("play_file: === Exiting with (video = \"%s\", audio = \"%s\", duration = %d)\n", video, audio, duration);
}
void
grab_dst_window(AppState *app_state)
{
#ifdef GET_WINDOW
  app_state->display = XOpenDisplay(NULL);
  if (app_state->display) {
    app_state->dst_window = DefaultRootWindow(app_state->display);
    if (app_state->dst_window) {
      app_state->overlay_window = XCompositeGetOverlayWindow(app_state->display, app_state->dst_window);
      if (app_state->overlay_window)
        app_state->dst_window = app_state->overlay_window;
    }
  }
#endif /* GET_WINDOW */
}

void
release_dst_window(AppState *app_state)
{
#ifdef GET_WINDOW
  if (app_state->overlay_window)
    XCompositeReleaseOverlayWindow(app_state->display, app_state->overlay_window);
  if (app_state->display)
    XCloseDisplay(app_state->display);
#endif /* GET_WINDOW */
}
int
main(int argc, char **argv)
{
  int Nix;
  AppState app_state = APP_STATE_STATIC_INIT;
  GstElement *pipeline = NULL;

  gst_init(&argc, &argv);

  g_log_set_default_handler(my_log_func, NULL);

  if (argc > 1)
    if ((pipeline = gst_pipeline_new("sequence-player"))) {
      app_state.audio_sink = gst_element_factory_make("autoaudiosink", NULL);
      gst_bin_add(GST_BIN(pipeline), app_state.audio_sink);
      app_state.video_sink = gst_element_factory_make("autovideosink", NULL);
      gst_bin_add(GST_BIN(pipeline), app_state.video_sink);

      grab_dst_window(&app_state);

      for (Nix = 1 ; Nix < argc ; Nix += 3)
        play_file(pipeline, &app_state, argv[Nix], argv[Nix + 1], atoi(argv[Nix + 2]));
      gst_element_set_state(pipeline, GST_STATE_NULL);
      gst_object_unref(pipeline);

    release_dst_window(&app_state);
    }

  gst_deinit();
  return 0;
}
