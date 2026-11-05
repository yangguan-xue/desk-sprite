idf_component_register(
    SRCS "video_probe.c"
         "../../../firmware/main/hal/st7789.c"
         "../../../firmware/main/hal/es8311.c"
    INCLUDE_DIRS "../../../firmware/main/hal" "../../../firmware/src/include"
    REQUIRES driver esp_lcd esp_timer avi_player esp_jpeg
)
if(CMAKE_BUILD_EARLY_EXPANSION)
    return()
endif()
if(NOT DEFINED VIDEO_PROBE_ASSET OR NOT EXISTS "${VIDEO_PROBE_ASSET}")
    message(FATAL_ERROR "Generate and validate clip.avi, then set -DVIDEO_PROBE_ASSET=/absolute/path/clip.avi")
endif()
configure_file("${VIDEO_PROBE_ASSET}" "${CMAKE_CURRENT_BINARY_DIR}/clip.avi" COPYONLY)
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_BINARY_DIR}/clip.avi" BINARY)
