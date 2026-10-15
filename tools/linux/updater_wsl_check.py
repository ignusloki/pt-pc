"""The Linux setup's update over an existing install and the Linux in-game updater, end to end in WSL (or a Linux machine with
python3; docs/updates.md, "Only what changed"). Run on Windows after the cross build of pt_setup_linux (tools/linux/cross_build.sh):

    python tools/linux/updater_wsl_check.py --setup build/linux-cross/pt_setup_linux --out work/linux-update

It makes an "old" and a "new" payload (the new one changes a file and the game, adds two, removes one, keeps one identical, and
ships pt-updater: the Linux setup itself), attaches each to a copy of pt_setup_linux (tools/linux/attach_payload.py), signs a
manifest for the new one with a test key, and hands the folder to tools/linux/updater_wsl_driver.py inside WSL, which installs,
updates (setup; in-game updater with and without Range) and compares every updated install with a new install, execute bits too.
The game in these payloads is a shell script that exits 0, so the updater's start of the new game succeeds without a GPU.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import random
import shutil
import struct
import subprocess
import sys
import zlib

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "ci"))
sys.path.insert(0, str(REPO / "tools"))
import release_signing  # noqa: E402
from package import FILE_LIST, file_list  # noqa: E402


def payload(files: dict[str, bytes]) -> tuple[bytes, dict]:
    out = bytearray(b"PTSETUP1" + struct.pack("<I", len(files)))
    listing = {}
    for name, data in sorted(files.items()):
        packed = zlib.compress(data, 6)
        digest = hashlib.sha256(data).hexdigest()
        out += struct.pack("<H", len(name.encode())) + name.encode() + struct.pack("<QQ", len(data), len(packed)) + digest.encode() + packed
        listing[name] = {"bytes": len(data), "sha256": digest}
    return bytes(out), listing


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--setup", type=pathlib.Path, required=True)
    p.add_argument("--out", type=pathlib.Path, required=True)
    a = p.parse_args()
    out = a.out.resolve()
    if out.exists():
        shutil.rmtree(out)
    prepared = out / "prepared"
    (prepared / "release").mkdir(parents=True)
    updater = a.setup.read_bytes()
    rng = random.Random(5)
    blob = bytes(rng.getrandbits(8) for _ in range(3 << 20))  # an incompressible 3 MB file, so ranges are worth it
    old = {"pt": b"#!/bin/sh\n# the old game\nexit 0\n", "pt-updater": updater, "shaders/a.spv": b"a, old", "shaders/same.spv": b"same",
           "voice/model.bin": blob, "gone/only-old.txt": b"a file the new version drops", "libggml.so": b"a library"}
    new = dict(old)
    new["pt"] = b"#!/bin/sh\n# the new game\nexit 0\n"
    new["shaders/a.spv"] = b"a, new"
    new["voice/new.bin"] = b"a new file"
    new["texture-tools/realesrgan-ncnn-vulkan"] = b"#!/bin/sh\nexit 0\n"
    del new["gone/only-old.txt"]
    for files in (old, new):
        files[FILE_LIST] = file_list(files)
    attach = REPO / "tools" / "linux" / "attach_payload.py"
    for name, files in (("old", old), ("new", new)):
        data, listing = payload(files)
        (out / f"{name}.bin").write_bytes(data)
        (out / f"{name}.json").write_text(json.dumps(listing), encoding="utf-8")
        subprocess.run([sys.executable, str(attach), "--setup", str(a.setup), "--payload", str(out / f"{name}.bin"), "--out", str(prepared / f"setup-{name}")],
                       check=True, stdout=subprocess.DEVNULL)
    # the signed manifest inside the new setup, as tools/ci/release.py makes it (a throwaway key): the setup installs with it too
    seed = hashlib.sha256(b"pt linux update test key").digest()
    manifest = release_signing.embed_signed("9.9.9", {"linux": prepared / "setup-new"}, {"linux": out / "new.json"}, seed)
    package = prepared / "release" / "P.T.PC.Port.Setup-linux"
    shutil.copy2(prepared / "setup-new", package)
    (prepared / "public-key.txt").write_text(release_signing.public_key(seed).hex(), encoding="ascii")
    port = 18000 + rng.randrange(2000)
    base = f"http://127.0.0.1:{port}"
    dl = "/repos/LoreanXavier/pt-pc/releases/download/v9.9.9"
    release = {"tag_name": "v9.9.9", "html_url": "https://github.com/LoreanXavier/pt-pc/releases/tag/v9.9.9", "draft": False, "prerelease": False,
               "body": "test", "assets": [{"name": "P.T.PC.Port.Setup-linux", "size": package.stat().st_size,
                                           "browser_download_url": f"{base}{dl}/P.T.PC.Port.Setup-linux"}]}
    (prepared / "release" / "latest.json").write_text(json.dumps(release), encoding="utf-8")
    routes = {"files": {"/repos/LoreanXavier/pt-pc/releases/latest": "latest.json", "/objects/package": "P.T.PC.Port.Setup-linux"},
              "redirects": {f"{dl}/P.T.PC.Port.Setup-linux": f"{base}/objects/package"}}
    (prepared / "release" / "routes.json").write_text(json.dumps(routes), encoding="utf-8")
    dump = prepared / "dump" / "CUSA01127"
    dump.mkdir(parents=True)
    (dump / "chunk1.psarc").write_bytes(b"PSAR" + bytes(60))
    qar = bytearray(0x40)
    qar[0x40 - 0x24 + 0x16:0x40 - 0x24 + 0x18] = b"aq"
    (dump / "texture.qar").write_bytes(bytes(qar))
    (dump / "pathid_list_ps4.bin").write_bytes(b"/Assets/sh/level/pt14_hallway/" + b"x" * 40)
    shutil.copy2(REPO / "tools" / "linux" / "updater_wsl_driver.py", prepared / "driver.py")

    def wsl_path(path: pathlib.Path) -> str:
        return "/mnt/" + path.drive[0].lower() + path.as_posix()[2:]

    # the test switches are refused to root (update::TestOverrides), and WSL's default user often is root: the driver runs as
    # nobody then (setpriv drops the rights, no account is made), and writes its result where nobody may write
    linux_result = f"/tmp/pt-linux-update-result-{port}.json"
    as_user = ["setpriv", "--reuid=65534", "--regid=65534", "--clear-groups"]
    root = subprocess.run(["wsl.exe", "-e", "id", "-u"], capture_output=True, text=True).stdout.strip() == "0"
    subprocess.run(["wsl.exe", "-e", *(as_user if root else []), "python3", wsl_path(prepared / "driver.py"), wsl_path(prepared), str(port), linux_result],
                   timeout=1800)
    text = subprocess.run(["wsl.exe", "-e", "cat", linux_result], capture_output=True, text=True).stdout
    if not text.strip():
        sys.exit("the WSL driver wrote no result")
    subprocess.run(["wsl.exe", "-e", "rm", "-f", linux_result])
    report = json.loads(text)
    (out / "result.json").write_text(text, encoding="utf-8")
    files = {f["path"]: f for f in json.loads(manifest)["files"]["linux"]}
    changed = [n for n in new if old.get(n) != new[n]]
    report["changed packed bytes"] = sum(files[n]["packed"] for n in changed)
    report["package bytes"] = package.stat().st_size
    print(json.dumps(report, indent=1))
    verdicts = [v["verdict"] for k, v in report.items() if isinstance(v, dict)]
    ranged = report.get("ingame-ranges", {}).get("package bytes sent", 0)
    ok = verdicts and all(v == "PASS" for v in verdicts) and ranged <= report["changed packed bytes"] + 2 * 256 * 1024
    print(f"{verdicts.count('PASS')} of {len(verdicts)} Linux scenarios passed; ranged update sent {ranged} bytes for "
          f"{report['changed packed bytes']} changed bytes of a {report['package bytes']} byte package")
    shutil.rmtree(prepared, ignore_errors=True)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
