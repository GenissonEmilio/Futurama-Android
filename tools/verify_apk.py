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
        forbidden = {".iso", ".7z", ".rar", ".xbe", ".img", ".xmv", ".jks", ".keystore",
                     ".key", ".p12", ".pfx", ".pk8", ".sav", ".save", ".log"}
        game_data = [name for name in names if pathlib.PurePosixPath(name).suffix.lower() in forbidden]
        if game_data:
            raise SystemExit(f"Disc data, private keys, saves and logs must not be packaged: {game_data}")
        private_paths = [name for name in names if pathlib.PurePosixPath(name).name == "local.properties"
                         or {part.lower() for part in pathlib.PurePosixPath(name).parts} & {"saves", "game-data"}]
        if private_paths:
            raise SystemExit(f"Private data must not be packaged: {private_paths}")
        # Release resource optimization can shorten file paths while retaining resource identifiers.
        if not any(name.endswith("/ic_launcher.xml") for name in names) and (
                "resources.arsc" not in names or b"ic_launcher" not in apk.read("resources.arsc")):
            raise SystemExit("Missing adaptive launcher icon")
        print("APK checks passed: ARM64 runtime and icon present; no forbidden disc/private data")


if __name__ == "__main__":
    main()
