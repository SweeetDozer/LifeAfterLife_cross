cmake_minimum_required(VERSION 3.21)
project(LifeAfterLife LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Rust/Cargo integration for the Android launcher cdylib.
FetchContent_Declare(
  Corrosion
  GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
  GIT_TAG v0.5
)
FetchContent_MakeAvailable(Corrosion)

# These must be set before Slint is configured. If they are set after
# FetchContent_MakeAvailable(Slint), Slint still builds optional crates such as
# slint-interpreter, which currently fails with the installed Rust toolchain.
if(ANDROID)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(SLINT_FEATURE_BACKEND_ANDROID_ACTIVITY_06 ON CACHE BOOL "" FORCE)
    set(SLINT_FEATURE_BACKEND_WINIT OFF CACHE BOOL "" FORCE)
    set(SLINT_FEATURE_BACKEND_QT OFF CACHE BOOL "" FORCE)
    set(SLINT_FEATURE_RENDERER_SOFTWARE ON CACHE BOOL "" FORCE)
    set(SLINT_FEATURE_RENDERER_FEMTOVG ON CACHE BOOL "" FORCE)
    set(SLINT_FEATURE_INTERPRETER OFF CACHE BOOL "" FORCE)
    set(SLINT_FEATURE_LIVE_PREVIEW OFF CACHE BOOL "" FORCE)
    set(SLINT_FEATURE_TESTING OFF CACHE BOOL "" FORCE)
    set(SLINT_FEATURE_SYSTEM_TESTING OFF CACHE BOOL "" FORCE)
endif()

find_package(Slint QUIET)
if(NOT Slint_FOUND)
  FetchContent_Declare(
    Slint
    GIT_REPOSITORY https://github.com/slint-ui/slint.git
    GIT_TAG v1.9.0
    SOURCE_SUBDIR api/cpp
  )
  FetchContent_MakeAvailable(Slint)
endif()

if(ANDROID)
    set(SLINT_CPP_CARGO_TOML "${slint_SOURCE_DIR}/api/cpp/Cargo.toml")
    file(READ "${SLINT_CPP_CARGO_TOML}" SLINT_CPP_CARGO)
    string(REPLACE
        "crate-type = [\"lib\", \"cdylib\", \"staticlib\"]"
        "crate-type = [\"lib\"]"
        SLINT_CPP_CARGO
        "${SLINT_CPP_CARGO}"
    )
    string(REPLACE
        "crate-type = [\"lib\", \"staticlib\"]"
        "crate-type = [\"lib\"]"
        SLINT_CPP_CARGO
        "${SLINT_CPP_CARGO}"
    )
    if(NOT SLINT_CPP_CARGO MATCHES "backend-android-activity-06")
        string(REPLACE
            "system-testing = [\"i-slint-backend-selector/system-testing\"]"
            "system-testing = [\"i-slint-backend-selector/system-testing\"]\nbackend-android-activity-06 = [\"dep:i-slint-backend-android-activity\", \"i-slint-backend-android-activity/native-activity\", \"i-slint-backend-android-activity/aa-06\", \"std\"]"
            SLINT_CPP_CARGO
            "${SLINT_CPP_CARGO}"
        )
        string(REPLACE
            "i-slint-backend-testing = { workspace = true, optional = true, features = [\"ffi\"] }"
            "i-slint-backend-testing = { workspace = true, optional = true, features = [\"ffi\"] }\ni-slint-backend-android-activity = { workspace = true, optional = true }"
            SLINT_CPP_CARGO
            "${SLINT_CPP_CARGO}"
        )
        file(WRITE "${SLINT_CPP_CARGO_TOML}" "${SLINT_CPP_CARGO}")
    endif()

    set(SLINT_CPP_LIB_RS "${slint_SOURCE_DIR}/api/cpp/lib.rs")
    file(READ "${SLINT_CPP_LIB_RS}" SLINT_CPP_LIB)
    if(NOT SLINT_CPP_LIB MATCHES "pub mod android")
        string(APPEND SLINT_CPP_LIB [=[

#[cfg(all(target_os = "android", feature = "backend-android-activity-06"))]
pub mod android {
    pub use i_slint_backend_android_activity::android_activity::AndroidApp;

    pub fn init(app: AndroidApp) -> Result<(), i_slint_core::platform::SetPlatformError> {
        i_slint_core::platform::set_platform(Box::new(
            i_slint_backend_android_activity::AndroidPlatform::new(app),
        ))
    }
}
]=])
        file(WRITE "${SLINT_CPP_LIB_RS}" "${SLINT_CPP_LIB}")
    endif()
endif()

set(LAL_SOURCES
    src/api/api_client.cpp
    src/api/auth_api.cpp
    src/api/tree_api.cpp
    src/main.cpp
    src/platform/android_support.cpp
    src/storage/persistence.cpp
    src/utils/formatters.cpp
    src/ui_sync/sync.cpp
)

if(ANDROID)
    get_filename_component(ANDROID_SDK_PATH "${ANDROID_NDK}/../.." ABSOLUTE)
    set(ANDROID_TOOLCHAIN_BIN "${ANDROID_NDK}/toolchains/llvm/prebuilt/windows-x86_64/bin")
    set(SLINT_GENERATED_INCLUDE_DIR "${slint_BINARY_DIR}/generated_include")
    set(SLINT_CPP_HEADER_STAMP "${SLINT_GENERATED_INCLUDE_DIR}/slint_internal.h")

    add_custom_command(
        OUTPUT "${SLINT_CPP_HEADER_STAMP}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SLINT_GENERATED_INCLUDE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E env
            "RUSTC=${Rust_COMPILER}"
            "SLINT_GENERATED_INCLUDE_DIR=${SLINT_GENERATED_INCLUDE_DIR}/"
            "${Rust_CARGO}" build
                --manifest-path "${slint_SOURCE_DIR}/Cargo.toml"
                --package slint-cpp
                --lib
                --no-default-features
                --features renderer-software,std
        WORKING_DIRECTORY "${slint_SOURCE_DIR}"
        COMMENT "Generating Slint C++ headers for Android"
        VERBATIM
    )
    add_custom_target(slint_cpp_headers DEPENDS "${SLINT_CPP_HEADER_STAMP}")

    add_library(lal_core STATIC ${LAL_SOURCES})
    set_target_properties(lal_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_dependencies(lal_core slint_cpp_headers)
    target_include_directories(lal_core PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${SLINT_GENERATED_INCLUDE_DIR}"
        "${slint_SOURCE_DIR}/api/cpp/include"
    )
    target_link_libraries(lal_core PRIVATE android log)
    slint_target_sources(lal_core ui/app-window.slint)

    set(LAL_ANDROID_LAUNCHER_DIR "${CMAKE_CURRENT_BINARY_DIR}/android-launcher")
    file(MAKE_DIRECTORY "${LAL_ANDROID_LAUNCHER_DIR}/src")

    set(SLINT_SOURCE_DIR "${slint_SOURCE_DIR}")
    string(REPLACE "\\" "/" SLINT_SOURCE_DIR "${SLINT_SOURCE_DIR}")

    configure_file(android-launcher/Cargo.toml.in "${LAL_ANDROID_LAUNCHER_DIR}/Cargo.toml" @ONLY)
    configure_file(android-launcher/src/lib.rs "${LAL_ANDROID_LAUNCHER_DIR}/src/lib.rs" COPYONLY)
    configure_file(android-launcher/build.rs "${LAL_ANDROID_LAUNCHER_DIR}/build.rs" COPYONLY)

    corrosion_import_crate(
        MANIFEST_PATH "${LAL_ANDROID_LAUNCHER_DIR}/Cargo.toml"
        CRATE_TYPES cdylib
    )

    corrosion_set_env_vars(lal_android_launcher
        ANDROID_HOME=${ANDROID_SDK_PATH}
        ANDROID_NDK=${ANDROID_NDK}
        SLINT_GENERATED_INCLUDE_DIR=${SLINT_GENERATED_INCLUDE_DIR}/
    )

    corrosion_link_libraries(lal_android_launcher lal_core)
else()
    add_executable(${PROJECT_NAME} ${LAL_SOURCES})
    target_link_libraries(${PROJECT_NAME} PRIVATE Slint::Slint)
    if(WIN32)
        target_link_libraries(${PROJECT_NAME} PRIVATE winhttp)
    endif()
    slint_target_sources(${PROJECT_NAME} ui/app-window.slint)
endif()
