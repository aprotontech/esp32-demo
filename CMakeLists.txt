cmake_minimum_required(VERSION 3.5)

project(demo)


# Include for ESP-IDF build system functions
include($ENV{IDF_PATH}/tools/cmake/idf.cmake)
# Create idf::esp32 and idf::freertos static libraries
idf_build_process(esp32
                # try and trim the build; additional components
                # will be included as needed based on dependency tree
                #
                # although esptool_py does not generate static library,
                # processing the component is needed for flashing related
                # targets and file generation
                COMPONENTS esp32 freertos bt esptool_py
                SDKCONFIG ${CMAKE_CURRENT_LIST_DIR}/sdkconfig
                BUILD_DIR ${CMAKE_BINARY_DIR})


set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(elf_file ${CMAKE_PROJECT_NAME}.elf)

include(./quark/esp32_env.cmake)

ADD_SUBDIRECTORY(quark)

INCLUDE_DIRECTORIES(${QUARK_SOURCE_DIR}/include)
INCLUDE_DIRECTORIES(${RCUTILS_SOURCE_DIR}/include)
INCLUDE_DIRECTORIES(${LOGS_SOURCE_DIR}/include)
INCLUDE_DIRECTORIES(${SYSTEM_SOURCE_DIR}/include)
INCLUDE_DIRECTORIES(${HTTP_SOURCE_DIR})
link_directories("./build/quark/lib")
add_executable(${elf_file} main.c test/bt-scan.c test/player.c)

ADD_DEPENDENCIES(${elf_file} examples_static quark_static system_static)
target_link_libraries(${elf_file} examples quark driver device ans http mqtt rcutils md5 json system logs MQTTPacketClient MQTTPacketServer paho-embed-mqtt3cc)
target_link_libraries(${elf_file} idf::esp32 idf::freertos idf::bt idf::spi_flash)
# Attach additional targets to the executable file for flashing,
# linker script generation, partition_table generation, etc.
idf_build_executable(${elf_file})