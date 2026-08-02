find_package(Doxygen)
IF(DOXYGEN_FOUND)
  CONFIGURE_FILE(Doxyfile.in "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile" @ONLY)
  add_custom_target(
    doc
    # 1. Make sure every top-level guide is represented in the curated nav.
    COMMAND ${CMAKE_COMMAND}
            "-DDOC_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DLAYOUT=${CMAKE_CURRENT_SOURCE_DIR}/DoxygenLayout.xml"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_navigation.cmake"
    # 2. Emit this Doxygen version's default header so the template always
    #    matches the installed toolchain (footer/stylesheet are discarded).
    COMMAND ${DOXYGEN_EXECUTABLE} -w html
            "${CMAKE_CURRENT_BINARY_DIR}/header.default.html"
            "${CMAKE_CURRENT_BINARY_DIR}/footer.default.html"
            "${CMAKE_CURRENT_BINARY_DIR}/style.default.css"
            "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    # 3. Splice the doxygen-awesome-css JS extensions into the header.
    COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/header.default.html"
            "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/header.html"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/inject_awesome.cmake"
    # 4. Build the HTML documentation.
    COMMAND ${DOXYGEN_EXECUTABLE} "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    # 5. Check canonical pages and required/forbidden generated assets.
    COMMAND ${CMAKE_COMMAND}
            "-DDOC_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DLAYOUT=${CMAKE_CURRENT_SOURCE_DIR}/DoxygenLayout.xml"
            "-DHTML_DIR=${CMAKE_CURRENT_BINARY_DIR}/html"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_navigation.cmake"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating curated API documentation with Doxygen Awesome"
    VERBATIM
  )
ENDIF()
