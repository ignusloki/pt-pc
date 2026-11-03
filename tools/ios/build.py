#!/usr/bin/env python3
"""Configure/build an isolated iOS app; never installs or signs it."""
import argparse
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, default=root / 'build/ios')
p.add_argument('--source-cache', type=Path)
p.add_argument('--moltenvk', required=True, type=Path)
p.add_argument('--bundle-id', default='org.ignusloki.pt.mobile')
p.add_argument('--decoder-source', type=Path)
p.add_argument('--xcode', action='store_true', help='Generate an Xcode project for Personal Team signing')
p.add_argument('--configure-only', action='store_true')
p.add_argument('--configuration', choices=['Debug', 'Release', 'RelWithDebInfo'], default='Debug')
p.add_argument('--build-number', help='Override the numeric iOS bundle build number')
p.add_argument('--diagnostics', action='store_true', help='Record startup stages, memory, shader compilation and GPU messages')
a = p.parse_args()
b = a.build.resolve()
args = ['cmake', '-S', str(root), '-B', str(b), '-G', 'Xcode' if a.xcode else 'Ninja',
        '-DCMAKE_SYSTEM_NAME=iOS', '-DCMAKE_OSX_SYSROOT=iphoneos', '-DCMAKE_OSX_ARCHITECTURES=arm64',
        '-DCMAKE_OSX_DEPLOYMENT_TARGET=17.0', '-DCMAKE_BUILD_TYPE=' + a.configuration,
        '-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO',
        '-DPT_MOLTENVK_IOS=' + str(a.moltenvk.resolve()), '-DPT_OPENXR=OFF', '-DPT_IOS_BUNDLE_ID=' + a.bundle_id,
        '-DPT_IOS_DIAGNOSTICS=' + ('ON' if a.diagnostics else 'OFF')]
if a.build_number:
    args.append('-DPT_IOS_BUILD_NUMBER=' + a.build_number)
if a.decoder_source:
    args.append('-DFETCHCONTENT_SOURCE_DIR_BCDECODE=' + str(a.decoder_source.resolve()))
if a.source_cache:
    cache = a.source_cache.resolve()
    for src in sorted((cache / '_deps').glob('*-src')):
        args.append('-DFETCHCONTENT_SOURCE_DIR_' + src.name[:-4].upper() + '=' + str(src))
    for directory in ['voice', '_deps/ww2ogg']:
        source = cache / directory
        if source.exists():
            shutil.copytree(source, b / directory, dirs_exist_ok=True,
                            ignore=shutil.ignore_patterns('*.dylib', '*.so'))
subprocess.run(args, check=True)
if not a.configure_only:
    subprocess.run(['cmake', '--build', str(b), '--target', 'pt', '--config', a.configuration, '-j', '4'], check=True)
