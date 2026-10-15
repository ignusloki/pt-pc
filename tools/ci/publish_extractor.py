"""Publishes the PKG extraction helper (installer/Extractor, on the LGPL LibOrbisPkg) for one .NET runtime, from a private
patched copy of the LibOrbisPkg source, the way tools/ci/release.py makes it for the setups. The game's first-run screen runs
the same helper (src/game/first_run.h), so the portable zips and the macOS app carry it too.

    python tools/ci/publish_extractor.py --liborbis <LibOrbisPkg source> --rid osx-arm64 --out build/extractor --single-file

--out gets the helper (PT.PkgExtract or PT.PkgExtract.exe and, without --single-file, the runtime beside it); --notices (default
<out>-notices) gets LICENSE-LibOrbisPkg.txt and LibOrbisPkg-corresponding-source.zip, which ship with it. --single-file makes one
executable (the macOS app signs it as one piece of code in Contents/MacOS/extractor).
"""
import argparse
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
p.add_argument("--liborbis", type=Path, required=True, help="the LibOrbisPkg source (maxton/LibOrbisPkg, never changed here)")
p.add_argument("--rid", required=True, help="the .NET runtime: win-x64, linux-x64, osx-arm64 or osx-x64")
p.add_argument("--out", type=Path, required=True)
p.add_argument("--notices", type=Path)
p.add_argument("--single-file", action="store_true")
a = p.parse_args()
if not (a.liborbis / "LibOrbisPkg.Core" / "LibOrbisPkg.Core.csproj").is_file():
    sys.exit(f"{a.liborbis} is not the LibOrbisPkg source (LibOrbisPkg.Core/LibOrbisPkg.Core.csproj)")
out = a.out.resolve()
notices = (a.notices or out.with_name(out.name + "-notices")).resolve()
source = out.with_name(out.name + "-liborbis-src")
for folder in (out, notices, source):
    if folder.exists():
        shutil.rmtree(folder)
# the same changes as release.py's liborbis_source(): net10.0 and the read fixes (tools/patch_liborbis_readers.py)
shutil.copytree(a.liborbis, source, ignore=shutil.ignore_patterns("bin", "obj", ".git"))
csproj = source / "LibOrbisPkg.Core" / "LibOrbisPkg.Core.csproj"
csproj.write_text(csproj.read_text(encoding="utf-8-sig").replace("netcoreapp3.0", "net10.0").replace("<LangVersion>7.3</LangVersion>", "<LangVersion>latest</LangVersion>")
                  .replace("<GenerateSerializationAssemblies>Auto</GenerateSerializationAssemblies>", "<GenerateSerializationAssemblies>Off</GenerateSerializationAssemblies>"),
                  encoding="utf-8")
subprocess.run([sys.executable, str(REPO / "tools" / "patch_liborbis_readers.py"), str(source)], check=True)
command = ["dotnet", "publish", str(REPO / "installer" / "Extractor" / "Extractor.csproj"), "-c", "Release", "-r", a.rid, "--self-contained", "true",
           f"-p:LibOrbisSource={source}", "-o", str(out)]
if a.single_file:
    command += ["-p:PublishSingleFile=true", "-p:IncludeNativeLibrariesForSelfExtract=true"]
subprocess.run(command, check=True)
for pdb in out.rglob("*.pdb"):
    pdb.unlink()
helper = out / ("PT.PkgExtract.exe" if a.rid.startswith("win") else "PT.PkgExtract")
if not helper.is_file():
    sys.exit(f"{helper} was not published")
# the LGPL notices: the licence and the corresponding source, as prepare_native_installer.py puts them in the setups
notices.mkdir(parents=True)
shutil.copy2(a.liborbis / "LICENSE.txt", notices / "LICENSE-LibOrbisPkg.txt")
with zipfile.ZipFile(notices / "LibOrbisPkg-corresponding-source.zip", "w", zipfile.ZIP_DEFLATED) as z:
    for root, prefix in ((source, "LibOrbisPkg"), (REPO / "installer" / "Extractor", "PT.PkgExtract")):
        for file in sorted(root.rglob("*")):
            if file.is_file() and not {"bin", "obj", ".git"}.intersection(file.relative_to(root).parts):
                z.write(file, prefix + "/" + file.relative_to(root).as_posix())
    z.writestr("BUILD.txt", f"python tools/ci/publish_extractor.py --liborbis <LibOrbisPkg source> --rid {a.rid}"
                            f"{' --single-file' if a.single_file else ''}, which runs dotnet publish PT.PkgExtract/Extractor.csproj -c Release "
                            f"-r {a.rid} --self-contained true -p:LibOrbisSource=<this LibOrbisPkg folder>. Library and wrapper are LGPL-3.0-or-later. "
                            "Changes: net10.0/modern SDK, PFSC decompression uses ReadExactly and chunked reads use the current chunk length. "
                            "No original game assets in this archive.\n")
shutil.rmtree(source)
print(f"{helper}\n{notices}")
