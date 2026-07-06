find_package(Doxygen)
IF(DOXYGEN_FOUND)
  CONFIGURE_FILE(Doxyfile.in "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile" @ONLY)
  add_custom_target(
    doc
    # 1. Emit this Doxygen version's default header so the template always
    #    matches the installed toolchain (footer/stylesheet are discarded).
    COMMAND ${DOXYGEN_EXECUTABLE} -w html
            "${CMAKE_CURRENT_BINARY_DIR}/header.default.html"
            "${CMAKE_CURRENT_BINARY_DIR}/footer.default.html"
            "${CMAKE_CURRENT_BINARY_DIR}/style.default.css"
            "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    # 2. Splice the doxygen-awesome-css JS extensions into the header.
    COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/header.default.html"
            "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/header.html"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/inject_awesome.cmake"
    # 3. Build the HTML documentation.
    COMMAND ${DOXYGEN_EXECUTABLE} "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating API documentation with Doxygen (doxygen-awesome theme)"
    VERBATIM
  )
ENDIF()
