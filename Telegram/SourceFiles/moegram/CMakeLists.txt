add_subdirectory(../plugins ${CMAKE_CURRENT_BINARY_DIR}/../plugins)

target_sources(Telegram PRIVATE
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
