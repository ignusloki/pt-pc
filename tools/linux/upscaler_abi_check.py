"""Static checks of the Linux build's upscaler binaries (docs/upscaling.md, Native Linux), runnable on Windows.

  python tools/linux/upscaler_abi_check.py [build/linux-cross]

With LLVM's llvm-readobj (C:/Program Files/LLVM/bin or PATH) it checks, for pt, libamd_fidelityfx_vk.so and the DLSS SDK's
libnvidia-ngx-dlss*.so next to it:
  - ELF x86-64 shared objects / executable, the libraries each one needs, and the newest GLIBC symbol version each asks for
    (pt's floor is glibc 2.38, docs/linux.md: nothing here may raise it);
  - libamd_fidelityfx_vk.so exports exactly the five FidelityFX API entry points the game looks up with dlsym, and needs
    nothing but libc, libm, the dynamic loader and the Vulkan loader;
  - pt has NGX's loader linked in (NVSDK_NGX_VULKAN_Init_with_ProjectID defined) and does not need libnvidia-ngx.so.1 at
    load time (NGX dlopens the driver's core, so pt starts on machines without the NVIDIA driver);
  - every DLSS runtime exports NGX's snippet entry points.
Exit code 0 when everything holds, 1 otherwise.
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

GLIBC_FLOOR = (2, 38)
FFX_EXPORTS = {"ffxCreateContext", "ffxDestroyContext", "ffxConfigure", "ffxQuery", "ffxDispatch"}
FFX_NEEDED = {"libc.so.6", "libm.so.6", "ld-linux-x86-64.so.2", "libvulkan.so.1"}


def readobj():
    for candidate in (shutil.which("llvm-readobj"), r"C:\Program Files\LLVM\bin\llvm-readobj.exe"):
        if candidate and Path(candidate).is_file():
            return candidate
    sys.exit("llvm-readobj not found")


def run(tool, *args):
    return subprocess.run([tool, *args], capture_output=True, text=True, check=True).stdout


def needed(tool, path):
    text = run(tool, "--needed-libs", str(path))
    return re.findall(r"^\s+(\S+\.so[\S]*)\s*$", text, re.M)


def glibc_max(tool, path):
    versions = {tuple(int(x) for x in v.split(".")) for v in re.findall(r"GLIBC_(\d+\.\d+(?:\.\d+)?)", run(tool, "--version-info", str(path)))}
    return max(versions) if versions else None


def dynamic_symbols(tool, path):
    """(defined, undefined) names of the dynamic symbol table"""
    defined, undefined = set(), set()
    text = run(tool, "--dyn-symbols", str(path))
    for block in text.split("Symbol {")[1:]:
        name = re.search(r"Name: (\S+)", block)
        section = re.search(r"Section: (\S+)", block)
        if not name:
            continue
        symbol = name.group(1).split("@")[0]
        if section and section.group(1) == "Undefined":
            # weak references (libstdc++'s transactional memory hooks, __cxa_pure_virtual) may stay unresolved
            if "Binding: Weak" not in block:
                undefined.add(symbol)
        else:
            defined.add(symbol)
    return defined, undefined


def main():
    build = Path(sys.argv[1] if len(sys.argv) > 1 else "build/linux-cross")
    tool = readobj()
    failures = []

    def check(condition, message):
        print(("ok    " if condition else "FAIL  ") + message)
        if not condition:
            failures.append(message)

    files = [build / "pt", build / "libamd_fidelityfx_vk.so", *sorted(build.glob("libnvidia-ngx-dlss*.so.*"))]
    for path in files:
        if not path.is_file():
            check(False, f"{path.name} exists in {build}")
            continue
        header = run(tool, "--file-headers", str(path))
        check("Machine: EM_X86_64" in header, f"{path.name}: ELF x86-64")
        libs = needed(tool, path)
        top = glibc_max(tool, path)
        print(f"      {path.name}: needs {', '.join(libs) or 'nothing'}; newest GLIBC_{'.'.join(map(str, top)) if top else '-'}")
        check(top is None or top <= GLIBC_FLOOR, f"{path.name}: asks for at most GLIBC_{GLIBC_FLOOR[0]}.{GLIBC_FLOOR[1]}")
        defined, undefined = dynamic_symbols(tool, path)
        if path.name == "pt":
            check("libnvidia-ngx.so.1" not in libs, "pt does not need the driver's libnvidia-ngx.so.1 at load time")
            static = run(tool, "--symbols", str(path)) if "NVSDK_NGX_VULKAN_Init_with_ProjectID" not in defined else ""
            check("NVSDK_NGX_VULKAN_Init_with_ProjectID" in defined or "Name: NVSDK_NGX_VULKAN_Init_with_ProjectID" in static,
                  "pt has NGX's loader (libnvsdk_ngx.a) linked in")
            check(not any(lib.startswith("libamd_fidelityfx") for lib in libs), "pt opens libamd_fidelityfx_vk.so with dlopen, not at load time")
        elif path.name == "libamd_fidelityfx_vk.so":
            check(defined == FFX_EXPORTS, f"libamd_fidelityfx_vk.so exports exactly {sorted(FFX_EXPORTS)} (has {sorted(defined)})")
            check(set(libs) <= FFX_NEEDED, f"libamd_fidelityfx_vk.so needs only {sorted(FFX_NEEDED)}")
            vulkan = sorted(s for s in undefined if s.startswith("vk"))
            print(f"      Vulkan loader functions it calls directly: {', '.join(vulkan)}")
            # glibc itself provides __cxa_atexit and __cxa_finalize
            runtime = sorted(s for s in undefined if s.startswith(("_Z", "__cxa_", "_Unwind", "__gxx")) and s not in ("__cxa_atexit", "__cxa_finalize"))
            check(not runtime, f"libamd_fidelityfx_vk.so has the C++ runtime linked in (unresolved: {', '.join(runtime) or 'none'})")
        else:
            check("NVSDK_NGX_VULKAN_CreateFeature" in defined or "NVSDK_NGX_VULKAN_CreateFeature1" in defined,
                  f"{path.name} exports NGX's Vulkan snippet entry points")
    print("PASS" if not failures else f"FAIL ({len(failures)})")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
