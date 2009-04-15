#include <stdlib.h>
#include <gst/gst.h>

#define HAVE_VIDEO

#define APP_STATE_STATIC_INIT { \
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
  GstElement *audio_sink;
  GstElement *video_sink;
  Window dst_window;
  Window overlay_window;
  Display *display;
} AppState;

static void
link_to_sink(GstElement *decodebin2, AppState *app_state)
{
  gst_element_link(decodebin2, app_state->audio_sink);
#ifdef HAVE_VIDEO
  gst_element_link(decodebin2, app_state->video_sink);
#endif /* HAVE_VIDEO */
}

static GstElement *
create_bin(char *fname, AppState *app_state)
{
  static int counter = 0;
  char *name = NULL;
  GstElement *bin, *filesrc, *decodebin2;

  name = g_strdup_printf("bin%d", counter++);
  bin = gst_bin_new(name);
  g_free(name);

  filesrc = gst_element_factory_make("filesrc", NULL);
  g_object_set(G_OBJECT(filesrc), "location", fname, NULL);
  decodebin2 = gst_element_factory_make("decodebin2", NULL);
  g_signal_connect(G_OBJECT(decodebin2), "no-more-pads", (GCallback)link_to_sink, app_state);
  gst_bin_add_many(GST_BIN(bin), filesrc, decodebin2, NULL);
  gst_element_link(filesrc, decodebin2);

  return bin;
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
play_file(GstElement *pipeline, char *file, AppState *app_state)
{
  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));

  g_debug("play_file: === Entering with \"%s\"\n", file);

  if (bus) {
    gboolean seek_sent = FALSE;
    gboolean keep_looping = TRUE;
    GstMessage *msg = NULL;
    GstElement *new_bin = NULL;

    new_bin = create_bin(file, app_state);
    gst_bin_add(GST_BIN(pipeline), new_bin);
    gst_element_set_state(pipeline, GST_STATE_PAUSED);
    seek_sent = send_seek_event(pipeline, "Before loop", seek_sent);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    g_debug("play_file: Entering loop\n");

    while (keep_looping) {
      msg = gst_bus_poll(bus, GST_MESSAGE_ANY, -1);
      if (!msg) break;

      switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR:
          {
            GError *err = NULL;
            gchar *debug = NULL;

            gst_message_parse_error(msg, &err, &debug);

            if (err) {
              g_debug("play_file: error: %s, debug: %s\n", err->message ? err->message : "?", debug ? debug : "?");
              g_error_free(err);
              err = NULL;
            }
            g_free(debug);
            debug = NULL;;
          }
          /* fall through */
        case GST_MESSAGE_EOS:
        case GST_MESSAGE_SEGMENT_DONE:
          g_debug("play_file: %s\n", GST_MESSAGE_TYPE_NAME(msg));
          keep_looping = FALSE;
          break;
#ifdef HAVE_VIDEO
        case GST_MESSAGE_ELEMENT:
          if (gst_structure_has_name(msg->structure, "prepare-xwindow-id")) {
            g_debug("play_file: Setting destination window\n");
            gst_x_overlay_set_xwindow_id(GST_X_OVERLAY(GST_MESSAGE_SRC(msg)), app_state->dst_window);
          }
          break;
#endif /* HAVE_VIDEO */
        default:
          break;
      }

      gst_message_unref(msg);
    }

    gst_element_set_state(new_bin, GST_STATE_NULL);
    gst_bin_remove(GST_BIN(pipeline), new_bin);

    /* Flush the bus before unrefing */
    gst_bus_set_flushing(bus, TRUE);
    gst_bus_set_flushing(bus, FALSE);
    gst_object_unref(bus);
  }

  g_debug("play_file: === Exiting with \"%s\"\n", file);
}
#ifdef HAVE_VIDEO
void
grab_dst_window(AppState *app_state)
{
  app_state->display = XOpenDisplay(NULL);
  if (app_state->display) {
    app_state->dst_window = DefaultRootWindow(app_state->display);
    if (app_state->dst_window) {
      app_state->overlay_window = XCompositeGetOverlayWindow(app_state->display, app_state->dst_window);
      if (app_state->overlay_window)
        app_state->dst_window = app_state->overlay_window;
    }
  }
}

void
release_dst_window(AppState *app_state)
{
  if (app_state->overlay_window)
    XCompositeReleaseOverlayWindow(app_state->display, app_state->overlay_window);
  if (app_state->display)
    XCloseDisplay(app_state->display);
}
#endif /* HAVE_VIDEO */
int
main(int argc, char **argv)
{
  int Nix;
  AppState app_state = APP_STATE_STATIC_INIT;
  GstElement *pipeline = NULL;

  gst_init(&argc, &argv);

#ifdef HAVE_VIDEO
  g_debug("HAVE VIDEO!\n");
#else /* !HAVE_VIDEO */
  g_debug("NO VIDEO!\n");
#endif /* HAVE_VIDEO */

  if (argc > 1)
    if ((pipeline = gst_pipeline_new("sequence-player"))) {
      app_state.audio_sink = gst_element_factory_make("autoaudiosink", NULL);
      gst_bin_add(GST_BIN(pipeline), app_state.audio_sink);
#ifdef HAVE_VIDEO
      app_state.video_sink = gst_element_factory_make("autovideosink", NULL);
      gst_bin_add(GST_BIN(pipeline), app_state.video_sink);

      grab_dst_window(&app_state);
#endif /* HAVE_VIDEO */
      for (Nix = 1 ; Nix < argc ; Nix++)
        play_file(pipeline, argv[Nix], &app_state);
      gst_element_set_state(pipeline, GST_STATE_NULL);
      gst_object_unref(pipeline);

#ifdef HAVE_VIDEO
    release_dst_window(&app_state);
#endif /* HAVE_VIDEO */
    }

  gst_deinit();
  return 0;
}
