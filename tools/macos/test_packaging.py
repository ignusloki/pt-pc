"""Assets-free archive, payload, architecture and signing regressions on any host."""
import hashlib
import importlib.util
import json
import plistlib
import struct
import subprocess
import sys
import tempfile
import unittest
import wave
import zipfile
import zlib
from pathlib import Path
from unittest.mock import patch
import bundle as mac
import make_app
import build as mac_build

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
spec=importlib.util.spec_from_file_location('prepare_installer',REPO/'tools/prepare_native_installer.py')
prepare=importlib.util.module_from_spec(spec);spec.loader.exec_module(prepare)

class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='pt-packaging-');self.root=Path(self.temp.name)
        self.build=self.root/'build';self.source=self.root/'source';self.extractor=self.root/'extractor'
        for folder in (self.build/'shaders',self.build/'voice/licenses',self.build/'fonts',self.build/'generated',self.source,self.extractor):folder.mkdir(parents=True)
        self.magic=b'\xcf\xfa\xed\xfe'+struct.pack('<I',0x0100000C)
        for name in ('pt_release','libMoltenVK.dylib'):(self.build/name).write_bytes(self.magic+b'fixture')
        (self.build/'generated/pt_version.h').write_text('#define PT_VERSION "1.2.3"')
        for name in make_app.VOICE_MODELS+make_app.VOICE_LIBRARIES:
            (self.build/'voice'/name).write_bytes(name.encode() if name in make_app.VOICE_MODELS else self.magic)
        (self.build/'voice/libggml-cpu-fixture.so').write_bytes(self.magic)
        (self.build/'voice/licenses/MIT.txt').write_text('Fixture license')
        (self.build/'fonts/font.ttf').write_bytes(b'font fixture')
        (self.build/'extras.pak').write_bytes(b'Fixture packed bonus content')
        (self.build/'licenses/enhanced-textures').mkdir(parents=True)
        (self.build/'licenses/enhanced-textures/notice.txt').write_text('Fixture texture tool license')
        (self.build/'licenses/bc7enc.txt').write_text('Retired encoder license')
        (self.build/'licenses/Real-ESRGAN-fixture.txt').write_text('Retired model license')
        (self.build/'texture-tools').mkdir()
        (self.build/'texture-tools/retired-tool').write_bytes(b'old build cache')
        for shader in (REPO/'shaders').iterdir():
            if shader.suffix in {'.vert','.frag','.comp'}:(self.build/'shaders'/f'{shader.name}.spv').write_bytes(b'SPIR-V fixture')
        (self.source/'LICENSE.txt').write_text('Fixture license')
        (self.extractor/'PT.PkgExtract').write_bytes(self.magic)
    def tearDown(self):self.temp.cleanup()
    def portable(self, extractor=False):
        out=self.root/'out'
        def ditto(args,**kwargs):
            app=Path(args[-2]);archive=Path(args[-1])
            with zipfile.ZipFile(archive,'w') as z:
                for file in app.rglob('*'):
                    if file.is_file():z.write(file,file.relative_to(app.parent))
                z.writestr('__MACOSX/._P.T. PC Port.app',b'metadata')
        args=['make_app.py','--build',str(self.build),'--exe',str(self.build/'pt_release'),'--out',str(out),'--game',str(self.root/'absent')]
        if extractor: args += ['--extractor',str(self.extractor)]
        with patch.object(make_app,'sign'),patch.object(make_app.subprocess,'run',side_effect=ditto),patch.object(sys,'argv',args):make_app.main()
        return out/'pt-port-1.2.3-macos-arm64.zip'
    def payload(self,archive,rid=None):
        out=self.root/'payload';prepare.prepare(archive,self.extractor,self.source,out,rid);return out
    def test_upstream_bundle_payload_roundtrip(self):
        archive=self.portable();out=self.payload(archive,'osx-arm64')
        raw=(out/'payload.bin').read_bytes();self.assertEqual(raw[:8],b'PTSETUP1')
        count=struct.unpack_from('<I',raw,8)[0];offset=12;files={}
        for _ in range(count):
            length=struct.unpack_from('<H',raw,offset)[0];offset+=2
            name=raw[offset:offset+length].decode();offset+=length
            size,packed=struct.unpack_from('<QQ',raw,offset);offset+=16
            digest=raw[offset:offset+64].decode();offset+=64
            data=zlib.decompress(raw[offset:offset+packed]);offset+=packed
            self.assertEqual(len(data),size);self.assertEqual(hashlib.sha256(data).hexdigest(),digest);files[name]=data
        self.assertEqual(offset,len(raw));prefix='P.T. PC Port.app/Contents/'
        self.assertEqual(files[prefix+'MacOS/pt'],self.magic+b'fixture')
        for name in make_app.VOICE_MODELS+make_app.VOICE_LIBRARIES+('libggml-cpu-fixture.so',):self.assertIn(prefix+'Resources/voice/'+name,files)
        self.assertIn('NSMicrophoneUsageDescription',plistlib.loads(files[prefix+'Info.plist']))
        self.assertEqual(plistlib.loads(files[prefix+'Info.plist'])['CFBundleIconFile'],mac.ICON_FILE)
        self.assertEqual(files[prefix+'Resources/'+mac.ICON_FILE],(REPO/'assets/macos'/mac.ICON_FILE).read_bytes())
        self.assertFalse(any(n.startswith('__MACOSX') for n in files))
        self.assertIn('extractor/PT.PkgExtract',files)
        self.assertEqual(files[prefix+'Resources/extras.pak'],b'Fixture packed bonus content')
        self.assertFalse(any('texture-tools/' in name or 'enhanced-textures/' in name or
                             name.endswith('bc7enc.txt') or 'Real-ESRGAN-' in name for name in files))
        self.assertEqual(files[prefix+'Resources/licenses/PT-MIT.txt'], (REPO/'LICENSE').read_bytes())
        self.assertIn('pt-files.json',files)
        with zipfile.ZipFile(out/'extractor-source.zip') as z:self.assertIn('-r osx-arm64',z.read('BUILD.txt').decode())
        self.assertEqual(set(files),set(json.loads((out/'payload-manifest.json').read_text())))
    def test_other_platform_layout_and_windows_validation_preserved(self):
        # Strict Windows builds are checked by validate_desktop.py. This fixture
        # checks the policy call and that only the archive wrapper is removed.
        for rid,name in [('win-x64','pt.exe'),('linux-x64','pt')]:
            archive=self.root/(rid+'.zip')
            with zipfile.ZipFile(archive,'w') as z:z.writestr('runtime/'+name,b'fixture')
            spec=importlib.util.spec_from_file_location('package',REPO/'tools/package.py');pkg=importlib.util.module_from_spec(spec);spec.loader.exec_module(pkg)
            with patch.dict(sys.modules,{'package':pkg}),patch.object(pkg,'validate_upscaler_archive') as validate:
                out=self.root/rid;prepare.prepare(archive,self.extractor,self.source,out,rid)
                self.assertEqual(validate.call_count,1 if rid=='win-x64' else 0)
            self.assertIn(name,json.loads((out/'payload-manifest.json').read_text()))
            with zipfile.ZipFile(out/'extractor-source.zip') as z:self.assertIn('-r '+rid,z.read('BUILD.txt').decode())

    def test_portable_first_run_helper_and_notices_are_bundled(self):
        notices=self.extractor.with_name(self.extractor.name+'-notices')
        notices.mkdir()
        (notices/'LICENSE-LibOrbisPkg.txt').write_text('LGPL fixture')
        (notices/'LibOrbisPkg-corresponding-source.zip').write_bytes(b'Source fixture')
        with zipfile.ZipFile(self.portable(extractor=True)) as z:
            prefix='P.T. PC Port.app/Contents/'
            self.assertEqual(z.read(prefix+'MacOS/extractor/PT.PkgExtract'),self.magic)
            self.assertEqual(z.read(prefix+'Resources/licenses/PT.PkgExtract/LibOrbisPkg-corresponding-source.zip'),b'Source fixture')

    def test_first_run_helper_without_corresponding_source_is_refused(self):
        with self.assertRaisesRegex(SystemExit,'lacks the LibOrbisPkg licence or corresponding source'):
            self.portable(extractor=True)

    def test_managed_dlls_in_the_app_code_folder_are_refused(self):
        (self.extractor/'LibOrbisPkg.Core.dll').write_bytes(b'Managed DLL fixture')
        with self.assertRaisesRegex(SystemExit,'must be a single-file publish'):
            self.portable(extractor=True)
    def test_wrong_runtime_architecture_refused(self):
        with self.assertRaisesRegex(RuntimeError,'architecture mismatch'):self.payload(self.portable(),'osx-x64')
    def test_unsafe_duplicates_assets_and_data_refused(self):
        cases=[('runtime/../escape',),('runtime/C:/escape',),('runtime/a','other/a'),('runtime/A','runtime/a'),('runtime/P.T.app/Contents/Resources/texture.qar',),('runtime/P.T.app/Contents/Resources/CUSA01127/a',),('runtime/data/save.dat',)]
        for names in cases:
            with self.subTest(names=names):
                archive=self.root/'bad.zip'
                with zipfile.ZipFile(archive,'w') as z:
                    for name in names:z.writestr(name,b'fixture')
                with self.assertRaises(RuntimeError):self.payload(archive)
    def test_incomplete_build_refused(self):
        (self.build/'voice/libggml-cpu-fixture.so').unlink()
        with self.assertRaisesRegex(RuntimeError,'Missing build output'):make_app.validate_build(self.build,self.build/'pt_release')
    def test_static_moltenvk_refused(self):
        (self.build/'libMoltenVK.dylib').write_bytes(b'!<arch>\n')
        with self.assertRaisesRegex(RuntimeError,'Expected a Mach-O'):make_app.validate_build(self.build,self.build/'pt_release')
    def test_intel_archive_name_uses_target_cpu(self):
        (self.build/'pt_release').write_bytes(b'\xcf\xfa\xed\xfe'+struct.pack('<I',0x01000007))
        self.assertEqual(make_app.architecture(self.build/'pt_release'),'x64')
    def macho_bundle(self):
        app = self.root / "audit/P.T..app"
        (app / "Contents/MacOS").mkdir(parents=True)
        (app / "Contents/Frameworks").mkdir()
        (app / "Contents/MacOS/pt").write_bytes(self.magic + b"game")
        (app / "Contents/Frameworks/libMoltenVK.dylib").write_bytes(self.magic + b"library")
        return app

    def otool(self, dependencies):
        def output(args, **kwargs):
            file = Path(args[-1])
            header = str(file) + ":\n"
            self.assertEqual(args[1:3], ["-arch", "arm64"])
            if args[-2] == "-l":
                return header + "Load command 0\n          cmd LC_RPATH\n      cmdsize 48\n         path @executable_path/../Frameworks (offset 12)\n"
            if args[-2] == "-D":
                return header + ("@rpath/" + file.name + "\n" if file.suffix == ".dylib" else "")
            if args[-2] == "-L":
                names = ["@rpath/" + file.name] if file.suffix == ".dylib" else []
                names += dependencies.get(file.name, ["/usr/lib/libSystem.B.dylib"])
                return header + "".join(f"\t{name} (compatibility version 1.0.0, current version 1.0.0)\n" for name in names)
            raise AssertionError(args)
        return output

    def test_macho_dependencies_resolved_inside_distribution(self):
        app = self.macho_bundle()
        with patch.object(mac, "run") as run, patch.object(mac.subprocess, "check_output", side_effect=self.otool({"pt": ["@rpath/libMoltenVK.dylib"]})):
            mac.audit_dependencies(app)
        self.assertEqual(run.call_count, 2)
        for call in run.call_args_list:
            self.assertEqual(call.args[0], "lipo")
            self.assertEqual(call.args[-2:], ("-verify_arch", "arm64"))

    def test_missing_and_developer_machine_dependencies_refused(self):
        app = self.macho_bundle()
        for dependency in ("@rpath/missing.dylib", "@loader_path/missing.dylib", "/opt/homebrew/lib/library.dylib"):
            with self.subTest(dependency=dependency), patch.object(mac, "run"), patch.object(mac.subprocess, "check_output", side_effect=self.otool({"pt": [dependency]})):
                with self.assertRaisesRegex(RuntimeError, "Unbundled Mach-O dependency"):
                    mac.audit_dependencies(app)

    def test_extractor_rpath_without_a_trailing_slash(self):
        (self.extractor / "libfixture.dylib").write_bytes(self.magic + b"library")
        standard = self.otool({"PT.PkgExtract": ["@rpath/libfixture.dylib"]})
        for path in ("@executable_path", "@loader_path"):
            with self.subTest(path=path):
                def output(args, **kwargs):
                    return standard(args, **kwargs).replace("@executable_path/../Frameworks", path)
                with patch.object(mac, "run"), patch.object(mac.subprocess, "check_output", side_effect=output):
                    mac.audit_dependencies(self.extractor)

    def test_wrong_architecture_prevents_signing(self):
        app = self.macho_bundle()
        def reject(*args):
            self.assertEqual(args[0], "lipo")
            self.assertEqual(args[-2:], ("-verify_arch", "arm64"))
            raise subprocess.CalledProcessError(1, args)
        with patch.object(mac, "run", side_effect=reject), patch.object(mac.subprocess, "check_output") as output:
            with self.assertRaises(subprocess.CalledProcessError):
                mac.sign_tree(app)
        output.assert_not_called()

    def test_newer_macos_dependency_refused(self):
        app = self.macho_bundle()
        original = self.otool({})
        def output(args, **kwargs):
            result = original(args, **kwargs)
            if args[-2] == "-l":
                result += "cmd LC_BUILD_VERSION\ncmdsize 32\nplatform 1\nminos 26.0\nsdk 26.0\n"
            return result
        with patch.object(mac, "run"), patch.object(mac.subprocess, "check_output", side_effect=output):
            with self.assertRaisesRegex(RuntimeError, "above the app minimum"):
                mac.audit_dependencies(app)

    def test_packaged_voice_smoke_checks_that_audio_was_processed(self):
        app = self.macho_bundle()
        def launch(*args):
            self.assertEqual(args[0], app / "Contents/MacOS/pt")
            self.assertEqual(args[1], "--voice-test")
            with wave.open(str(args[2]), "rb") as audio:
                self.assertEqual((audio.getnchannels(), audio.getsampwidth(), audio.getframerate(), audio.getnframes()), (1, 2, 16000, 48000))
            Path(args[-1]).write_text("voice test: file voice-smoke.wav | 0 detections")
        with patch.object(mac_build, "run", side_effect=launch):
            mac_build.verify_packaged_voice(app, self.root)
        with patch.object(mac_build, "run", side_effect=lambda *args: Path(args[-1]).write_text("cannot load WAV")):
            with self.assertRaisesRegex(RuntimeError, "did not process the WAV"):
                mac_build.verify_packaged_voice(app, self.root)

    def test_runtime_notices_match_the_published_runtime(self):
        artifacts = self.root / "dotnet-artifacts"
        (artifacts / "obj/Extractor").mkdir(parents=True)
        packages = self.root / "packages"
        pack = packages / "microsoft.netcore.app.runtime.osx-arm64/10.0.1"
        pack.mkdir(parents=True)
        for name in ("LICENSE.TXT", "THIRD-PARTY-NOTICES.TXT"):
            (pack / name).write_text("Microsoft fixture " + name)
        (artifacts / "obj/Extractor/project.assets.json").write_text(json.dumps({"packageFolders": {str(packages): {}}}))
        (self.extractor / "PT.PkgExtract.runtimeconfig.json").write_text(json.dumps({"runtimeOptions": {"includedFrameworks": [{"name": "Microsoft.NETCore.App", "version": "10.0.1"}]}}))
        mac_build.copy_runtime_notices(self.extractor, artifacts, "osx-arm64")
        for name in ("LICENSE.TXT", "THIRD-PARTY-NOTICES.TXT"):
            self.assertEqual((self.extractor / "licenses" / ("dotnet-" + name)).read_bytes(), (pack / name).read_bytes())
        with self.assertRaisesRegex(RuntimeError, "runtime pack not found"):
            mac_build.copy_runtime_notices(self.extractor, artifacts, "osx-x64")

    def test_entitlements_kept_when_signing_the_app(self):
        app = self.macho_bundle()
        entitlements = HERE / "game-entitlements.plist"
        self.assertTrue(plistlib.loads(entitlements.read_bytes())["com.apple.security.device.audio-input"])
        with patch.object(mac, "audit_dependencies"), patch.object(mac, "run") as run:
            mac.sign_tree(app, "Developer ID fixture", entitlements)
        signatures = [call.args for call in run.call_args_list if "--sign" in call.args]
        for args in signatures:
            self.assertIn("runtime", args)
            if args[-1] == app or Path(args[-1]).name == "pt":
                self.assertIn(entitlements, args)
            else:
                self.assertNotIn("--entitlements", args)
        with patch.object(mac, "audit_dependencies"), patch.object(mac, "run") as run:
            mac.sign_tree(self.extractor, "Developer ID fixture")
        self.assertIn(HERE / "extractor-entitlements.plist", run.call_args.args)


    def test_patched_source_is_fresh_and_original_is_preserved(self):
        original = self.root / "upstream"
        originals = {
            "LibOrbisPkg.Core/LibOrbisPkg.Core.csproj": "<TargetFramework>netcoreapp3.0</TargetFramework>",
            "LibOrbisPkg/PFS/PFSCReader.cs": "ds.Read(output, 0, hdr.BlockSz);",
            "LibOrbisPkg/Util/MemoryMapped.cs": "reader.Read((long)chunks[chunkIdx++]*chunkSize + offsetIntoChunk, buf, offset, count);",
        }
        for name, value in originals.items():
            file = original / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(value)
        destination = self.root / "patched"
        mac_build.prepare_source(original, destination)
        self.assertIn("ReadExactly", (destination / "LibOrbisPkg/PFS/PFSCReader.cs").read_text())
        self.assertIn("toReadFromChunk", (destination / "LibOrbisPkg/Util/MemoryMapped.cs").read_text())
        self.assertIn("net10.0", (destination / "LibOrbisPkg.Core/LibOrbisPkg.Core.csproj").read_text())
        for name, value in originals.items():
            self.assertEqual((original / name).read_text(), value)
        with self.assertRaises(FileExistsError):
            mac_build.prepare_source(original, destination)
        with self.assertRaisesRegex(RuntimeError, "must not overlap"):
            mac_build.prepare_source(original, original / "nested")


if __name__ == "__main__":
    unittest.main(verbosity=2)
