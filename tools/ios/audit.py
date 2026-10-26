#!/usr/bin/env python3
"""Audit an unsigned iOS device bundle without installing or changing it."""
import argparse
import json
from pathlib import Path
import plistlib
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('app', type=Path)
a = p.parse_args()
app = a.app.resolve()
info = plistlib.loads((app / 'Info.plist').read_bytes())
assert info['NSMicrophoneUsageDescription']
assert info['UIFileSharingEnabled'] and info['LSSupportsOpeningDocumentsInPlace']
assert set(info['UIDeviceFamily']) == {1, 2}
assert info['CFBundleIcons']['CFBundlePrimaryIcon']['CFBundleIconName'] == 'AppIcon'
assert info['CFBundleIcons~ipad']['CFBundlePrimaryIcon']['CFBundleIconName'] == 'AppIcon'
required = ['Assets.car', 'pt', 'extras.pak', 'Frameworks/MoltenVK.framework/MoltenVK',
            'voice/ggml-base.en-q5_1.bin', 'voice/ggml-silero-v6.2.0.bin',
            'licenses/bcdecode-LICENSE.txt', 'licenses/MoltenVK-Apache-2.0.txt']
for name in required:
    assert (app / name).is_file(), name
assert list((app / 'shaders').glob('*.spv'))
assert list((app / 'fonts').glob('*.ttf'))
for file in app.rglob('*'):
    assert file.suffix.lower() not in {'.pkg', '.psarc', '.qar'}, file
    assert not file.name.startswith(('libwhisper', 'libggml')), file
for name in ['pt', 'Frameworks/MoltenVK.framework/MoltenVK']:
    binary = app / name
    arch = subprocess.check_output(['lipo', '-archs', str(binary)], text=True).strip()
    assert arch == 'arm64', (name, arch)
    load = subprocess.check_output(['otool', '-l', str(binary)], text=True)
    assert 'platform 2\n' in load, name
    deps = subprocess.check_output(['otool', '-L', str(binary)], text=True).splitlines()[1:]
    assert not any('/opt/homebrew/' in line or 'AppKit.framework' in line for line in deps), deps
undefined = subprocess.check_output(['nm', '-u', str(app / 'pt')], text=True).splitlines()
for forbidden in ['_fork', '_posix_spawn', '_posix_spawnp', '_system', '_popen']:
    assert not any(line.strip() == forbidden for line in undefined), forbidden
print(json.dumps({'app' : str(app), 'bundle_id': info['CFBundleIdentifier'],
                  'architecture': 'arm64', 'platform': 'iOS device',
                  'minimum_os': info['MinimumOSVersion'],
                  'files': sum(f.is_file() for f in app.rglob('*')),
                  'device_installed_or_tested': False}, indent=2))
