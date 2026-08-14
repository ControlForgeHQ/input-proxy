cmake_minimum_required(VERSION 3.16)

project(
    input-proxy
    VERSION 0.2.0
    LANGUAGES C
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

configure_file(
    include/input_proxy/version.h.in
    generated/include/input_proxy/version.h
    @ONLY
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)
pkg_check_modules(LIBSYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

add_executable(
    input-proxy
    src/device_discovery.c
    src/device_inspection.c
    src/instance_name.c
    src/main.c
    src/proxy_session.c
    src/runtime_control.c
    src/runtime_discovery.c
    src/result.c
    src/source_device.c
    src/virtual_device.c
)

target_include_directories(
    input-proxy
    PRIVATE
        ${PROJECT_BINARY_DIR}/generated/include
        ${PROJECT_SOURCE_DIR}/include
)

install(
    TARGETS input-proxy
    RUNTIME DESTINATION bin
)

target_link_libraries(
    input-proxy
    PRIVATE
        PkgConfig::LIBEVDEV
        PkgConfig::LIBSYSTEMD
)

include(CTest)

if(BUILD_TESTING)
    add_executable(
        device-discovery-test
        tests/device_discovery_test.c
        src/device_discovery.c
    )

    target_include_directories(
        device-discovery-test
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
            ${PROJECT_SOURCE_DIR}/src
    )

    add_test(NAME device-discovery-test COMMAND device-discovery-test)

    add_executable(
        device-inspection-test
        tests/device_inspection_test.c
        src/device_inspection.c
        src/device_discovery.c
        src/runtime_discovery.c
    )
    target_include_directories(
        device-inspection-test
        PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(device-inspection-test PRIVATE
        PkgConfig::LIBEVDEV PkgConfig::LIBSYSTEMD)
    add_test(NAME device-inspection-test COMMAND device-inspection-test)

    add_executable(
        source-device-test
        tests/source_device_test.c
        src/result.c
        src/source_device.c
    )

    target_include_directories(
        source-device-test
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
            ${PROJECT_SOURCE_DIR}/src
    )

    target_link_libraries(
        source-device-test
        PRIVATE
            PkgConfig::LIBEVDEV
    )

    add_test(NAME source-device-test COMMAND source-device-test)

    add_executable(
        virtual-device-test
        tests/virtual_device_test.c
        src/result.c
        src/virtual_device.c
    )

    target_include_directories(
        virtual-device-test
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
            ${PROJECT_SOURCE_DIR}/src
    )

    target_link_libraries(
        virtual-device-test
        PRIVATE
            PkgConfig::LIBEVDEV
    )

    add_test(NAME virtual-device-test COMMAND virtual-device-test)

    add_executable(
        proxy-session-test
        tests/proxy_session_test.c
        src/proxy_session.c
        src/runtime_control.c
        src/instance_name.c
        src/result.c
    )

    target_include_directories(
        proxy-session-test
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_BINARY_DIR}/generated/include
    )

    target_link_libraries(proxy-session-test PRIVATE PkgConfig::LIBSYSTEMD)
    target_link_options(
        proxy-session-test
        PRIVATE
            "-Wl,--wrap=input_proxy_runtime_control_apply_changes"
            "-Wl,--wrap=input_proxy_runtime_control_create"
            "-Wl,--wrap=input_proxy_runtime_control_recreate"
            "-Wl,--wrap=input_proxy_runtime_control_process"
            "-Wl,--wrap=input_proxy_runtime_control_wait"
            "-Wl,--wrap=input_proxy_runtime_control_destroy"
    )

    add_test(NAME proxy-session-test COMMAND proxy-session-test)

    add_executable(
        instance-name-test
        tests/instance_name_test.c
        src/instance_name.c
        src/result.c
    )

    target_include_directories(
        instance-name-test
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
            ${PROJECT_SOURCE_DIR}/src
    )

    add_test(NAME instance-name-test COMMAND instance-name-test)

    add_executable(
        runtime-control-test
        tests/runtime_control_test.c
        src/runtime_control.c
        src/result.c
    )
    target_include_directories(
        runtime-control-test
        PRIVATE
            ${PROJECT_BINARY_DIR}/generated/include
            ${PROJECT_SOURCE_DIR}/include
            ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(runtime-control-test PRIVATE PkgConfig::LIBSYSTEMD)
    target_link_options(
        runtime-control-test
        PRIVATE
            "-Wl,--wrap=sd_bus_add_object_vtable"
            "-Wl,--wrap=sd_bus_emit_properties_changed_strv"
            "-Wl,--wrap=sd_bus_flush_close_unref"
            "-Wl,--wrap=sd_bus_open_system"
            "-Wl,--wrap=sd_bus_process"
            "-Wl,--wrap=sd_bus_request_name"
            "-Wl,--wrap=sd_bus_slot_unref"
    )
    add_test(NAME runtime-control-test COMMAND runtime-control-test)

    add_executable(
        runtime-discovery-test
        tests/runtime_discovery_test.c
        src/runtime_discovery.c
    )
    target_include_directories(
        runtime-discovery-test
        PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(runtime-discovery-test PRIVATE PkgConfig::LIBSYSTEMD)
    add_test(NAME runtime-discovery-test COMMAND runtime-discovery-test)

    add_test(
        NAME cli-test
        COMMAND
            ${CMAKE_COMMAND}
            -DINPUT_PROXY=$<TARGET_FILE:input-proxy>
            -DINPUT_PROXY_VERSION=${PROJECT_VERSION}
            -P ${PROJECT_SOURCE_DIR}/tests/cli_test.cmake
    )
endif()
