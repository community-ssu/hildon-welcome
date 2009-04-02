/**
 * cc -g -Wall -O0 `pkg-config --cflags --libs gstreamer-0.10 gstreamer-plugins-base-0.10 gstreamer-interfaces-0.10 xcomposite` tmp.c -o play
 * ./play /usr/share/sounds/ui-wake_up_tune.wav /usr/share/sounds/call-ringtone01.wav
 * ./play /usr/share/hildon-welcome/media/Hands-v32-h264.avi /usr/share/hildon-welcome/media/Hands-v32-h264.avi
 **/

#include <stdlib.h>
#include <gst/gst.h>
#include <gst/interfaces/xoverlay.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>

//#define HAVE_VIDEO

#ifdef HAVE_VIDEO
#define VIDEO_SINK 0
#endif /* HAVE_VIDEO */
#define AUDIO_SINK 1

static void
no_more_pads(GstElement *src, GstElement *sinks[2])
{
#ifdef HAVE_VIDEO
  gst_element_link(src, sinks[VIDEO_SINK]);
#endif /* HAVE_VIDEO */
  gst_element_link(src, sinks[AUDIO_SINK]);
}

static GstElement *
create_bin(char *fname, GstElement *sinks[2])
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
  g_signal_connect(G_OBJECT(decodebin2), "no-more-pads", (GCallback)no_more_pads, sinks);
  gst_bin_add_many(GST_BIN(bin), filesrc, decodebin2, NULL);
  gst_element_link(filesrc, decodebin2);

  return bin;
}

static void
play_file(GstElement *pipeline, char *audio, Window dst_window, GstElement *sinks[2])
{
  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));

  g_print("play_file: Entering with \"%s\"\n", audio);

  if (bus) {
    gboolean keep_looping = TRUE;
    GstMessage *msg = NULL;
    GstElement *new_bin = NULL;

    new_bin = create_bin(audio, sinks);
    gst_bin_add(GST_BIN(pipeline), new_bin);
    gst_element_set_state(pipeline, GST_STATE_PAUSED);
    gst_element_send_event(pipeline,
      gst_event_new_seek(1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH|GST_SEEK_FLAG_SEGMENT, GST_SEEK_TYPE_SET, G_GUINT64_CONSTANT(0), GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE));
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    g_print("play_file: Entering loop\n");

    while (keep_looping) {
      msg = gst_bus_poll(bus, GST_MESSAGE_ANY, -1);
      if (!msg) break;

      switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_STATE_CHANGED:
          if (GST_OBJECT(pipeline) == GST_MESSAGE_SRC(msg))
          {
            GstState oldstate, newstate, pending;
            gst_message_parse_state_changed(msg, &oldstate, &newstate, &pending);
            g_debug("wait_for_eos: %s state-changed: old = %s, new = %s, pending = %s\n",
              gst_element_get_name(GST_ELEMENT(GST_MESSAGE_SRC(msg))),
              gst_element_state_get_name(oldstate),
              gst_element_state_get_name(newstate),
              gst_element_state_get_name(pending));
          }
          break;

        case GST_MESSAGE_ERROR:
          {
            GError *err = NULL;
            gchar *debug = NULL;

            gst_message_parse_error(msg, &err, &debug);

            if (err) {
              g_print("play_file: error: %s, debug: %s\n", err->message ? err->message : "?", debug ? debug : "?");
              g_error_free(err);
              err = NULL;
            }
            g_free(debug);
            debug = NULL;;
          }
          /* fall through */
        case GST_MESSAGE_EOS:
        case GST_MESSAGE_SEGMENT_DONE:
          g_print("play_file: Exiting loop: %s\n", GST_MESSAGE_TYPE_NAME(msg));
          keep_looping = FALSE;
          break;

        case GST_MESSAGE_ELEMENT:
          if (gst_structure_has_name(msg->structure, "prepare-xwindow-id"))
            gst_x_overlay_set_xwindow_id(GST_X_OVERLAY(GST_MESSAGE_SRC(msg)), dst_window);
          break;

        default:
          break;
      }

      gst_message_unref(msg);
    }

    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "playdetails");

    gst_element_set_state(new_bin, GST_STATE_NULL);
    g_print("play_file: Removing %s from pipeline\n", gst_element_get_name(new_bin));
    gst_bin_remove(GST_BIN(pipeline), new_bin);

    /* Flush the bus before unrefing */
    gst_bus_set_flushing(bus, TRUE);
    gst_bus_set_flushing(bus, FALSE);
    gst_object_unref(bus);

    g_print("play_file: === Exiting with \"%s\"\n", audio);
  }
}

int
main(int argc, char **argv)
{
  int Nix;
  GstElement *pipeline = NULL, *sinks[2];
  gst_init(&argc, &argv);
  Display *display;
  Window dst_window = 0, xcomposite_window = 0;

  if (!(display = XOpenDisplay(NULL)))
    g_error("main: Failed to open display\n");
  if ((dst_window = DefaultRootWindow(display)) == 0)
    g_error("main: Failed to obtain root window\n");
  if ((xcomposite_window = XCompositeGetOverlayWindow(display, dst_window)) != 0)
    dst_window = xcomposite_window;

  if (argc > 1)
    if ((pipeline = gst_pipeline_new("sequence-player"))) {

      sinks[AUDIO_SINK] = gst_element_factory_make("autoaudiosink", NULL);
      gst_bin_add(GST_BIN(pipeline), sinks[AUDIO_SINK]);
#ifdef HAVE_VIDEO
      sinks[VIDEO_SINK] = gst_element_factory_make("autovideosink", NULL);
      gst_bin_add(GST_BIN(pipeline), sinks[VIDEO_SINK]);
#endif /* HAVE_VIDEO */
      for (Nix = 1 ; Nix < argc ; Nix++)
        play_file(pipeline, argv[Nix], dst_window, sinks);
      gst_element_set_state(pipeline, GST_STATE_NULL);
      gst_object_unref(pipeline);
    }

  if (xcomposite_window)
    XCompositeReleaseOverlayWindow(display, xcomposite_window);
  XCloseDisplay(display);
  gst_deinit();
  return 0;
}
