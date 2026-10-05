set(Python3_FIND_IMPLEMENTATIONS CPython)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter Development.Embed)

add_library(moegram_plugins STATIC)
add_library(moegram::plugins ALIAS moegram_plugins)
init_target(moegram_plugins)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/runtime.py" plugin_runtime_source)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS runtime.py)
configure_file(runtime_source.h.in runtime_source.h @ONLY)

target_sources(moegram_plugins PRIVATE plugin_host.cpp plugin_host.h)
target_include_directories(moegram_plugins PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(moegram_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(moegram_plugins PRIVATE Python3::Python desktop-app::lib_base)

option(MOEGRAM_PLUGIN_TESTS "Build the embedded plugin host smoke test" OFF)
if (MOEGRAM_PLUGIN_TESTS)
    add_executable(moegram_request_smoke
        tests/request_smoke.cpp
        telegram_request_adapter.cpp
        ../mtproto/details/mtproto_serialized_request.cpp
        ../mtproto/mtproto_response.cpp
        $<FILTER:$<TARGET_OBJECTS:td_scheme>,EXCLUDE,scheme-dump_to_text>)
    init_target(moegram_request_smoke)
    target_precompile_headers(moegram_request_smoke PRIVATE <scheme.h>)
    target_include_directories(moegram_request_smoke PRIVATE
        $<TARGET_PROPERTY:td_scheme,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(moegram_request_smoke PRIVATE
        moegram::plugins desktop-app::lib_tl Qt::Core)
    add_executable(moegram_send_smoke tests/send_smoke.cpp)
    init_target(moegram_send_smoke)
    target_link_libraries(moegram_send_smoke PRIVATE moegram::plugins Qt::Core)
    add_executable(moegram_compatibility_smoke tests/compatibility_smoke.cpp)
    init_target(moegram_compatibility_smoke)
    target_link_libraries(moegram_compatibility_smoke PRIVATE moegram::plugins Qt::Core)
    add_executable(moegram_event_smoke tests/event_smoke.cpp)
    init_target(moegram_event_smoke)
    target_link_libraries(moegram_event_smoke PRIVATE moegram::plugins Qt::Core)
    add_executable(moegram_plugin_smoke tests/host_smoke.cpp)
    init_target(moegram_plugin_smoke)
    target_link_libraries(moegram_plugin_smoke PRIVATE moegram::plugins Qt::Core)
endif()
