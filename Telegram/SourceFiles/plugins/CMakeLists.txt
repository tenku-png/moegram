if (MOEGRAM_PLUGIN_TESTS)
    add_executable(moegram_composer_smoke tests/composer_smoke.cpp)
    target_include_directories(moegram_composer_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    init_target(moegram_composer_smoke)
    target_link_libraries(moegram_composer_smoke PRIVATE moegram::plugins desktop-app::lib_base Qt::Core)
    add_executable(moegram_request_smoke
        tests/request_smoke.cpp
        telegram_request_adapter.cpp
        ../mtproto/details/mtproto_serialized_request.cpp
        ../mtproto/mtproto_response.cpp
        $<FILTER:$<TARGET_OBJECTS:td_scheme>,EXCLUDE,scheme-dump_to_text>)
    target_include_directories(moegram_request_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    init_target(moegram_request_smoke)
    target_precompile_headers(moegram_request_smoke PRIVATE <scheme.h>)
    target_include_directories(moegram_request_smoke PRIVATE
        $<TARGET_PROPERTY:td_scheme,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(moegram_request_smoke PRIVATE
        moegram::plugins desktop-app::lib_tl Qt::Core)
endif()
