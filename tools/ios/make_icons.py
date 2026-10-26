#!/usr/bin/env python3
"""Compile Mac artwork as iPhone/iPad icons, or merge actool metadata into an app."""
import argparse
from pathlib import Path
import plistlib
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path)
p.add_argument('--output', required=True, type=Path)
p.add_argument('--minimum-os', default='17.0')
p.add_argument('--bundle', type=Path)
a = p.parse_args()
output = a.output.resolve()
metadata = output / 'icon-info.plist'
if a.bundle:
    info_file = a.bundle / 'Info.plist'
    info = plistlib.loads(info_file.read_bytes())
    partial = plistlib.loads(metadata.read_bytes())
    assert 'CFBundleIcons' in partial and 'CFBundleIcons~ipad' in partial
    info.update(partial)
    info_file.write_bytes(plistlib.dumps(info))
else:
    if not a.source:
        p.error('--source is required for icon compilation')
    catalog = output / 'Icons.xcassets'
    compiled = output / 'compiled'
    compiled.mkdir(parents=True, exist_ok=True)
    subprocess.run(['xcrun', 'swift', str(Path(__file__).with_suffix('.swift')), str(a.source.resolve()), str(catalog)], check=True)
    subprocess.run(['xcrun', 'actool', str(catalog), '--compile', str(compiled), '--platform', 'iphoneos',
                    '--minimum-deployment-target', a.minimum_os, '--app-icon', 'AppIcon',
                    '--target-device', 'iphone', '--target-device', 'ipad',
                    '--output-partial-info-plist', str(metadata)], check=True)
