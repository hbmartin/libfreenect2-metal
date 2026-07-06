# - Try to find GLFW3
#
# If no pkgconfig, define GLFW_ROOT to installation tree
# Will define the following:
# GLFW3_FOUND
# GLFW3_INCLUDE_DIRS
# GLFW3_LIBRARIES

# Prefer GLFW's own CMake package config (Homebrew, vcpkg, official
# packages all install lib/cmake/glfw3). This works out of the box in
# /opt/homebrew on Apple Silicon and /usr/local on Intel Macs.
FIND_PACKAGE(glfw3 CONFIG QUIET)
IF(TARGET glfw)
  SET(GLFW3_LIBRARIES glfw)
  GET_TARGET_PROPERTY(GLFW3_INCLUDE_DIRS glfw INTERFACE_INCLUDE_DIRECTORIES)
  IF(NOT GLFW3_INCLUDE_DIRS)
    SET(GLFW3_INCLUDE_DIRS "")
  ENDIF()
  IF(WIN32)
    # A shared glfw imported target's file is glfw3.dll itself; record it so
    # the DLL-deploy step can copy it next to Protonect.exe. Use a generator
    # expression so a multi-config build deploys the matching configuration's
    # DLL, rather than freezing one configuration's path at configure time.
    GET_TARGET_PROPERTY(_glfw3_type glfw TYPE)
    IF(_glfw3_type STREQUAL "SHARED_LIBRARY")
      SET(GLFW3_DLL "$<TARGET_FILE:glfw>")
    ENDIF()
  ENDIF()
  SET(GLFW3_FOUND TRUE)
  RETURN()
ENDIF()

IF(PKG_CONFIG_FOUND)
  IF(APPLE)
    # Homebrew (arm64 and Intel prefixes) and MacPorts pkgconfig locations;
    # append rather than overwrite the user's PKG_CONFIG_PATH.
    SET(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:/opt/homebrew/opt/glfw/lib/pkgconfig:/usr/local/opt/glfw/lib/pkgconfig:/usr/local/opt/glfw3/lib/pkgconfig:/opt/local/lib/pkgconfig")
  ENDIF()
  SET(ENV{PKG_CONFIG_PATH} "${DEPENDS_DIR}/glfw/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
  PKG_CHECK_MODULES(GLFW3 glfw3)

  IF(GLFW3_FOUND)
    FIND_LIBRARY(GLFW3_LIBRARY
      NAMES ${GLFW3_LIBRARIES}
      HINTS ${GLFW3_LIBRARY_DIRS}
    )
    SET(GLFW3_LIBRARIES ${GLFW3_LIBRARY})

    RETURN()
  ENDIF()
  # fall through to a manual search if pkg-config doesn't know glfw3
ENDIF()

FIND_PATH(GLFW3_INCLUDE_DIRS
  GLFW/glfw3.h
  DOC "GLFW include directory "
  PATHS
    "${DEPENDS_DIR}/glfw"
    "$ENV{ProgramW6432}/glfw"
    ENV GLFW_ROOT
    /opt/homebrew
    /usr/local
    /opt/local
  PATH_SUFFIXES
    include
)

# directories in the official binary package
IF(MINGW)
  SET(_SUFFIX lib-mingw)
ELSEIF(MSVC11)
  SET(_SUFFIX lib-vc2012)
ELSEIF(MSVC12)
  SET(_SUFFIX lib-vc2013)
ELSEIF(MSVC14)
  SET(_SUFFIX lib-vc2015)
ELSEIF(MSVC)
  SET(_SUFFIX lib-vc2012)
ENDIF()

FIND_LIBRARY(GLFW3_LIBRARIES
  NAMES glfw3dll glfw3 glfw
  PATHS
    "${DEPENDS_DIR}/glfw"
    "$ENV{ProgramW6432}/glfw"
    ENV GLFW_ROOT
    /opt/homebrew
    /usr/local
    /opt/local
  PATH_SUFFIXES
    lib
    ${_SUFFIX}
)

IF(WIN32)
FIND_FILE(GLFW3_DLL
  glfw3.dll
  PATHS
    "${DEPENDS_DIR}/glfw"
    "$ENV{ProgramW6432}/glfw"
    ENV GLFW_ROOT
  PATH_SUFFIXES
    ${_SUFFIX}
)
ENDIF()

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(GLFW3 FOUND_VAR GLFW3_FOUND
  REQUIRED_VARS GLFW3_LIBRARIES GLFW3_INCLUDE_DIRS)
