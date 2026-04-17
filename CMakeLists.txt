cmake_minimum_required(VERSION 3.21)
project(LifeAfterLife LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Slint QUIET)
if (NOT Slint_FOUND)
  message("Slint could not be located in the CMake module search path. Downloading it from Git and building it locally")
  include(FetchContent)
  FetchContent_Declare(
    Slint
    GIT_REPOSITORY https://github.com/slint-ui/slint.git
    GIT_TAG release/1
    SOURCE_SUBDIR api/cpp
  )
  FetchContent_MakeAvailable(Slint)
endif()

add_executable(${PROJECT_NAME}
    src/main.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE Slint::Slint)

slint_target_sources(${PROJECT_NAME}
    ui/app-window.slint
)

if (WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}>
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMAND_EXPAND_LISTS
    )
endif()
