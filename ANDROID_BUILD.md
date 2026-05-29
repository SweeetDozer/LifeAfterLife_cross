# Android APK Build

This repository includes an Android wrapper under [`android/`](./android) that builds the CMake/Slint app into an APK through Gradle.

Android support is part of the MVP, but still experimental. The current APK is meant for internal testing on `arm64-v8a` devices.

## What It Does

- Builds the existing top-level `CMakeLists.txt`
- Compiles the app as a shared library on Android
- Packages that native library into an APK using `NativeActivity`

## Android Runtime Notes

- On Windows, the app still uses WinHTTP.
- On Android, the app uses JNI with `HttpURLConnection`.
- On Android, app data is stored in the app-private files directory under `app_data`.

## Required Tools

- Android Studio
- Android SDK Platform 35
- Android Build-Tools for API 35
- Android NDK 26.3.11579264
- CMake 3.22.1 inside Android Studio
- Java 17 or newer
- Rust toolchain in `PATH`
- Rust Android target: `aarch64-linux-android`

Rust is still needed because Slint's C++ runtime is built through Rust under the hood.

Install the Rust Android target once:

```powershell
rustup target add aarch64-linux-android
```

If Gradle cannot find Rust from `PATH`, add these optional entries to `android/local.properties`:

```properties
rustc.path=C\:\\Users\\<you>\\.rustup\\toolchains\\stable-x86_64-pc-windows-msvc\\bin\\rustc.exe
cargo.path=C\:\\Users\\<you>\\.rustup\\toolchains\\stable-x86_64-pc-windows-msvc\\bin\\cargo.exe
```

## Recommended First Build In Android Studio

1. Open the [`android/`](./android) folder in Android Studio.
2. Let Android Studio install any missing SDK, NDK, or Gradle components.
3. In the SDK Manager, confirm:
   - Android SDK Platform 35
   - NDK (Side by side)
   - CMake 3.22.1
4. Build the project from Android Studio.

## Command-Line Build

After Android Studio has installed the required SDK/NDK components, build from the command line.

From the repository root:

```powershell
cd android
.\gradlew.bat assembleDebug
```

Expected output:

```text
android\app\build\outputs\apk\debug\app-debug.apk
```

## Release Notes

- The debug APK is unsigned for store distribution.
- The release build type exists, but production signing/minification are not configured for this MVP.
- The Gradle project currently builds only `arm64-v8a`.
- Generated `.cxx`, Gradle cache, build logs, and copied native libraries should stay out of git.

## If Configuration Fails

Common causes:

- `cargo` or `rustc` are not in `PATH`
- Android NDK is missing
- CMake 3.22.1 is not installed in Android Studio
- Gradle cannot fetch dependencies on first run

## Next Technical Steps

1. Test login, tree loading, graph editing, and persistence on a real Android device.
2. Add production signing config when the APK is ready to leave internal testing.
3. Decide whether MVP Android distribution should use a debug APK, signed APK, or Play internal testing track.
