# Validate that every user-facing Markdown guide is generated once and is
# reachable through the curated Doxygen navigation.
#
# Required variables:
#   DOC_DIR  - source documentation directory
#   LAYOUT   - Doxygen layout file
# Optional:
#   HTML_DIR - generated HTML directory; enables post-build output checks

if(NOT DEFINED DOC_DIR OR NOT DEFINED LAYOUT)
  message(FATAL_ERROR "check_navigation.cmake requires DOC_DIR and LAYOUT")
endif()

file(READ "${LAYOUT}" NAVIGATION)
file(GLOB DOC_PAGES "${DOC_DIR}/*.md")

if(NOT DOC_PAGES)
  message(FATAL_ERROR "No Markdown documentation pages found under ${DOC_DIR}")
endif()

if(DEFINED HTML_DIR)
  foreach(NAVIGATION_OUTPUT IN ITEMS menudata.js guides.html)
    if(NOT EXISTS "${HTML_DIR}/${NAVIGATION_OUTPUT}")
      message(FATAL_ERROR
        "Generated documentation is missing ${NAVIGATION_OUTPUT}")
    endif()
  endforeach()
  file(READ "${HTML_DIR}/menudata.js" GENERATED_NAVIGATION)
  file(READ "${HTML_DIR}/guides.html" GENERATED_GUIDES)
endif()

foreach(PAGE IN LISTS DOC_PAGES)
  file(STRINGS "${PAGE}" PAGE_HEADING
       REGEX "^#[ \t]+.*\\{#[A-Za-z0-9_]+\\}[ \t]*$"
       LIMIT_COUNT 1)
  if(NOT PAGE_HEADING)
    message(FATAL_ERROR
      "${PAGE} needs a level-one title with a stable Doxygen ID, for example: # Title {#page_id}")
  endif()

  string(REGEX MATCH "\\{#([A-Za-z0-9_]+)\\}" PAGE_ID_MATCH "${PAGE_HEADING}")
  set(PAGE_ID "${CMAKE_MATCH_1}")
  string(REGEX MATCHALL "url=\"@ref ${PAGE_ID}\"" NAVIGATION_LINKS "${NAVIGATION}")
  list(LENGTH NAVIGATION_LINKS LINK_COUNT)
  if(NOT LINK_COUNT EQUAL 1)
    message(FATAL_ERROR
      "${PAGE} (${PAGE_ID}) must appear exactly once in ${LAYOUT}; found ${LINK_COUNT} entries")
  endif()

  if(DEFINED HTML_DIR AND NOT EXISTS "${HTML_DIR}/${PAGE_ID}.html")
    message(FATAL_ERROR
      "Doxygen did not generate the canonical page ${HTML_DIR}/${PAGE_ID}.html")
  endif()
  if(DEFINED HTML_DIR)
    foreach(NAVIGATION_DOCUMENT IN ITEMS GENERATED_NAVIGATION GENERATED_GUIDES)
      string(FIND "${${NAVIGATION_DOCUMENT}}" "${PAGE_ID}.html" NAVIGATION_POSITION)
      if(NAVIGATION_POSITION EQUAL -1)
        message(FATAL_ERROR
          "${PAGE_ID}.html is missing from ${NAVIGATION_DOCUMENT}")
      endif()
    endforeach()
  endif()
endforeach()

foreach(REQUIRED_PAGE IN ITEMS getting_started guides)
  string(REGEX MATCHALL "url=\"@ref ${REQUIRED_PAGE}\"" REQUIRED_LINKS "${NAVIGATION}")
  list(LENGTH REQUIRED_LINKS REQUIRED_LINK_COUNT)
  if(NOT REQUIRED_LINK_COUNT EQUAL 1)
    message(FATAL_ERROR
      "${REQUIRED_PAGE} must appear exactly once in ${LAYOUT}; found ${REQUIRED_LINK_COUNT} entries")
  endif()
  if(DEFINED HTML_DIR AND NOT EXISTS "${HTML_DIR}/${REQUIRED_PAGE}.html")
    message(FATAL_ERROR
      "Doxygen did not generate ${HTML_DIR}/${REQUIRED_PAGE}.html")
  endif()
endforeach()

if(DEFINED HTML_DIR)
  foreach(REQUIRED_FILE IN ITEMS
      index.html
      topics.html
      annotated.html
      doxygen-awesome.css
      Doxyextra.css
      doxygen-awesome-compat.js
      doxygen-awesome-darkmode-toggle.js
      doxygen-awesome-fragment-copy-button.js
      doxygen-awesome-paragraph-link.js
      doxygen-awesome-interactive-toc.js
      calibration_job.example.json)
    if(NOT EXISTS "${HTML_DIR}/${REQUIRED_FILE}")
      message(FATAL_ERROR "Generated documentation is missing ${REQUIRED_FILE}")
    endif()
  endforeach()

  file(GLOB RAW_MARKDOWN_PAGES "${HTML_DIR}/*_8md.html")
  if(RAW_MARKDOWN_PAGES)
    message(FATAL_ERROR
      "Raw Markdown file-reference pages should not be generated: ${RAW_MARKDOWN_PAGES}")
  endif()

  file(READ "${HTML_DIR}/index.html" GENERATED_INDEX)
  foreach(REQUIRED_ASSET IN ITEMS
      "doxygen-awesome.css"
      "Doxyextra.css"
      "doxygen-awesome-compat.js"
      "doxygen-awesome-darkmode-toggle.js"
      "doxygen-awesome-fragment-copy-button.js"
      "doxygen-awesome-paragraph-link.js"
      "doxygen-awesome-interactive-toc.js")
    string(FIND "${GENERATED_INDEX}" "${REQUIRED_ASSET}" ASSET_POSITION)
    if(ASSET_POSITION EQUAL -1)
      message(FATAL_ERROR "index.html does not load ${REQUIRED_ASSET}")
    endif()
  endforeach()

  file(READ "${HTML_DIR}/calibration_profiles.html" CALIBRATION_PAGE)
  string(FIND "${CALIBRATION_PAGE}" "href=\"calibration_job.example.json\"" DOWNLOAD_POSITION)
  if(DOWNLOAD_POSITION EQUAL -1)
    message(FATAL_ERROR
      "calibration_profiles.html does not link the downloadable calibration job")
  endif()

  foreach(FORBIDDEN_ASSET IN ITEMS
      "doxygen-awesome-sidebar-only.css"
      "doxygen-awesome-sidebar-only-darkmode-toggle.css"
      "doxygen-awesome-tabs.js"
      "clipboard.js")
    string(FIND "${GENERATED_INDEX}" "${FORBIDDEN_ASSET}" ASSET_POSITION)
    if(NOT ASSET_POSITION EQUAL -1)
      message(FATAL_ERROR "index.html unexpectedly loads ${FORBIDDEN_ASSET}")
    endif()
  endforeach()
endif()
