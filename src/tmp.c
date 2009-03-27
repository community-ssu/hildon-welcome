#include <stdlib.h>
#include <gst/gst.h>

#define GST_STATE_NAME(state)             \
  ((GST_STATE_VOID_PENDING == (state))    \
    ? "GST_STATE_VOID_PENDING"            \
    : (GST_STATE_NULL == (state))         \
      ? "GST_STATE_NULL"                  \
      : (GST_STATE_READY == (state))      \
        ? "GST_STATE_READY"               \
        : (GST_STATE_PAUSED == (state))   \
          ? "GST_STATE_PAUSED"            \
          : "GST_STATE_PLAYING")


static void
no_more_pads(GstElement *src, GstElement *dst)
{
  g_print("no_more_pads: Linking src to dst\n");
  gst_element_link(src, dst);
}

static GstElement *
create_bin(char *fname, GstElement *sink)
{
  static int counter = 0;
  char *name = NULL;
  GstElement *bin, *filesrc, *decodebin2;

  name = g_strdup_printf("audio-bin%d", counter++);
  bin = gst_bin_new(name);
  g_free(name);

  filesrc = gst_element_factory_make("filesrc", "the-filesrc");
  g_object_set(G_OBJECT(filesrc), "location", fname, NULL);
  decodebin2 = gst_element_factory_make("decodebin2", "the-decodebin2");
  g_signal_connect(G_OBJECT(decodebin2), "no-more-pads", (GCallback)no_more_pads, sink);
  gst_bin_add_many(GST_BIN(bin), filesrc, decodebin2, NULL);
  gst_element_link(filesrc, decodebin2);

  return bin;
}

static void
play_file(GstElement *pipeline, char *filename, GstElement *audiosink)
{
  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));

  g_print("play_file: Entering with \"%s\"\n", filename);

  if (bus) {
    gboolean keep_looping = TRUE;
    GstMessage *msg = NULL;
    GstElement *new_bin = NULL;

    new_bin = create_bin(filename, audiosink);
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
        case GST_MESSAGE_SEGMENT_DONE:
          g_print("play_file: SEGMENT_DONE\n");
          gst_element_set_locked_state(new_bin, TRUE);
          gst_element_set_state(new_bin, GST_STATE_NULL);
          gst_bin_remove(GST_BIN(pipeline), new_bin);
          keep_looping = FALSE;
          break;

        default:
          break;
      }

      gst_message_unref(msg);
    }
  }

  g_print("play_file: === Exiting with \"%s\"\n", filename);
}

int
main(int argc, char **argv)
{
  int Nix;
  GstElement *pipeline = NULL, *audiosink;
  gst_init(&argc, &argv);

  if (argc > 1)
    if ((pipeline = gst_pipeline_new("sequence-player"))) {
      audiosink = gst_element_factory_make("autoaudiosink", "the-audio-sink");
      gst_bin_add(GST_BIN(pipeline), audiosink);
      for (Nix = 1 ; Nix < argc ; Nix++)
        play_file(pipeline, argv[Nix], audiosink);
      gst_element_set_state(pipeline, GST_STATE_NULL);
      gst_object_unref(pipeline);
    }

  gst_deinit();
  return 0;
}
