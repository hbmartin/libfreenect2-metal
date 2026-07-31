#include "kinect_bridge.h"

#include "bridge_helpers.h"

#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/packet_pipeline.h>
#include <libfreenect2/registration.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace
{

const size_t COLOR_WIDTH = 1920;
const size_t COLOR_HEIGHT = 1080;
const size_t DEPTH_WIDTH = 512;
const size_t DEPTH_HEIGHT = 424;
const float MAX_SYNCHRONIZATION_DELTA_MS = 50.0f;

class ScopedFrameRelease
{
public:
  ScopedFrameRelease(libfreenect2::SyncMultiFrameListener& listener, libfreenect2::FrameMap& frames)
      : listener_(listener), frames_(frames)
  {
  }

  ~ScopedFrameRelease() { listener_.release(frames_); }

private:
  ScopedFrameRelease(const ScopedFrameRelease&) = delete;
  ScopedFrameRelease& operator=(const ScopedFrameRelease&) = delete;

  libfreenect2::SyncMultiFrameListener& listener_;
  libfreenect2::FrameMap& frames_;
};

void writeError(char* destination, size_t capacity, const std::string& message)
{
  if (!destination || capacity == 0)
    return;
  const size_t count = std::min(capacity - 1, message.size());
  std::memcpy(destination, message.data(), count);
  destination[count] = '\0';
}

libfreenect2::PacketPipeline* createPipeline(const std::string& requested, int& pipeline_kind,
                                             std::string& error)
{
  if (requested.empty() || requested == "auto")
  {
#ifdef LIBFREENECT2_WITH_METAL_SUPPORT
    pipeline_kind = MP_POSE_PIPELINE_METAL;
    return new libfreenect2::MetalPacketPipeline();
#else
    pipeline_kind = MP_POSE_PIPELINE_CPU;
    return new libfreenect2::CpuPacketPipeline();
#endif
  }
  if (requested == "cpu")
  {
    pipeline_kind = MP_POSE_PIPELINE_CPU;
    return new libfreenect2::CpuPacketPipeline();
  }
  if (requested == "metal")
  {
#ifdef LIBFREENECT2_WITH_METAL_SUPPORT
    pipeline_kind = MP_POSE_PIPELINE_METAL;
    return new libfreenect2::MetalPacketPipeline();
#else
    error = "Metal was requested but this libfreenect2 build has no Metal support";
    return NULL;
#endif
  }
  error = "Unknown pipeline '" + requested + "' (expected auto, metal, or cpu)";
  return NULL;
}

} // namespace

struct MpPoseCapture
{
  libfreenect2::Freenect2 context;
  libfreenect2::Freenect2Device* device;
  libfreenect2::SyncMultiFrameListener listener;
  std::unique_ptr<libfreenect2::Registration> registration;
  libfreenect2::Frame undistorted;
  libfreenect2::Frame registered_color;
  std::vector<int> depth_to_color;
  std::vector<int32_t> color_to_depth;
  std::string last_error;
  int pipeline_kind;
  bool started;
  bool has_frame;
  bool synchronization_valid;

  MpPoseCapture()
      : device(NULL), listener(libfreenect2::Frame::Color | libfreenect2::Frame::Depth),
        undistorted(DEPTH_WIDTH, DEPTH_HEIGHT, 4), registered_color(DEPTH_WIDTH, DEPTH_HEIGHT, 4),
        depth_to_color(DEPTH_WIDTH * DEPTH_HEIGHT, -1), pipeline_kind(MP_POSE_PIPELINE_CPU),
        started(false), has_frame(false), synchronization_valid(false)
  {
    undistorted.format = libfreenect2::Frame::Float;
    registered_color.format = libfreenect2::Frame::BGRX;
  }

  ~MpPoseCapture()
  {
    if (device)
    {
      if (started)
        device->stop();
      device->close();
    }
  }
};

extern "C" int mp_pose_capture_open(const char* serial, const char* pipeline,
                                    MpPoseCapture** capture, MpPoseStreamInfo* stream_info,
                                    char* error, size_t error_capacity)
{
  if (!capture || !stream_info)
  {
    writeError(error, error_capacity, "capture and stream_info are required");
    return MP_POSE_STATUS_INVALID_ARGUMENT;
  }
  *capture = NULL;

  try
  {
    std::unique_ptr<MpPoseCapture> state(new (std::nothrow) MpPoseCapture());
    if (!state)
    {
      writeError(error, error_capacity, "Unable to allocate capture state");
      return MP_POSE_STATUS_CAPTURE_ERROR;
    }

    const std::string requested_pipeline = pipeline ? pipeline : "auto";
    std::string pipeline_error;
    std::unique_ptr<libfreenect2::PacketPipeline> packet_pipeline(
        createPipeline(requested_pipeline, state->pipeline_kind, pipeline_error));
    if (!packet_pipeline)
    {
      writeError(error, error_capacity, pipeline_error);
      return MP_POSE_STATUS_UNAVAILABLE;
    }

    if (state->context.enumerateDevices() == 0)
    {
      writeError(error, error_capacity, "No Kinect v2 detected");
      return MP_POSE_STATUS_NO_DEVICE;
    }

    const std::string requested_serial =
        serial && serial[0] != '\0' ? serial : state->context.getDefaultDeviceSerialNumber();
    state->device = state->context.openDevice(requested_serial, packet_pipeline.release());
    if (!state->device)
    {
      writeError(error, error_capacity, "Unable to open Kinect " + requested_serial);
      return MP_POSE_STATUS_CAPTURE_ERROR;
    }

    state->device->setColorFrameListener(&state->listener);
    state->device->setIrAndDepthFrameListener(&state->listener);
    if (!state->device->start())
    {
      writeError(error, error_capacity, "Unable to start Kinect color and depth streams");
      return MP_POSE_STATUS_CAPTURE_ERROR;
    }
    state->started = true;
    state->registration.reset(new libfreenect2::Registration(
        state->device->getIrCameraParams(), state->device->getColorCameraParams()));

    stream_info->color_width = static_cast<uint32_t>(COLOR_WIDTH);
    stream_info->color_height = static_cast<uint32_t>(COLOR_HEIGHT);
    stream_info->depth_width = static_cast<uint32_t>(DEPTH_WIDTH);
    stream_info->depth_height = static_cast<uint32_t>(DEPTH_HEIGHT);
    stream_info->rgb_buffer_size = COLOR_WIDTH * COLOR_HEIGHT * 3;
    stream_info->pipeline = state->pipeline_kind;
    *capture = state.release();
    writeError(error, error_capacity, std::string());
    return MP_POSE_STATUS_OK;
  }
  catch (const std::exception& exception)
  {
    writeError(error, error_capacity,
               std::string("Unable to open Kinect capture: ") + exception.what());
    return MP_POSE_STATUS_CAPTURE_ERROR;
  }
  catch (...)
  {
    writeError(error, error_capacity, "Unable to open Kinect capture: unknown native error");
    return MP_POSE_STATUS_CAPTURE_ERROR;
  }
}

extern "C" int mp_pose_capture_next(MpPoseCapture* capture, unsigned char* rgb, size_t rgb_capacity,
                                    int timeout_ms, MpPoseFrameInfo* frame_info)
{
  if (!capture || !rgb || !frame_info || timeout_ms < 0)
    return MP_POSE_STATUS_INVALID_ARGUMENT;
  if (rgb_capacity < COLOR_WIDTH * COLOR_HEIGHT * 3)
  {
    capture->last_error = "RGB destination buffer is too small";
    return MP_POSE_STATUS_INVALID_ARGUMENT;
  }

  capture->has_frame = false;
  capture->synchronization_valid = false;
  try
  {
    libfreenect2::FrameMap frames;
    if (!capture->listener.waitForNewFrame(frames, timeout_ms))
    {
      capture->last_error = "Timed out waiting for a synchronized Kinect frame";
      return MP_POSE_STATUS_TIMEOUT;
    }
    ScopedFrameRelease release_frames(capture->listener, frames);

    libfreenect2::Frame* color = frames[libfreenect2::Frame::Color];
    libfreenect2::Frame* depth = frames[libfreenect2::Frame::Depth];
    if (!color || !depth || color->width != COLOR_WIDTH || color->height != COLOR_HEIGHT ||
        depth->width != DEPTH_WIDTH || depth->height != DEPTH_HEIGHT)
    {
      capture->last_error = "Kinect returned a missing or unexpected frame shape";
      return MP_POSE_STATUS_CAPTURE_ERROR;
    }

    mediapipe_pose::ColorFormat source_format;
    if (color->format == libfreenect2::Frame::RGBX)
      source_format = mediapipe_pose::ColorFormatRgbx;
    else if (color->format == libfreenect2::Frame::BGRX)
      source_format = mediapipe_pose::ColorFormatBgrx;
    else
    {
      capture->last_error = "Kinect color frame is neither RGBX nor BGRX";
      return MP_POSE_STATUS_CAPTURE_ERROR;
    }

    if (!mediapipe_pose::convertColorToRgb(color->data, color->width, color->height,
                                           color->bytes_per_pixel, source_format, rgb,
                                           rgb_capacity))
    {
      capture->last_error = "Unable to convert the Kinect color frame to RGB";
      return MP_POSE_STATUS_CAPTURE_ERROR;
    }

    capture->registration->apply(color, depth, &capture->undistorted, &capture->registered_color,
                                 true, NULL, capture->depth_to_color.data());
    mediapipe_pose::buildReverseColorMap(reinterpret_cast<const float*>(capture->undistorted.data),
                                         DEPTH_WIDTH * DEPTH_HEIGHT, capture->depth_to_color.data(),
                                         COLOR_WIDTH * COLOR_HEIGHT, capture->color_to_depth);

    frame_info->color_timestamp = color->timestamp;
    frame_info->depth_timestamp = depth->timestamp;
    frame_info->color_sequence = color->sequence;
    frame_info->depth_sequence = depth->sequence;
    frame_info->synchronization_delta_ms =
        mediapipe_pose::timestampDeltaMilliseconds(color->timestamp, depth->timestamp);
    capture->synchronization_valid =
        frame_info->synchronization_delta_ms <= MAX_SYNCHRONIZATION_DELTA_MS;
    frame_info->synchronization_valid = capture->synchronization_valid ? 1 : 0;
    capture->has_frame = true;
    capture->last_error.clear();
    return MP_POSE_STATUS_OK;
  }
  catch (const std::exception& exception)
  {
    capture->last_error = std::string("Unable to capture Kinect frame: ") + exception.what();
    return MP_POSE_STATUS_CAPTURE_ERROR;
  }
  catch (...)
  {
    capture->last_error = "Unable to capture Kinect frame: unknown native error";
    return MP_POSE_STATUS_CAPTURE_ERROR;
  }
}

extern "C" int mp_pose_capture_lift(MpPoseCapture* capture, const float* normalized_xy,
                                    size_t landmark_count, int primary_radius, int fallback_radius,
                                    float cluster_span_mm, float* xyz_meters, uint8_t* valid)
{
  if (!capture || !normalized_xy || !xyz_meters || !valid || primary_radius < 0 ||
      fallback_radius < primary_radius || cluster_span_mm < 0.0f)
    return MP_POSE_STATUS_INVALID_ARGUMENT;
  if (landmark_count > std::numeric_limits<size_t>::max() / 3)
  {
    capture->last_error = "Landmark count is too large";
    return MP_POSE_STATUS_INVALID_ARGUMENT;
  }
  if (!capture->has_frame)
  {
    capture->last_error = "No captured frame is available for landmark lifting";
    return MP_POSE_STATUS_CAPTURE_ERROR;
  }

  try
  {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::fill(valid, valid + landmark_count, static_cast<uint8_t>(0));
    std::fill(xyz_meters, xyz_meters + landmark_count * 3, nan);
    if (!capture->synchronization_valid)
    {
      capture->last_error.clear();
      return MP_POSE_STATUS_OK;
    }

    const float* depth = reinterpret_cast<const float*>(capture->undistorted.data);
    for (size_t i = 0; i < landmark_count; ++i)
    {
      const int depth_index = mediapipe_pose::findDepthPixel(
          normalized_xy[i * 2], normalized_xy[i * 2 + 1], capture->color_to_depth, COLOR_WIDTH,
          COLOR_HEIGHT, depth, DEPTH_WIDTH * DEPTH_HEIGHT, primary_radius, fallback_radius,
          cluster_span_mm);
      if (depth_index < 0)
        continue;

      float x = nan, y = nan, z = nan;
      capture->registration->getPointXYZ(&capture->undistorted,
                                         depth_index / static_cast<int>(DEPTH_WIDTH),
                                         depth_index % static_cast<int>(DEPTH_WIDTH), x, y, z);
      if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        continue;
      xyz_meters[i * 3] = x;
      xyz_meters[i * 3 + 1] = y;
      xyz_meters[i * 3 + 2] = z;
      valid[i] = 1;
    }
    capture->last_error.clear();
    return MP_POSE_STATUS_OK;
  }
  catch (const std::exception& exception)
  {
    capture->last_error = std::string("Unable to lift pose landmarks: ") + exception.what();
    return MP_POSE_STATUS_CAPTURE_ERROR;
  }
  catch (...)
  {
    capture->last_error = "Unable to lift pose landmarks: unknown native error";
    return MP_POSE_STATUS_CAPTURE_ERROR;
  }
}

extern "C" const char* mp_pose_capture_last_error(const MpPoseCapture* capture)
{
  return capture ? capture->last_error.c_str() : "Capture handle is null";
}

extern "C" void mp_pose_capture_close(MpPoseCapture* capture)
{
  delete capture;
}
