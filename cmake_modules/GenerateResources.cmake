FUNCTION(GENERATE_RESOURCES OUTPUT BASE_FOLDER)

ADD_EXECUTABLE(generate_resources_tool
  tools/generate_resources.cpp
)

# The tool expects paths relative to BASE_FOLDER. Resources generated into the
# build tree (e.g. the compiled Metal shader library) arrive as absolute
# paths — relativize them here so the tool keeps a single path convention.
SET(RESOURCE_PATHS)
FOREACH(RESOURCE ${ARGN})
  IF(IS_ABSOLUTE ${RESOURCE})
    FILE(RELATIVE_PATH RESOURCE ${BASE_FOLDER} ${RESOURCE})
  ENDIF()
  LIST(APPEND RESOURCE_PATHS ${RESOURCE})
ENDFOREACH()

ADD_CUSTOM_COMMAND(
  OUTPUT ${OUTPUT}
  COMMAND generate_resources_tool ${BASE_FOLDER} ${RESOURCE_PATHS} > ${OUTPUT}
  WORKING_DIRECTORY ${BASE_FOLDER}
  DEPENDS generate_resources_tool ${ARGN}
)

ENDFUNCTION(GENERATE_RESOURCES)
