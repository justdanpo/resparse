cmake_minimum_required(VERSION 3.21.0)
set(target resparse)

set(CMAKE_CXX_STANDARD 20)
set(ABSL_PROPAGATE_CXX_STD ON)

include(FetchContent)
FetchContent_Declare(argy
    GIT_REPOSITORY https://github.com/mshenoda/argy.git
    GIT_TAG v0.6.0
    GIT_SHALLOW 1
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(argy)

set(WIL_BUILD_TESTS FALSE)
FetchContent_Declare(WIL
    GIT_REPOSITORY https://github.com/microsoft/wil.git
    GIT_TAG v1.0.250325.1
    GIT_SHALLOW 1
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(WIL)

project(${target}
    VERSION 1.0.0.0
    DESCRIPTION "resparse"
)

add_executable(${target}
    source/main.cpp
)

target_link_libraries(${target}
    PRIVATE
    argy
    PUBLIC
    WIL::WIL
)
