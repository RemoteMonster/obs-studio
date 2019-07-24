project(libremonobs)

set( REMON_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

if(WIN32)
set( REMON_LIB "libremonobs.dll")
else()
set( REMON_LIB "libremonobs.so")
endif()

add_library(libremonobs SHARED IMPORTED)
set_property(TARGET libremonobs PROPERTY IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/${REMON_LIB}")

install(FILES ${REMON_LIB}
	DESTINATION ${OBS_EXECUTABLE_DESTINATION})
