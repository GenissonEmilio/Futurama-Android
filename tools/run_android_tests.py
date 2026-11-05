"""Run the native regression suites on an authorized Android ARM64 device (no game data needed)."""
import argparse
import pathlib
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--serial')
    parser.add_argument('--build-dir', required=True, type=pathlib.Path)
    parser.add_argument('--libcxx', required=True, type=pathlib.Path)
    args = parser.parse_args()
    adb = [args.adb] + (['-s', args.serial] if args.serial else [])
    suites = ['fut_portable_tests', 'fut_SaveStorage_tests', 'fut_ResourceLifetime_tests',
              'fut_FilesystemSafety_tests', 'fut_TextureConversion_tests',
              'fut_android_gles_tests', 'fut_android_raster_tests']
    files = [args.build_dir / name for name in suites]
    files += [args.build_dir / 'libfuturama_android.so', args.libcxx]
    for file in files:
        if not file.is_file():
            parser.error(f'Missing build output: {file}')
    remote = '/data/local/tmp/futurama-tests'
    subprocess.run(adb + ['shell', 'mkdir', '-p', remote], check=True)
    subprocess.run(adb + ['push', *map(str, files), remote + '/'], check=True)
    for suite in suites:
        command = (f'cd {shlex.quote(remote)} && chmod 700 {suite} && '
                   f'TMPDIR={remote} LD_LIBRARY_PATH={remote} ./{suite}')
        subprocess.run(adb + ['shell', command], check=True, timeout=120)
    subprocess.run(adb + ['shell', f'cd {remote} && TMPDIR={remote} LD_LIBRARY_PATH={remote} ./fut_android_raster_tests --scaled'], check=True, timeout=120)
    print(f'All {len(suites) + 1} Android native suites passed.')


if __name__ == '__main__':
    main()
