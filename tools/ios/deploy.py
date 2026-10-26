#!/usr/bin/env python3
"""Install with an existing explicit development profile, preserving app identity/data."""
import datetime
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def main():
    env = os.environ
    for key in ['PT_PROFILE', 'PT_IDENTITY', 'PT_DEVICE']:
        if not env.get(key):
            raise SystemExit(f'{key} is required; see docs/mobile.md')
    source = Path(env.get('PT_APP', 'build/ios/pt.app')).resolve()
    info = plistlib.loads((source / 'Info.plist').read_bytes())
    bundle_id = info['CFBundleIdentifier']
    decoded = run('security', 'cms', '-D', '-i', env['PT_PROFILE'], capture_output=True).stdout
    profile = plistlib.loads(decoded)
    if profile['ExpirationDate'] <= datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None):
        raise SystemExit('Profile expired; renew through Xcode without uninstalling the app')
    entitlements = profile['Entitlements']
    app_id = entitlements['application-identifier'].split('.', 1)[1]
    if app_id != bundle_id:
        raise SystemExit(f'Profile must explicitly match {bundle_id}; no bundle-ID rewriting is performed')
    # Unique staging path; no deletion of build outputs, original archives or installed data.
    staging = Path(tempfile.mkdtemp(prefix='pt-ios-signed-'))
    app = staging / 'pt.app'
    shutil.copytree(source, app, symlinks=True)
    shutil.copyfile(env['PT_PROFILE'], app / 'embedded.mobileprovision')
    entitlement_file = staging / 'entitlements.plist'
    entitlement_file.write_bytes(plistlib.dumps(entitlements))
    for framework in sorted((app / 'Frameworks').glob('*.framework')):
        run('codesign', '--force', '--sign', env['PT_IDENTITY'], str(framework))
    run('codesign', '--force', '--sign', env['PT_IDENTITY'], '--entitlements', str(entitlement_file), str(app))
    run('codesign', '--verify', '--deep', '--strict', str(app))
    device = env['PT_DEVICE']
    run('xcrun', 'devicectl', 'device', 'install', 'app', '--device', device, str(app))
    domain = ['--device', device, '--domain-type', 'appDataContainer', '--domain-identifier', bundle_id]
    if env.get('PT_GAME'):
        # Inspect the whole Documents tree first. Fail closed on listing failures; never replace an import.
        listing = staging / 'documents.json'
        run('xcrun', 'devicectl', 'device', 'info', 'files', *domain,
            '--subdirectory', 'Documents', '--json-output', str(listing))
        data = listing.read_text()
        if any(name in data for name in ['chunk1.psarc', 'texture.qar', 'pathid_list_ps4.bin']):
            raise SystemExit('Game files already exist; skipping import to preserve the installation')
        game = Path(env['PT_GAME'])
        files = ['chunk1.psarc', 'texture.qar', 'pathid_list_ps4.bin']
        if not all((game / name).is_file() for name in files):
            raise SystemExit('PT_GAME must contain all three extracted game archives')
        for name in files:
            run('xcrun', 'devicectl', 'device', 'copy', 'to', *domain,
                '--source', str(game / name), '--destination', 'Documents/CUSA01127/' + name)
    args = ['xcrun', 'devicectl', 'device', 'process', 'launch', '--device', device, '--terminate-existing']
    if env.get('PT_LAUNCH_ENV'):
        json.loads(env['PT_LAUNCH_ENV'])
        args += ['--environment-variables', env['PT_LAUNCH_ENV']]
    run(*args, bundle_id)
    print(f'Signed staging bundle retained at {app}')


if __name__ == '__main__':
    main()
