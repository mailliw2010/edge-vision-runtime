#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <iostream>
#include <string>

namespace {

class GstObjectPtr {
 public:
  explicit GstObjectPtr(GstObject* object = nullptr) : object_(object) {}
  ~GstObjectPtr() {
    if (object_ != nullptr) {
      gst_object_unref(object_);
    }
  }
  GstObjectPtr(const GstObjectPtr&) = delete;
  GstObjectPtr& operator=(const GstObjectPtr&) = delete;
  GstObject* get() const { return object_; }

 private:
  GstObject* object_{nullptr};
};

class GstSamplePtr {
 public:
  explicit GstSamplePtr(GstSample* sample = nullptr) : sample_(sample) {}
  ~GstSamplePtr() {
    if (sample_ != nullptr) {
      gst_sample_unref(sample_);
    }
  }
  GstSamplePtr(const GstSamplePtr&) = delete;
  GstSamplePtr& operator=(const GstSamplePtr&) = delete;
  GstSample* get() const { return sample_; }

 private:
  GstSample* sample_{nullptr};
};

class GstMessagePtr {
 public:
  explicit GstMessagePtr(GstMessage* message = nullptr) : message_(message) {}
  ~GstMessagePtr() {
    if (message_ != nullptr) {
      gst_message_unref(message_);
    }
  }
  GstMessagePtr(const GstMessagePtr&) = delete;
  GstMessagePtr& operator=(const GstMessagePtr&) = delete;
  GstMessage* get() const { return message_; }

 private:
  GstMessage* message_{nullptr};
};

std::string RedactUri(const std::string& uri) {
  const auto scheme = uri.find("://");
  if (scheme == std::string::npos) {
    return uri;
  }
  const auto at = uri.find('@', scheme + 3);
  if (at == std::string::npos) {
    return uri;
  }
  return uri.substr(0, scheme + 3) + "***:***@" + uri.substr(at + 1);
}

std::string ReadBusMessage(GstElement* pipeline, GstClockTime timeout_ns) {
  GstBus* bus = gst_element_get_bus(pipeline);
  if (bus == nullptr) {
    return "failed to get GStreamer bus";
  }
  GstObjectPtr bus_owner(GST_OBJECT(bus));

  GstMessagePtr message(gst_bus_timed_pop_filtered(
      bus, timeout_ns, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING |
                                                   GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED)));
  if (message.get() == nullptr) {
    return {};
  }

  if (GST_MESSAGE_TYPE(message.get()) == GST_MESSAGE_ERROR) {
    GError* gst_error = nullptr;
    gchar* debug = nullptr;
    gst_message_parse_error(message.get(), &gst_error, &debug);
    std::string result = "ERROR";
    if (GST_MESSAGE_SRC(message.get()) != nullptr) {
      result += " from ";
      result += GST_OBJECT_NAME(GST_MESSAGE_SRC(message.get()));
    }
    if (gst_error != nullptr && gst_error->message != nullptr) {
      result += ": ";
      result += gst_error->message;
    }
    if (debug != nullptr && debug[0] != '\0') {
      result += " [debug: ";
      result += debug;
      result += "]";
    }
    if (gst_error != nullptr) {
      g_error_free(gst_error);
    }
    if (debug != nullptr) {
      g_free(debug);
    }
    return result;
  }

  if (GST_MESSAGE_TYPE(message.get()) == GST_MESSAGE_WARNING) {
    GError* gst_error = nullptr;
    gchar* debug = nullptr;
    gst_message_parse_warning(message.get(), &gst_error, &debug);
    std::string result = "WARNING";
    if (GST_MESSAGE_SRC(message.get()) != nullptr) {
      result += " from ";
      result += GST_OBJECT_NAME(GST_MESSAGE_SRC(message.get()));
    }
    if (gst_error != nullptr && gst_error->message != nullptr) {
      result += ": ";
      result += gst_error->message;
    }
    if (debug != nullptr && debug[0] != '\0') {
      result += " [debug: ";
      result += debug;
      result += "]";
    }
    if (gst_error != nullptr) {
      g_error_free(gst_error);
    }
    if (debug != nullptr) {
      g_free(debug);
    }
    return result;
  }

  if (GST_MESSAGE_TYPE(message.get()) == GST_MESSAGE_EOS) {
    return "EOS before first decoded frame";
  }

  if (GST_MESSAGE_TYPE(message.get()) == GST_MESSAGE_STATE_CHANGED) {
    GstState old_state = GST_STATE_NULL;
    GstState new_state = GST_STATE_NULL;
    GstState pending_state = GST_STATE_VOID_PENDING;
    gst_message_parse_state_changed(message.get(), &old_state, &new_state, &pending_state);
    return "STATE_CHANGED " + std::string(gst_element_state_get_name(old_state)) + " -> " +
           gst_element_state_get_name(new_state);
  }

  return {};
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: runtime_gstreamer_hw_decode_probe <rtsp-uri>\n";
    return 2;
  }

  const std::string uri = argv[1];
  gst_init(nullptr, nullptr);

  if (gst_element_factory_find("nvv4l2decoder") == nullptr) {
    std::cerr << "nvv4l2decoder is not available on this host\n";
    return 1;
  }
  if (gst_element_factory_find("nvvidconv") == nullptr) {
    std::cerr << "nvvidconv is not available on this host\n";
    return 1;
  }

  const std::string pipeline_description =
      "rtspsrc location=\"" + uri +
      "\" protocols=tcp latency=200 ! rtph264depay ! h264parse ! "
      "nvv4l2decoder ! nvvidconv ! video/x-raw,format=RGBA ! "
      "appsink name=probe_sink sync=false max-buffers=1 drop=true";

  GError* parse_error = nullptr;
  GstElement* pipeline = gst_parse_launch(pipeline_description.c_str(), &parse_error);
  if (pipeline == nullptr) {
    std::cerr << "failed to create GStreamer pipeline for " << RedactUri(uri);
    if (parse_error != nullptr && parse_error->message != nullptr) {
      std::cerr << ": " << parse_error->message;
    }
    std::cerr << '\n';
    if (parse_error != nullptr) {
      g_error_free(parse_error);
    }
    return 1;
  }
  if (parse_error != nullptr) {
    g_error_free(parse_error);
  }
  GstObjectPtr pipeline_owner(GST_OBJECT(pipeline));

  GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "probe_sink");
  if (sink == nullptr) {
    std::cerr << "failed to find appsink in probe pipeline\n";
    return 1;
  }
  GstObjectPtr sink_owner(GST_OBJECT(sink));

  const GstStateChangeReturn state_result = gst_element_set_state(pipeline, GST_STATE_PLAYING);
  if (state_result == GST_STATE_CHANGE_FAILURE) {
    std::cerr << "failed to start GStreamer hardware decode pipeline for " << RedactUri(uri)
              << '\n';
    const std::string bus_message = ReadBusMessage(pipeline, 2 * GST_SECOND);
    if (!bus_message.empty()) {
      std::cerr << bus_message << '\n';
    }
    gst_element_set_state(pipeline, GST_STATE_NULL);
    return 1;
  }

  GstState current_state = GST_STATE_NULL;
  GstState pending_state = GST_STATE_VOID_PENDING;
  const GstStateChangeReturn wait_result =
      gst_element_get_state(pipeline, &current_state, &pending_state, 5 * GST_SECOND);
  if (wait_result == GST_STATE_CHANGE_FAILURE || current_state != GST_STATE_PLAYING) {
    std::cerr << "pipeline did not reach PLAYING for " << RedactUri(uri) << '\n';
    const std::string bus_message = ReadBusMessage(pipeline, 2 * GST_SECOND);
    if (!bus_message.empty()) {
      std::cerr << bus_message << '\n';
    }
    gst_element_set_state(pipeline, GST_STATE_NULL);
    return 1;
  }

  GstSamplePtr sample(gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 10 * GST_SECOND));
  if (sample.get() == nullptr) {
    std::cerr << "hardware decode probe timed out before first frame for " << RedactUri(uri)
              << '\n';
    const std::string bus_message = ReadBusMessage(pipeline, 2 * GST_SECOND);
    if (!bus_message.empty()) {
      std::cerr << bus_message << '\n';
    }
    gst_element_set_state(pipeline, GST_STATE_NULL);
    return 1;
  }

  GstBuffer* buffer = gst_sample_get_buffer(sample.get());
  if (buffer == nullptr) {
    std::cerr << "probe pulled a sample without GstBuffer\n";
    gst_element_set_state(pipeline, GST_STATE_NULL);
    return 1;
  }

  std::cout << "hardware decode probe succeeded for " << RedactUri(uri)
            << "; first frame bytes=" << gst_buffer_get_size(buffer) << '\n';
  gst_element_set_state(pipeline, GST_STATE_NULL);
  return 0;
}
