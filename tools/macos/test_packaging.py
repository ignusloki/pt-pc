"""Synthetic packaging checks; runnable on Windows without a Mac SDK or game data."""
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
import build as mac_build

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
spec = importlib.util.spec_from_file_location("mac_package", HERE / "package.py")
mac = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mac)


class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="pt-mac-packaging-")
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.source = self.root / "source"
        self.extractor = self.root / "extractor"
        for folder in (self.build / "shaders", self.build / "voice/licenses", self.build / "fonts", self.source, self.extractor):
            folder.mkdir(parents=True)
        (self.source / "LICENSE.txt").write_text("Synthetic LGPL fixture")
        self.magic = b"\xcf\xfa\xed\xfe"
        (self.extractor / "PT.PkgExtract").write_bytes(self.magic + b"synthetic extractor")
        (self.build / "pt_release").write_bytes(self.magic + b"synthetic executable")
        (self.build / "pt_setup_macos").write_bytes(self.magic + b"synthetic installer")
        (self.build / "libMoltenVK.dylib").write_bytes(self.magic + b"synthetic graphics library")
        for name in mac.MODELS + mac.VOICE_LIBS:
            (self.build / "voice" / name).write_bytes(self.magic + name.encode() if name in mac.VOICE_LIBS else name.encode())
        (self.build / "voice/libggml-cpu-fixture.so").write_bytes(self.magic + b"CPU module")
        (self.build / "voice/licenses/MIT.txt").write_text("Synthetic license")
        (self.build / "fonts/font.ttf").write_bytes(b"font fixture")
        for shader in (REPO / "shaders").iterdir():
            if shader.suffix in {".vert", ".frag", ".comp"}:
                (self.build / "shaders" / f"{shader.name}.spv").write_bytes(b"SPIR-V fixture")

    def tearDown(self):
        self.temp.cleanup()

    def payload(self, archive):
        output = self.root / "payload"
        return subprocess.run([sys.executable, str(REPO / "tools/prepare_native_installer.py"), "--runtime", str(archive),
                               "--extractor", str(self.extractor), "--source", str(self.source), "--out", str(output),
                               "--runtime-id", "osx-arm64"], capture_output=True, text=True), output

    def test_bundle_and_payload_roundtrip(self):
        with patch.object(mac, "relocate_voice"), patch.object(mac, "sign_tree"):
            archive = mac.runtime(self.build, self.root / "release", "1.2.3")
        with zipfile.ZipFile(archive) as z:
            prefix = "pt-port-macos-arm64/P.T..app/Contents/"
            self.assertIn(prefix + "MacOS/pt", z.namelist())
            self.assertIn(prefix + "Frameworks/libMoltenVK.dylib", z.namelist())
            for name in mac.MODELS + mac.VOICE_LIBS + ("libggml-cpu-fixture.so",):
                self.assertIn(prefix + "Resources/voice/" + name, z.namelist())
            info = plistlib.loads(z.read(prefix + "Info.plist"))
            self.assertEqual(info["CFBundleExecutable"], "pt")
            self.assertIn("NSMicrophoneUsageDescription", info)
            self.assertEqual(info["CFBundleIconFile"], mac.ICON_FILE)
            self.assertEqual(z.read(prefix + "Resources/" + mac.ICON_FILE), mac.ICON_SOURCE.read_bytes())
            self.assertTrue((z.getinfo(prefix + "MacOS/pt").external_attr >> 16) & 0o111)
        result, output = self.payload(archive)
        self.assertEqual(result.returncode, 0, result.stderr)
        data = memoryview((output / "payload.bin").read_bytes())
        self.assertEqual(data[:8], b"PTSETUP1")
        count = struct.unpack_from("<I", data, 8)[0]
        offset = 12
        files = {}
        for _ in range(count):
            length = struct.unpack_from("<H", data, offset)[0]
            offset += 2
            name = bytes(data[offset:offset + length]).decode()
            offset += length
            size, packed = struct.unpack_from("<QQ", data, offset)
            offset += 16
            digest = bytes(data[offset:offset + 64]).decode()
            offset += 64
            raw = zlib.decompress(data[offset:offset + packed])
            offset += packed
            self.assertEqual(len(raw), size)
            self.assertEqual(hashlib.sha256(raw).hexdigest(), digest)
            files[name] = raw
        self.assertEqual(offset, len(data))
        self.assertEqual(files["P.T..app/Contents/MacOS/pt"], self.magic + b"synthetic executable")
        self.assertEqual(files["P.T..app/Contents/Resources/" + mac.ICON_FILE], mac.ICON_SOURCE.read_bytes())
        self.assertIn("extractor/PT.PkgExtract", files)
        sourcezip = output / "extractor-source.zip"
        with zipfile.ZipFile(sourcezip) as z:
            self.assertIn("-r osx-arm64", z.read("BUILD.txt").decode())
        manifest = json.loads((output / "payload-manifest.json").read_text())
        self.assertEqual(set(manifest), set(files))

    def test_nested_game_assets_rejected(self):
        for name in ("P.T..app/Contents/Resources/CUSA01127/a", "P.T..app/Contents/Resources/texture.qar"):
            with self.subTest(name=name):
                archive = self.root / "bad.zip"
                with zipfile.ZipFile(archive, "w") as z:
                    z.writestr("runtime/" + name, "game fixture")
                result, _ = self.payload(archive)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Game assets in runtime ZIP", result.stderr)

    def test_unsafe_and_duplicate_paths_rejected(self):
        for names in (("runtime/../escape",), ("runtime/C:/escape",), ("runtime/a", "other/a")):
            with self.subTest(names=names):
                archive = self.root / "unsafe.zip"
                with zipfile.ZipFile(archive, "w") as z:
                    for name in names:
                        z.writestr(name, "fixture")
                result, _ = self.payload(archive)
                self.assertNotEqual(result.returncode, 0)

    def test_incomplete_runtime_refused_before_bundle_creation(self):
        (self.build / "voice/libggml-cpu-fixture.so").unlink()
        with self.assertRaisesRegex(RuntimeError, "Missing build output"):
            mac.runtime(self.build, self.root / "incomplete", "1.0.0")
        self.assertFalse((self.root / "incomplete/pt-port-macos-arm64").exists())

    def test_existing_release_preserved(self):
        output = self.root / "existing"
        (output / "pt-port-macos-arm64").mkdir(parents=True)
        marker = output / "pt-port-macos-arm64/keep.txt"
        marker.write_text("preserve me")
        with self.assertRaisesRegex(RuntimeError, "Output already exists"):
            mac.runtime(self.build, output, "1.0.0")
        self.assertEqual(marker.read_text(), "preserve me")

    def test_static_moltenvk_refused_before_bundle_creation(self):
        (self.build / "libMoltenVK.dylib").write_bytes(b"!<arch>\nstatic archive")
        with self.assertRaisesRegex(RuntimeError, "Expected a Mach-O"):
            mac.runtime(self.build, self.root / "invalid", "1.0.0")
        self.assertFalse((self.root / "invalid/pt-port-macos-arm64").exists())

    def macho_bundle(self):
        app = self.root / "audit/P.T..app"
        mac.make_bundle(app, mac.info("P.T.", "pt", "org.pt-port.game", "1.0.0"))
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
            self.assertEqual(call.args[:3], ("lipo", "-verify_arch", "arm64"))

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
            self.assertEqual(args[:3], ("lipo", "-verify_arch", "arm64"))
            raise subprocess.CalledProcessError(1, args)
        with patch.object(mac, "run", side_effect=reject), patch.object(mac.subprocess, "check_output") as output:
            with self.assertRaises(subprocess.CalledProcessError):
                mac.sign_tree(app)
        output.assert_not_called()

    def test_packaged_voice_smoke_checks_that_audio_was_processed(self):
        app = self.macho_bundle()
        def launch(*args):
            self.assertEqual(args[0], app / "Contents/MacOS/pt")
            self.assertEqual(args[1], "--voice-test")
            with wave.open(str(args[2]), "rb") as audio:
                self.assertEqual((audio.getnchannels(), audio.getsampwidth(), audio.getframerate(), audio.getnframes()), (1, 2, 16000, 16000))
            Path(args[-1]).write_text("voice test: file voice-smoke.wav | 0 detections")
        with patch.object(mac_build, "run", side_effect=launch):
            mac_build.verify_packaged_voice(app, self.root)
        with patch.object(mac_build, "run", side_effect=lambda *args: Path(args[-1]).write_text("cannot load WAV")):
            with self.assertRaisesRegex(RuntimeError, "did not process the WAV"):
                mac_build.verify_packaged_voice(app, self.root)

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

    def test_voice_module_relocation_has_no_install_id(self):
        voice = self.build / "voice"
        def output(args, **kwargs):
            library = Path(args[-1])
            self.assertEqual(args[1:3], ["-arch", "arm64"])
            if args[-2] == "-D":
                return str(library) + ":\n" + ("@rpath/" + library.name + "\n" if library.name != "libggml-cpu.dylib" else "")
            return str(library) + ":\n\t@rpath/libggml-base.dylib (compatibility version 1.0.0)\n"
        with patch.object(mac.subprocess, "check_output", side_effect=output), patch.object(mac, "run") as run:
            mac.relocate_voice(voice)
        self.assertFalse(any(call.args[1] == "-id" and Path(call.args[-1]).name == "libggml-cpu.dylib" for call in run.call_args_list))
        self.assertTrue(any("@loader_path/libggml-base.dylib" in call.args for call in run.call_args_list))

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
