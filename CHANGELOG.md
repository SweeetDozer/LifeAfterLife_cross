# Changelog

## 0.1.0 - MVP

Initial MVP release candidate for Life After Life.

### Added

- Slint/C++ desktop app for viewing and editing relationship trees.
- Authentication flow with registration, login, logout, and persisted sessions.
- Backend-backed tree list, tree creation, rename, and deletion.
- Person create, edit, delete, drag, and local canvas position persistence.
- Relationship create/delete flow for parent, spouse, sibling, and friend links.
- Canvas pan, zoom, relationship hit testing, and auto-layout.
- English/Russian UI language toggle.
- Windows installer script through Inno Setup.
- Experimental Android Gradle wrapper around the shared CMake/Slint app.

### Known MVP Gaps

- Android build is experimental and targets `arm64-v8a`.
- Relationship type editing is not available yet.
- Production Android signing and store packaging are not configured.
- Photos, sharing/access management, and full offline sync are not included.
