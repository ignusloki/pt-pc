"""Run the focused camera-blur math test against the production method body.

Usage: python tools/motion_blur_check.py --glm <glm source directory>
Run from a Visual Studio developer shell on Windows (clang++ available).
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--glm", required=True, type=Path)
parser.add_argument("--compiler", default="clang++")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / "src/engine/render/scene_post.cpp").read_text(encoding="utf-8")
start = source.index("void SceneRenderer::UpdateMotion(")
end = source.index("\nbool SceneRenderer::RecordMotionBlur(", start)
with tempfile.TemporaryDirectory(prefix="pt-motion-blur-") as directory:
    scratch = Path(directory)
    (scratch / "motion_update_under_test.inc").write_text(source[start:end], encoding="utf-8")
    compiler = shutil.which(args.compiler) or args.compiler
    binary = scratch / "blur_motion_test.exe"
    subprocess.run([compiler, "-std=c++20", "-I", str(root / "src"), "-I", str(args.glm), "-I", str(scratch),
                    str(root / "tests/blur_motion_test.cpp"), "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)])
    raise SystemExit(result.returncode)
