ecm_add_tests(osdactions.cpp LINK_LIBRARIES Qt6::Test Qt6::DBus KF6::Screen kscreen_common)
target_compile_definitions(osdactions PRIVATE "-DTEST_DATA=\"${CMAKE_CURRENT_SOURCE_DIR}/\"")

# kcmshell6 < 6.27.0 doesn't know the --smoke-test param, so skip this test for
# now.
# TODO: Drop this when KF6_MIN_VERSION >= 6.27.0
if(KF6KCMUtils_VERSION VERSION_GREATER_EQUAL "6.27.0")
    add_test(NAME kcm_smoketest COMMAND kcmshell6 --smoke-test kcm_kscreen)
    set_tests_properties(kcm_smoketest PROPERTIES
        ENVIRONMENT_MODIFICATION QT_PLUGIN_PATH=path_list_prepend:${CMAKE_BINARY_DIR}/bin
    )
endif()

if(HAVE_KSCREEN_STEREO)
    ecm_add_tests(stereomodes.cpp LINK_LIBRARIES Qt6::Test KF6::I18n KF6::Screen)

    ecm_add_test(anaglyphselection.cpp
        ../kcm/output_model.cpp ../kcm/config_handler.cpp
        ${CMAKE_BINARY_DIR}/kcm/kcm_screen_debug.cpp
        TEST_NAME anaglyphselection
        LINK_LIBRARIES Qt6::Test Qt6::Gui Qt6::DBus KF6::Screen KF6::I18n KF6::ConfigCore KF6::CoreAddons kscreen_common)
    target_include_directories(anaglyphselection PRIVATE ${CMAKE_BINARY_DIR}/kcm)
endif()
