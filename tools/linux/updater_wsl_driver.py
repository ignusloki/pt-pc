"""The Linux half of tools/linux/updater_wsl_check.py: runs inside WSL (or any Linux) with python3 and the standard library only.

It copies the prepared folder (the dump, the two Linux setups, the signed release files) to a Linux file system, makes the old
install and a new install of the new version with the Linux setups themselves, puts the player's files in, and updates copies of
the old install with the setup and with the in-game updater (pt-updater --game-update), against a local server with and without
Range. Each updated install is compared with the new install, file by file, folder by folder and execute bit by execute bit.

    python3 updater_wsl_driver.py <prepared folder> <port> <result json>

Run it as a user that is not root: the updater's test switches (PT_UPDATE_TEST) are refused to root, as to an elevated process.
"""
import hashlib
import http.server
import json
import os
import shutil
import stat
import subprocess
import sys
import threading

SRC, PORT, RESULT = sys.argv[1], int(sys.argv[2]), sys.argv[3]
WORK = f"/tmp/pt-linux-update-check-{os.getpid()}"
RANGES = {"on": True}
SENT = {"bytes": 0}


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        routes = json.load(open(os.path.join(WORK, "release", "routes.json")))
        if self.path in routes["redirects"]:
            self.send_response(302)
            self.send_header("Location", routes["redirects"][self.path])
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        name = routes["files"].get(self.path)
        if not name:
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        data = open(os.path.join(WORK, "release", name), "rb").read()
        first, last = 0, len(data) - 1
        wanted = self.headers.get("Range", "")
        if RANGES["on"] and wanted.startswith("bytes="):
            a, _, b = wanted[6:].partition("-")
            first, last = int(a), min(int(b), len(data) - 1)
            self.send_response(206)
            self.send_header("Content-Range", f"bytes {first}-{last}/{len(data)}")
        else:
            self.send_response(200)
        self.send_header("Content-Length", str(last - first + 1))
        self.end_headers()
        try:
            self.wfile.write(data[first:last + 1])
            if name.endswith("Setup-linux"):
                SENT["bytes"] += last - first + 1
        except OSError:
            pass


class Quiet(http.server.ThreadingHTTPServer):
    def handle_error(self, request, client_address):
        pass


PLAYER = {"data/pt.ini": b"[display]\nwidth = 1280\n", "data/saves/progress.dat": b"save", "mods/mine/init.lua": b"-- mod",
          "enhanced-textures/cache/0001.dds": b"texture", "my-notes.txt": b"the player's own file"}
SKIP_TOP = {"data", "mods", "enhanced-textures", "CUSA01127"}
SKIP = {"pt-install-manifest.txt", "install-notes.txt", "my-notes.txt"}


def tree(root):
    out = {}
    for folder, dirs, files in os.walk(root):
        for name in dirs + files:
            path = os.path.join(folder, name)
            rel = os.path.relpath(path, root)
            if rel.split("/")[0] in SKIP_TOP or rel in SKIP:
                continue
            if os.path.isdir(path):
                out[rel + "/"] = ""
            else:
                mode = "x" if os.stat(path).st_mode & stat.S_IXUSR else "-"
                out[rel] = mode + hashlib.sha256(open(path, "rb").read()).hexdigest()
    return out


def manifest_files(root):
    return sorted(line for line in open(os.path.join(root, "pt-install-manifest.txt")).read().splitlines() if line.startswith("file="))


def compare(updated, fresh):
    problems = []
    a, b = tree(fresh), tree(updated)
    for key in sorted(set(a) | set(b)):
        if a.get(key) != b.get(key):
            problems.append(f"{key}: new install {a.get(key, 'missing')[:9]}, updated {b.get(key, 'missing')[:9]}")
    if manifest_files(updated) != manifest_files(fresh):
        problems.append("the install manifests list other files")
    for name, data in PLAYER.items():
        if open(os.path.join(updated, name), "rb").read() != data:
            problems.append(f"player file {name} changed")
    return problems[:10]


def leftovers(dest):
    parent = os.path.dirname(dest)
    found = [n for n in os.listdir(parent) if n.startswith(".pt-")] + [n for n in os.listdir(dest) if n.startswith(".pt-")]
    if os.path.exists(os.path.join(dest, "data", "update-journal.txt")):
        found.append("data/update-journal.txt")
    return found


def main():
    shutil.rmtree(WORK, ignore_errors=True)
    shutil.copytree(SRC, WORK)
    for name in ("setup-old", "setup-new"):
        os.chmod(os.path.join(WORK, name), 0o755)
    env = dict(os.environ, PT_UPDATE_TEST="1", PT_UPDATE_TEST_PUBLIC_KEY=open(os.path.join(WORK, "public-key.txt")).read().strip(),
               PT_UPDATE_MANIFEST_URL=f"http://127.0.0.1:{PORT}/repos/LoreanXavier/pt-pc/releases/latest")
    server = Quiet(("127.0.0.1", PORT), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    results = {}

    def setup(exe, source, dest, name):
        result = os.path.join(WORK, name + ".txt")
        subprocess.run([os.path.join(WORK, exe), "--install", source, dest, result], env=env, timeout=600,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return open(result).read() if os.path.exists(result) else "(no result)"

    dump = os.path.join(WORK, "dump", "CUSA01127")
    old = os.path.join(WORK, "old", "P.T. PC Port")
    fresh = os.path.join(WORK, "fresh", "P.T. PC Port")
    results["install old"] = setup("setup-old", dump, old, "install-old").splitlines()[0]
    results["install new"] = setup("setup-new", dump, fresh, "install-new").splitlines()[0]
    for name, data in PLAYER.items():
        os.makedirs(os.path.dirname(os.path.join(old, name)), exist_ok=True)
        open(os.path.join(old, name), "wb").write(data)

    def scenario(name, run):
        dest = os.path.join(WORK, name, "P.T. PC Port")
        shutil.copytree(old, dest, symlinks=True)
        SENT["bytes"] = 0
        text = run(dest)
        problems = [] if text.startswith("PASS updated") else [f"said {text[:300]!r}"]
        problems += compare(dest, fresh)
        if leftovers(dest):
            problems.append(f"leftovers {leftovers(dest)}")
        log = os.path.join(dest, "data", "pt-update.log")
        results[name] = {"verdict": "FAIL" if problems else "PASS", "problems": problems, "said": text.splitlines()[:3],
                         "package bytes sent": SENT["bytes"], "log": open(log).read().splitlines()[-2:] if os.path.exists(log) else []}

    def ingame(ranges):
        def run(dest):
            RANGES["on"] = ranges
            result = os.path.join(os.path.dirname(dest), "result.txt")
            e = dict(env, PT_UPDATE_TEST_RESULT=result)
            subprocess.run([os.path.join(dest, "pt-updater"), "--game-update", dest, "--lang", "0", "--", "--help"], env=e, timeout=600,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return open(result).read() if os.path.exists(result) else "(no result)"
        return run

    scenario("installer", lambda dest: setup("setup-new", "", dest, "installer-result"))
    scenario("ingame-ranges", ingame(True))
    scenario("ingame-full", ingame(False))
    server.shutdown()
    json.dump(results, open(RESULT, "w"), indent=1)
    shutil.rmtree(WORK, ignore_errors=True)


main()
