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
                COMPONENTS esp32 freertos esptool_py
                SDKCONFIG ${CMAKE_CURRENT_LIST_DIR}/sdkconfig
                BUILD_DIR ${CMAKE_BINARY_DIR})


set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(elf_file ${CMAKE_PROJECT_NAME}.elf)

#################### GET ALL INCLUDES #############################
idf_build_get_property(all_incs INCLUDE_DIRECTORIES)
set(all_deps freertos nvs_flash)
set(watched_deps )
list(LENGTH all_deps new_count)
message(STATUS "count=${new_count}")
WHILE (1)
    set(last_count ${new_count})
    #message(STATUS "current deps=${all_deps}")
    foreach(_var ${all_deps})
        list(FIND watched_deps ${_var} hasdep)
        if (${hasdep} STREQUAL "-1")
            message(STATUS "try to expand ${_var}")
            list(APPEND watched_deps ${_var})
            idf_component_get_property(cpth ${_var} COMPONENT_DIR)
            idf_component_get_property(incs ${_var} INCLUDE_DIRS)
            message(STATUS "${_var} incs=${incs}")
            idf_component_get_property(deps ${_var} REQUIRES)
            message(STATUS "${_var} deps=${deps}")
            foreach(_inc ${incs})
                list(APPEND all_incs ${cpth}/${_inc})
            endforeach()

            foreach(_dep ${deps})
                list(FIND watched_deps ${_dep} hasdep)
                if (${hasdep} STREQUAL "-1")
                    list(APPEND all_deps ${_dep})
                    #message(STATUS "append new ${_dep} to expand")
                endif()
            endforeach()
        endif()
    endforeach()
    list(LENGTH all_deps new_count)
    #message(STATUS "last_count=${last_count}, new_count=${new_count}")
    if (${new_count} STREQUAL ${last_count})
        break()
    endif()
ENDWHILE()
message(STATUS "all deps=${all_deps}")
message(STATUS "all incs=${all_incs}")
#################### GET ALL INCLUDES #############################

ADD_SUBDIRECTORY(quark)

INCLUDE_DIRECTORIES("./quark/sdk/include")
link_directories("./build/quark/lib")
add_executable(${elf_file} main.c)

ADD_DEPENDENCIES(${elf_file} quark_static)
target_link_libraries(${elf_file} libquark.a driver logs)
target_link_libraries(${elf_file} idf::esp32 idf::freertos idf::spi_flash)
# Attach additional targets to the executable file for flashing,
# linker script generation, partition_table generation, etc.
idf_build_executable(${elf_file})