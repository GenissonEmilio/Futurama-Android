# Futurama Android

- Preserve upstream history, credits, `orig:` annotations and the existing Windows target.
- Never commit or upload disc data, ISO/XBE/IMG/XMV files, saves, signing keys, local.properties or device logs containing personal information.
- Android platform code lives in `src/platform/android`, GLES in `src/render/gles`, Java/Gradle in `android`.
- Build with JDK17, SDK36, build-tools35.0.0, NDK30.0.16248370 and CMake3.31.6: `android/gradlew assembleDebug`.
- Run portable regressions after numeric/filesystem changes. Verify the APK with `python tools/verify_apk.py <apk>`.
- Distinguish compiled, launched, menu-tested and gameplay-tested in reports and `docs/ANDROID_STATUS.md`.
- Keep independent edits delegated when useful; do not have multiple agents mutate the same platform file or operate one device concurrently.
