"""Verify the Android package structure without requiring game data."""
import pathlib
import sys
import zipfile


def main():
    with zipfile.ZipFile(sys.argv[1]) as apk:
        names = set(apk.namelist())
        required = {
            "AndroidManifest.xml",
            "classes.dex",
            "lib/arm64-v8a/libfuturama_android.so",
            "lib/arm64-v8a/libc++_shared.so",
        }
        missing = required - names
        if missing:
            raise SystemExit(f"Missing package entries: {sorted(missing)}")
        forbidden = {".iso", ".7z", ".xbe", ".img", ".xmv"}
        game_data = [name for name in names if pathlib.PurePosixPath(name).suffix.lower() in forbidden]
        if game_data:
            raise SystemExit(f"Disc data must not be packaged: {game_data}")
        print("APK checks passed: ARM64 runtime present, no disc data bundled")


if __name__ == "__main__":
    main()
