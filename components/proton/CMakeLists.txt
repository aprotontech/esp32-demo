set(COMPONENT_ADD_INCLUDEDIRS "quark/sdk/include" "quark/framework/logs/include" "quark/framework/common/include" ".")

set(COMPONENT_REQUIRES esp32 lwip)

register_component()

include(./quark/esp32_env.cmake)

SET(QUARK_SYSTEM 2)
ADD_SUBDIRECTORY(quark)

target_link_libraries(${COMPONENT_TARGET} INTERFACE "-L ${CMAKE_CURRENT_BINARY_DIR}/quark/lib -L ${CMAKE_CURRENT_BINARY_DIR}/../lwip")

target_link_libraries(${COMPONENT_TARGET} INTERFACE quark driver player device ans http mqtt network rcutils md5 json system logs lwip)