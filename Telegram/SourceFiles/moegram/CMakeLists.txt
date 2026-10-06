add_subdirectory(../plugins ${CMAKE_CURRENT_BINARY_DIR}/../plugins)

target_sources(Telegram PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/composer/composer_bridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/composer/composer_bridge.h
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge/requests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge/requests.h
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge/request_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge/outgoing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge/outgoing.h
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge/outgoing_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge/events.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge/events.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/message_menu.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/message_menu.h
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge/application_bridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge/application_bridge.h
    ${src_loc}/plugins/telegram_event_adapter.cpp
    ${src_loc}/plugins/telegram_event_adapter.h
    ${src_loc}/plugins/telegram_request_adapter.cpp
    ${src_loc}/plugins/telegram_request_adapter.h
    ${src_loc}/settings/sections/settings_moegram_plugins.cpp
    ${src_loc}/settings/sections/settings_moegram_plugins.h
)
target_link_libraries(Telegram PRIVATE moegram::plugins)

if (MOEGRAM_PLUGIN_TESTS)
    add_executable(moegram_request_bridge_smoke
        tests/request_bridge_smoke.cpp
        bridge/requests.cpp
        bridge/outgoing.cpp
        ${src_loc}/plugins/telegram_request_adapter.cpp
        ${src_loc}/mtproto/details/mtproto_serialized_request.cpp
        ${src_loc}/mtproto/mtproto_response.cpp
        $<FILTER:$<TARGET_OBJECTS:td_scheme>,EXCLUDE,scheme-dump_to_text>)
    init_target(moegram_request_bridge_smoke)
    target_precompile_headers(moegram_request_bridge_smoke PRIVATE <scheme.h>)
    target_include_directories(moegram_request_bridge_smoke PRIVATE
        ${src_loc}
        $<TARGET_PROPERTY:td_scheme,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(moegram_request_bridge_smoke PRIVATE
        moegram::plugins desktop-app::lib_tl Qt::Core)
endif()
