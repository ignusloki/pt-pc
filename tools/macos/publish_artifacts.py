"""Publish completed Mac workflow packages after checking their source commit."""
import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path


def gh_json(path):
    return json.loads(subprocess.check_output(["gh", "api", path], text=True))


def verify_source(repo, run_id, tag):
    run = gh_json(f"repos/{repo}/actions/runs/{run_id}")
    if run.get("conclusion") != "success":
        raise RuntimeError("The selected build did not complete successfully")
    # The commit endpoint resolves annotated and lightweight tags alike.
    commit = gh_json(f"repos/{repo}/commits/{tag}")
    if not run.get("head_sha") or run["head_sha"] != commit.get("sha"):
        raise RuntimeError("The release tag and selected build have different source commits")
    return run["head_sha"]


def collect_assets(root, require_setup=False):
    portable = {f"P.T.PC.Port-macOS-{arch}.{ext}" for arch in ("arm64", "x64") for ext in ("zip", "dmg")}
    setup = {f"P.T.PC.Port.Setup-macOS-{arch}.zip" for arch in ("arm64", "x64")}
    selected = {}
    for path in root.rglob("*"):
        if path.is_file() and path.name.startswith(("P.T.PC.Port-macOS-", "P.T.PC.Port.Setup-macOS-")) and path.suffix in (".zip", ".dmg"):
            selected.setdefault(path.name, []).append(path)
    expected = portable | (setup if require_setup or setup.intersection(selected) else set())
    if set(selected) != expected:
        raise RuntimeError(f"Incomplete or unexpected Mac assets: missing {sorted(expected-set(selected))}; unexpected {sorted(set(selected)-expected)}")
    if any(len(paths) != 1 for paths in selected.values()):
        raise RuntimeError("More than one artifact has the same release filename")
    return [selected[name][0] for name in sorted(expected)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY"))
    parser.add_argument("--run-id", required=True, type=int)
    parser.add_argument("--tag", required=True)
    args = parser.parse_args()
    if not args.repo or args.run_id <= 0:
        parser.error("Provide a repository and a positive workflow run ID")
    sha = verify_source(args.repo, args.run_id, args.tag)
    with tempfile.TemporaryDirectory(prefix="pt-macos-publish-") as directory:
        root = Path(directory)
        subprocess.run(["gh", "run", "download", str(args.run_id), "--repo", args.repo, "--dir", str(root)], check=True)
        assets = collect_assets(root, require_setup=args.repo.lower() == "ignusloki/pt-pc")
        print(f"Verified {len(assets)} Mac packages from {sha}", flush=True)
        subprocess.run(["gh", "release", "upload", "--clobber", "--repo", args.repo, "--", args.tag, *map(str, assets)], check=True)


if __name__ == "__main__":
    main()
