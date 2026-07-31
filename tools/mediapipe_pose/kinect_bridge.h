#ifndef LIBFREENECT2_MEDIAPIPE_POSE_KINECT_BRIDGE_H_
#define LIBFREENECT2_MEDIAPIPE_POSE_KINECT_BRIDGE_H_

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define MP_POSE_BRIDGE_API __declspec(dllexport)
#else
#define MP_POSE_BRIDGE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct MpPoseCapture MpPoseCapture;

  typedef enum MpPoseStatus
  {
    MP_POSE_STATUS_OK = 0,
    MP_POSE_STATUS_TIMEOUT = 1,
    MP_POSE_STATUS_INVALID_ARGUMENT = 2,
    MP_POSE_STATUS_NO_DEVICE = 3,
    MP_POSE_STATUS_UNAVAILABLE = 4,
    MP_POSE_STATUS_CAPTURE_ERROR = 5
  } MpPoseStatus;

  typedef enum MpPosePipeline
  {
    MP_POSE_PIPELINE_CPU = 0,
    MP_POSE_PIPELINE_METAL = 1
  } MpPosePipeline;

  typedef struct MpPoseStreamInfo
  {
    uint32_t color_width;
    uint32_t color_height;
    uint32_t depth_width;
    uint32_t depth_height;
    size_t rgb_buffer_size;
    int pipeline;
  } MpPoseStreamInfo;

  typedef struct MpPoseFrameInfo
  {
    uint32_t color_timestamp;
    uint32_t depth_timestamp;
    uint32_t color_sequence;
    uint32_t depth_sequence;
    float synchronization_delta_ms;
    uint8_t synchronization_valid;
  } MpPoseFrameInfo;

  MP_POSE_BRIDGE_API int mp_pose_capture_open(const char* serial, const char* pipeline,
                                              MpPoseCapture** capture,
                                              MpPoseStreamInfo* stream_info, char* error,
                                              size_t error_capacity);

  MP_POSE_BRIDGE_API int mp_pose_capture_next(MpPoseCapture* capture, unsigned char* rgb,
                                              size_t rgb_capacity, int timeout_ms,
                                              MpPoseFrameInfo* frame_info);

  MP_POSE_BRIDGE_API int mp_pose_capture_lift(MpPoseCapture* capture, const float* normalized_xy,
                                              size_t landmark_count, int primary_radius,
                                              int fallback_radius, float cluster_span_mm,
                                              float* xyz_meters, uint8_t* valid);

  MP_POSE_BRIDGE_API const char* mp_pose_capture_last_error(const MpPoseCapture* capture);
  MP_POSE_BRIDGE_API void mp_pose_capture_close(MpPoseCapture* capture);

#ifdef __cplusplus
}
#endif

#endif
