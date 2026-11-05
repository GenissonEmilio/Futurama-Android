"""Compile and run host-only touch gesture regressions with JDK 17 or newer."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    java_home = os.environ.get('JAVA_HOME')
    parser.add_argument('--javac', default=str(Path(java_home) / 'bin' / 'javac') if java_home else 'javac')
    parser.add_argument('--java', default=str(Path(java_home) / 'bin' / 'java') if java_home else 'java')
    parser.add_argument('--build-dir', type=Path, default=root / 'build' / 'touch-tests')
    args = parser.parse_args()
    args.build_dir.mkdir(parents=True, exist_ok=True)
    package = Path('com/genisson/futuramarecomp')
    sources = [root / 'android/app/src/main/java' / package / 'TouchControlModel.java',
               root / 'tests/java' / package / 'TouchControlModelTests.java']
    subprocess.run([args.javac, '--release', '17', '-encoding', 'UTF-8', '-d', str(args.build_dir), *map(str, sources)],
                   check=True, timeout=60)
    subprocess.run([args.java, '-cp', str(args.build_dir),
                    'com.genisson.futuramarecomp.TouchControlModelTests'], check=True, timeout=60)


if __name__ == '__main__':
    main()
