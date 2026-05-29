# Life After Life

Life After Life is a cross-platform MVP client for building and editing family and relationship trees. The app is written in C++20 with a Slint UI and talks to the LAL backend API at `https://api.lal.mors.space`.

## MVP Scope

This release is intended as a usable first public build, not a final product. It supports:

- Account registration, login, logout, and persisted sessions.
- Tree list loading, tree creation, renaming, deletion, and last-opened tree restore.
- Person creation, editing, deletion, and draggable placement on the graph canvas.
- Relationship creation and deletion for parent, spouse, sibling, and friend links.
- Canvas pan/zoom, auto-layout, and local layout persistence per tree.
- English/Russian UI strings.
- Windows desktop build and an experimental Android APK wrapper.

## Current Limits

- The HTTP client is implemented for Windows and Android only.
- Android support is MVP/experimental and currently targets `arm64-v8a`.
- Relationship editing is limited to create/delete; changing a relationship type is not implemented yet.
- Photos/media, sharing/access management, offline backend sync, and production signing are outside this MVP.

## Requirements

- CMake 3.21 or newer.
- A C++20 compiler.
- Rust toolchain in `PATH` because Slint may be fetched and built from source.
- Network access on first configure if Slint/Corrosion are not already available.

Windows builds also link against WinHTTP. Android builds need Android Studio, SDK/NDK, Java 17+, and the Rust Android target. See [ANDROID_BUILD.md](ANDROID_BUILD.md).

## Build On Windows

Configure:

```powershell
cmake -S . -B out -DCMAKE_BUILD_TYPE=Debug
```

Build:

```powershell
cmake --build out --config Debug
```

Run:

```powershell
.\out\Debug\LifeAfterLife.exe
```

For a quick local run, `z_run.bat` builds the Debug target and starts the app.

## Windows Installer

The repository includes an Inno Setup script:

```powershell
iscc setup.iss
```

The installer is written to `installer-output\LifeAfterLife-0.1.0-Setup.exe`.

## Android APK

Open the `android/` directory in Android Studio and build the `app` target, or use the Gradle wrapper after Android Studio has installed the required SDK/NDK components:

```powershell
cd android
.\gradlew.bat assembleDebug
```

The debug APK is expected at:

```text
android\app\build\outputs\apk\debug\app-debug.apk
```

## Local Data

Desktop local data is stored under `app_data/` in this repository during development. It contains session tokens, UI state, and per-tree layout files, so it should not be shipped or committed with real user credentials.

Before publishing a release, check [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md).

## License

MIT. See [LICENSE](LICENSE).
