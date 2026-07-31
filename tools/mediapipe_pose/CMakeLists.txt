CMAKE_MINIMUM_REQUIRED(VERSION 3.16)

ADD_LIBRARY(mediapipe_kinect_bridge SHARED
  bridge_helpers.cpp
  kinect_bridge.cpp
)

TARGET_COMPILE_FEATURES(mediapipe_kinect_bridge PRIVATE cxx_std_11)
TARGET_INCLUDE_DIRECTORIES(mediapipe_kinect_bridge PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(mediapipe_kinect_bridge PRIVATE freenect2)

SET_TARGET_PROPERTIES(mediapipe_kinect_bridge PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  OUTPUT_NAME mediapipe_kinect_bridge
)

IF(APPLE)
  SET_TARGET_PROPERTIES(mediapipe_kinect_bridge PROPERTIES
    INSTALL_RPATH "@loader_path"
  )
ENDIF()
