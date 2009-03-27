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
play_pipeline(GstElement *pipeline)
{
  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));

  gst_element_set_state(pipeline, GST_STATE_PLAYING);

  if (bus) {
    GstMessage *msg = NULL;
    gboolean keep_looping = TRUE;

    while (keep_looping) {
      msg = gst_bus_poll(bus, GST_MESSAGE_ANY, -1);
      g_print("play_pipeline: Got message %s\n", GST_MESSAGE_TYPE_NAME(msg));
      if (!msg) break;

      switch(GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_STATE_CHANGED:
          if (pipeline == GST_ELEMENT(GST_MESSAGE_SRC(msg)))
          {
            GstState oldstate, newstate, pending;

            gst_message_parse_state_changed(msg, &oldstate, &newstate, &pending);
            g_print("play_pipeline: state-changed: old = %s, new = %s, pending = %s\n", 
              GST_STATE_NAME(oldstate),
              GST_STATE_NAME(newstate),
              GST_STATE_NAME(pending));
          }
          break;

        case GST_MESSAGE_ERROR:
          {
            GError *err = NULL;
            char *debug = NULL;
            gst_message_parse_error(msg, &err, &debug);
            g_print("play_pipeline: %s: %s %s\n", GST_MESSAGE_TYPE_NAME(msg), err ? err->message : "", debug ? debug : "");
            if (err)
              g_error_free(err);
            g_free(debug);
          }
          /* fall through */
        case GST_MESSAGE_EOS:
        case GST_MESSAGE_SEGMENT_DONE:
          g_print("play_pipeline: SEGMENT_DONE received\n");
          gst_element_set_state(pipeline, GST_STATE_PAUSED);
          keep_looping = FALSE;
          break;

        default:
          break;
      }

      gst_message_unref(msg);
    }
  }
}

static void
no_more_pads(GstElement *decodebin2, GstElement *output)
{
  g_print("no_more_pads: Linking input to output\n");
  gst_element_link(decodebin2, output);
}

static GstElement *
create_bin(const char *fname, GstElement *output)
{
  GstElement *filesrc, *decodebin2, *bin;
  static int counter = 0;
  char *name = g_strdup_printf("audio_bin%d", counter++);

  bin = gst_bin_new(name);
  g_free(name);
  filesrc = gst_element_factory_make("filesrc", "audio_filesrc");
  g_object_set(G_OBJECT(filesrc), "location", fname, NULL);
  decodebin2 = gst_element_factory_make("decodebin2", "audio_decodebin");
  gst_bin_add_many(GST_BIN(bin), filesrc, decodebin2, NULL);
  gst_element_link(filesrc, decodebin2);

  g_signal_connect(G_OBJECT(decodebin2), "no-more-pads", (GCallback)no_more_pads, output);

  return bin;
}

int
main(int argc, char **argv)
{
  GstEvent *seek;
  int Nix;
  GstElement *pipeline, *autoaudiosink, *old_bin = NULL, *new_bin = NULL;

  gst_init(&argc, &argv);

  pipeline = gst_pipeline_new("myname");
  autoaudiosink = gst_element_factory_make("autoaudiosink", "audio_autoaudiosink");
  seek = gst_event_new_seek(1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SEGMENT, GST_SEEK_TYPE_SET, G_GUINT64_CONSTANT(0), GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);

  gst_bin_add(GST_BIN(pipeline), autoaudiosink);

  for (Nix = 1 ; Nix < argc ; Nix++) {
    g_print("main: playing argv[%d] = %s\n", Nix, argv[Nix]);
    new_bin = create_bin(argv[Nix], autoaudiosink);
    if (old_bin) {
      gst_element_set_locked_state(old_bin, TRUE);
      gst_element_set_state(old_bin, GST_STATE_NULL);
      gst_bin_remove(GST_BIN(pipeline), old_bin);
    }
    gst_bin_add(GST_BIN(pipeline), new_bin);
    gst_element_set_state(pipeline, GST_STATE_PAUSED);
    gst_element_send_event(pipeline, seek);
    play_pipeline(pipeline);
    old_bin = new_bin;
    g_print("main: played argv[%d] = %s\n", Nix, argv[Nix]);
  }

  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);

  gst_deinit();

  return 0;
}
