ADD_EXECUTABLE(KinectCameraCalibration
  KinectCameraCalibration.cpp
)

TARGET_INCLUDE_DIRECTORIES(KinectCameraCalibration PRIVATE
  "${MY_DIR}/third_party"
  ${OpenCV_INCLUDE_DIRS}
)
TARGET_COMPILE_FEATURES(KinectCameraCalibration PRIVATE cxx_std_17)
TARGET_LINK_LIBRARIES(KinectCameraCalibration PRIVATE freenect2 ${OpenCV_LIBS})

IF(COMMAND libfreenect2_apply_quality)
  libfreenect2_apply_quality(KinectCameraCalibration PRIVATE)
ENDIF()

INSTALL(TARGETS KinectCameraCalibration DESTINATION bin)
