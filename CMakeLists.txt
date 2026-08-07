cmake_minimum_required(VERSION 3.16)

project(
    input-proxy
    VERSION 0.1.0
    LANGUAGES C
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)

add_executable(
    input-proxy
    src/main.c
    src/proxy_session.c
    src/result.c
    src/source_device.c
    src/virtual_device.c
)

target_include_directories(
    input-proxy
    PRIVATE
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
)

include(CTest)

if(BUILD_TESTING)
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
        src/result.c
    )

    target_include_directories(
        proxy-session-test
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
            ${PROJECT_SOURCE_DIR}/src
    )

    add_test(NAME proxy-session-test COMMAND proxy-session-test)
endif()
