"""Stage 0 spike of the DirectX 12 plan: fetch the pinned shader tools and build the spike's helper programs (Windows).

  DXC            microsoft/DirectXShaderCompiler release v1.9.2609, dxc_2026_09_29.zip (dxc.exe, dxcompiler.dll, dxil.dll), SHA-256 pinned
  SPIRV-Cross    KhronosGroup/SPIRV-Cross commit 6c09849f (tag vulkan-sdk-1.4.357.0, the one shipped with the Vulkan SDK 1.4.357.0 that
                 builds the game's SPIR-V), source zip SHA-256 pinned, built here with MSVC and Ninja as static libraries

Then tools/dx12/build_tools.bat builds spirv_to_hlsl.exe (SPIRV-Cross C++ API driver) and feature_probe.exe.

usage: python tools/dx12/fetch_tools.py --out <dir>
Result: <dir>/dxc/bin/x64, <dir>/spirv-cross-build, <dir>/tools/{spirv_to_hlsl,feature_probe}.exe
"""
import argparse
import hashlib
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
DXC_URL = "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip"
DXC_SHA256 = "ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1"
SC_COMMIT = "6c09849fe88c48eaed08413aa022aaa136a3a057"
SC_URL = f"https://github.com/KhronosGroup/SPIRV-Cross/archive/{SC_COMMIT}.zip"
SC_SHA256 = "6114250b398aa30d3f77840e868b1dcdf2537ecc3f70ed0a754f0907e8889d49"
VCVARS = r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"


def download(url, sha256, path: Path):
    if not path.exists():
        urllib.request.urlretrieve(url, path)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != sha256:
        sys.exit(f"{path.name}: SHA-256 {digest} is not the pinned {sha256}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=Path, required=True)
    out = ap.parse_args().out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    download(DXC_URL, DXC_SHA256, out / "dxc.zip")
    with zipfile.ZipFile(out / "dxc.zip") as z:
        z.extractall(out / "dxc")
    download(SC_URL, SC_SHA256, out / "spirv-cross.zip")
    with zipfile.ZipFile(out / "spirv-cross.zip") as z:
        z.extractall(out / "spirv-cross-src")
    src = out / "spirv-cross-src" / f"SPIRV-Cross-{SC_COMMIT}"
    build = out / "spirv-cross-build"
    tools = out / "tools"
    script = (f'call "{VCVARS}" >nul || exit /b 1\n'
              f'cmake -S "{src}" -B "{build}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSPIRV_CROSS_ENABLE_TESTS=OFF -DSPIRV_CROSS_SHARED=OFF '
              f'-DSPIRV_CROSS_STATIC=ON -DSPIRV_CROSS_CLI=ON || exit /b 2\n'
              f'cmake --build "{build}" --parallel 4 || exit /b 3\n'
              f'call "{REPO / "tools" / "dx12" / "build_tools.bat"}" "{src}" "{build}" "{tools}" || exit /b 4\n')
    bat = out / "build_all.bat"
    bat.write_text(script, encoding="utf-8")
    sys.exit(subprocess.run(["cmd", "/c", str(bat)]).returncode)


if __name__ == "__main__":
    main()
