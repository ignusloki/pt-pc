"""Assets-free Windows/Linux compilation, installer and extractor validation."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools/macos"))
from build import LIBORBIS_REVISION, TESTS, prepare_source


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build", type=Path, default=REPO / "build/desktop-validation")
    p.add_argument("--cmake-arg", action="append", default=[])
    a = p.parse_args()
    build = a.build.resolve()
    build.mkdir(parents=True)
    logs = build / "validation-logs"
    logs.mkdir()
    results = []

    def run(name, *command):
        print(name, flush=True)
        with (logs / (name + ".log")).open("w", encoding="utf-8") as output:
            result = subprocess.run([str(c) for c in command], cwd=REPO, stdout=output, stderr=subprocess.STDOUT)
        results.append({"name": name, "exit_code": result.returncode})
        (logs / "results.json").write_text(json.dumps(results, indent=2))
        if result.returncode:
            raise RuntimeError(f"{name} failed; see {logs / (name + '.log')}")

    windows = sys.platform == "win32"
    setup = "pt_setup" if windows else "pt_setup_linux"
    suffix = ".exe" if windows else ""
    # This payload enables the Windows resource/link step. It is a test fixture
    # only, never uploaded as an installer or used to install a game.
    fixture_payload = build / "compile-only-payload.bin"
    fixture_payload.write_bytes(b"PTSETUP1\0\0\0\0")
    compiler = ["-DCMAKE_C_COMPILER=clang-cl", "-DCMAKE_CXX_COMPILER=clang-cl"] if windows else []
    policy = ["-DPT_REQUIRE_UPSCALERS=ON", "-DPT_UPSCALERS=ON"] if windows else []
    run("configure", "cmake", "-G", "Ninja", "-S", REPO, "-B", build, "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
        f"-DPT_SETUP_PAYLOAD={fixture_payload.as_posix()}", *compiler, *policy, *a.cmake_arg)
    run("compile", "cmake", "--build", build, "--parallel", "4", "--target", "pt", "pt_release", setup, *TESTS)
    fixtures = build / "fixtures"
    fixtures.mkdir()
    arguments = {
        "pt_settings_roundtrip_test": [fixtures / "settings"], "pt_save_status_test": [fixtures / "save-status"],
        "pt_save_reset_test": [fixtures / "save-reset"], "pt_graphics_preset_test": [fixtures / "graphics.ini"],
        "pt_installer_update_test": [fixtures / "installer"], "pt_unicode_paths_test": [fixtures / "unicode"],
        "pt_user_data_test": [fixtures / "user-data"],
    }
    for test in TESTS:
        run(test, build / (test + suffix), *arguments.get(test, []))
    run("native-installer", build / (setup + suffix), "--self-test", logs / "native-installer.txt")
    run("python-packaging", sys.executable, REPO / "tools/macos/test_packaging.py")
    original = build / "liborbis-upstream"
    run("liborbis-clone", "git", "clone", "--no-checkout", "https://github.com/maxton/LibOrbisPkg.git", original)
    run("liborbis-revision", "git", "-C", original, "checkout", "--detach", LIBORBIS_REVISION)
    source = prepare_source(original, build / "liborbis-patched")
    artifacts = build / "dotnet-artifacts"
    run("extractor-build", "dotnet", "build", REPO / "installer/Tests/Tests.csproj", "-c", "Release",
        f"-p:LibOrbisSource={source}", "--artifacts-path", artifacts)
    run("extractor-fixtures", "dotnet", artifacts / "bin/Tests/release/Tests.dll", fixtures / "extractor")
    rid = "win-x64" if windows else "linux-x64"
    run("extractor-publish", "dotnet", "publish", REPO / "installer/Extractor/Extractor.csproj", "-c", "Release",
        "-r", rid, "--self-contained", "true", f"-p:LibOrbisSource={source}", "--artifacts-path", artifacts,
        "-o", build / "extractor")
    if windows:
        sys.path.insert(0, str(REPO / "tools"))
        from package import validate_upscaler_release
        validate_upscaler_release(build, build / "pt_release.exe")
    print("PASS assets-free desktop validation; GPU gameplay and real PKG installation require hardware testing.")


if __name__ == "__main__":
    main()
