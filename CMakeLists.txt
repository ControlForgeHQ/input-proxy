cmake_minimum_required(VERSION 3.16)

project(
    input-proxy
    VERSION 0.1.0
    LANGUAGES C
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(
    input-proxy
    src/main.c
    src/proxy_session.c
    src/result.c
    src/source_device.c
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