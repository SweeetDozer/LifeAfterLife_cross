# MVP Release Checklist

Use this before publishing a Windows installer or Android APK.

## Version

- Confirm version is `0.1.0` in `setup.iss`.
- Confirm Android `versionName`/`versionCode` in `android/app/build.gradle`.
- Update `CHANGELOG.md` if the build contains user-visible changes.

## Build

- Clean build the Windows target:

```powershell
cmake -S . -B out -DCMAKE_BUILD_TYPE=Debug
cmake --build out --config Debug
```

- Build the Windows installer:

```powershell
iscc setup.iss
```

- Build the Android debug APK:

```powershell
cd android
.\gradlew.bat assembleDebug
```

## Smoke Test

- Register a new account.
- Log in with an existing account.
- Create, rename, select, and delete a tree.
- Add, move, edit, and delete a person.
- Add and delete each relationship type: parent, spouse, sibling, friend.
- Pan and zoom the canvas.
- Close and reopen the app; verify session and layout restore.
- Log out and verify the auth screen returns.

## Git Hygiene

- Do not ship local session data from `app_data/`.
- Do not commit generated build output from `out/`, `build/`, `installer-output/`, or `android/app/build/`.
- Do not commit Android local files such as `android/local.properties`, `.cxx`, `build_log.txt`, or copied `jniLibs` output.
- Review `git status --short` before tagging or sharing a build.

## Distribution

- Windows MVP artifact: `installer-output\LifeAfterLife-0.1.0-Setup.exe`.
- Android MVP artifact: `android\app\build\outputs\apk\debug\app-debug.apk`.
- Include the current `CHANGELOG.md` notes with the build.
