cmake_minimum_required(VERSION 3.10)
project(permup VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build tests" OFF)

add_compile_definitions(_GNU_SOURCE)
add_compile_options(-Wall -Wextra -Wpedantic -O2 -D_FORTIFY_SOURCE=2)

include_directories(include)
include_directories(src/common)

add_library(permup_common STATIC
    src/common/config.cpp
    src/common/protocol.cpp
    src/common/socket.cpp
    src/common/utils.cpp
)

target_include_directories(permup_common PUBLIC include)
target_include_directories(permup_common PRIVATE src/common)

add_executable(permupd
    src/permupd/main.cpp
    src/permupd/daemon.cpp
    src/permupd/child.cpp
)

target_link_libraries(permupd
    permup_common
    pthread
    util
)

target_include_directories(permupd PRIVATE src/permupd)

install(TARGETS permupd RUNTIME DESTINATION /usr/sbin)

add_executable(permup
    src/permup/main.cpp
    src/permup/client.cpp
    src/permup/session.cpp
    src/permup/ratelimit.cpp
)

target_link_libraries(permup
    permup_common
    pthread
)

target_include_directories(permup PRIVATE src/permup)

install(TARGETS permup RUNTIME DESTINATION /usr/bin)

if(BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    include_directories(${GTEST_INCLUDE_DIRS})
    
    add_executable(test_permup
        tests/unit/test_config.cpp
        tests/unit/test_protocol.cpp
        tests/unit/test_utils.cpp
    )
    target_link_libraries(test_permup permup_common ${GTEST_LIBRARIES} pthread)
    add_test(NAME permup_test COMMAND test_permup)
endif()

install(FILES
    conf/permup.cfg
    conf/permdown.cfg
    conf/shell.rc
    conf/permup.time
    conf/permup.session
    conf/permup.ratelimit
    DESTINATION /etc/permup
)

install(PROGRAMS
    init/systemd/permupd.service
    DESTINATION /etc/systemd/system
)