#!/usr/bin/env python3
"""Audit and package an unsigned iOS bundle for a sideloading tool to re-sign."""
import argparse
import hashlib
from pathlib import Path
import plistlib
import subprocess
import sys
import zipfile


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    app = args.app.resolve()
    root = Path(__file__).resolve().parents[2]
    subprocess.run([sys.executable, str(Path(__file__).with_name('audit.py')), str(app)], check=True)
    info = plistlib.loads((app / 'Info.plist').read_bytes())
    executable = info['CFBundleExecutable']
    allowed_roots = {'Info.plist', executable, 'extras.pak', 'Assets.car',
                     'AppIcon60x60@2x.png', 'AppIcon76x76@2x~ipad.png'}
    allowed_directories = {'shaders', 'fonts', 'licenses', 'voice', 'Frameworks'}
    voice = {'ggml-base.en-q5_1.bin', 'ggml-silero-v6.2.0.bin'}
    files = []
    for path in sorted(app.rglob('*')):
        if path.is_symlink():
            raise ValueError(f'Unexpected symlink: {path}')
        if not path.is_file():
            continue
        relative = path.relative_to(app)
        if path.suffix == '.d' and relative.parts[0] == 'shaders':
            continue  # Compiler dependency files include host paths and are not runtime resources.
        if (len(relative.parts) == 1 and path.name not in allowed_roots) or (
                len(relative.parts) > 1 and relative.parts[0] not in allowed_directories):
            raise ValueError(f'Unexpected bundle resource: {relative}')
        if path.suffix.lower() in {'.pkg', '.psarc', '.qar', '.mobileprovision', '.p12', '.pfx', '.key'} or path.name.lower() == 'pathid_list_ps4.bin':
            raise ValueError(f'Game or signing file in bundle: {relative}')
        if relative.parts[0] == 'voice' and relative.as_posix() not in {'voice/' + name for name in voice}:
            raise ValueError(f'Unexpected voice resource: {relative}')
        if relative.parts[0] == 'Frameworks' and relative.as_posix() not in {
                'Frameworks/MoltenVK.framework/MoltenVK', 'Frameworks/MoltenVK.framework/Info.plist'}:
            raise ValueError(f'Unexpected framework resource: {relative}')
        files.append((path, relative))
    if digest(app / 'extras.pak') != digest(root / 'assets/extras.pak'):
        raise ValueError('extras.pak differs from the repository resource')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path, relative in files:
            archive.write(path, 'Payload/pt.app/' + relative.as_posix())
    with zipfile.ZipFile(args.output) as archive:
        if archive.testzip() is not None:
            raise ValueError('IPA CRC validation failed')
        if not (archive.getinfo('Payload/pt.app/' + executable).external_attr >> 16) & 0o111:
            raise ValueError('Executable permission missing in IPA')
    checksum = f'{digest(args.output)}  {args.output.name}\n'
    args.output.with_suffix('.ipa.sha256').write_text(checksum)
    print(f'Packaged {len(files)} files; version {info["CFBundleShortVersionString"]}, build {info["CFBundleVersion"]}')
    print(checksum, end='')


if __name__ == '__main__':
    main()
