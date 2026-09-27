cmake_minimum_required(VERSION 3.16)
option(STREAMMASTER_O3 "Experimental O3 comparison build" OFF)
set(PROJECT_VER "0.2.3")
if(STREAMMASTER_O3)
    set(PROJECT_VER "0.2.3-o3")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(streammaster_onju_v3)
if(STREAMMASTER_O3)
    idf_build_set_property(COMPILE_OPTIONS "-O3" APPEND)
    # IDF 5.5.1 semihost readdir and RTC clocks trigger uninitialized errors at O3.
    # Keep these unrelated sources at their supported level; do not hide
    # warnings or patch the vendored SDK for this comparison.
    idf_component_get_property(streammaster_vfs vfs COMPONENT_LIB)
    set_property(SOURCE "$ENV{IDF_PATH}/components/vfs/vfs_semihost.c"
        TARGET_DIRECTORY ${streammaster_vfs} APPEND PROPERTY COMPILE_OPTIONS -O2)
    idf_component_get_property(streammaster_hw esp_hw_support COMPONENT_LIB)
    set_property(SOURCE "$ENV{IDF_PATH}/components/esp_hw_support/port/esp32s3/rtc_clk.c"
        TARGET_DIRECTORY ${streammaster_hw} APPEND PROPERTY COMPILE_OPTIONS -O2)
endif()
