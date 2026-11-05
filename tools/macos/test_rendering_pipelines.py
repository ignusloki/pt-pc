"""Run Mac rendering checks with isolated profiles and captures; never installs the game."""
import argparse
import ast
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def run(command, directory, log, env=None, timeout=180):
    started = time.monotonic()
    with log.open("w") as stream:
        result = subprocess.run(list(map(str, command)), cwd=directory, env=env,
                                stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
    return result.returncode, time.monotonic() - started


def unit_checks(build, output):
    module = ast.parse((ROOT / "tools/macos/build.py").read_text())
    names = list(next(ast.literal_eval(n.value) for n in module.body if isinstance(n, ast.Assign)
                      and any(isinstance(t, ast.Name) and t.id == "TESTS" for t in n.targets)))
    names += ["pt_exposure_test", "pt_rendering_pipeline_test", "pt_enhanced_hdr_pipeline_test",
              "pt_reflection_mix_test", "pt_skin_render_test", "pt_bc_decode_test", "pt_touch_controls_test",
              "pt_ios_lifecycle_test", "pt_ios_scene_state_test"]
    fixtures = output / "fixtures"
    fixtures.mkdir()
    args = {"pt_settings_roundtrip_test": "settings", "pt_save_status_test": "save-status",
            "pt_save_reset_test": "save-reset", "pt_graphics_preset_test": "graphics.ini",
            "pt_installer_update_test": "installer", "pt_unicode_paths_test": "unicode",
            "pt_user_data_test": "user-data", "pt_macos_data_path_test": "macos-data-path",
            "pt_rendering_pipeline_test": "profiles"}
    results = []
    for name in dict.fromkeys(names):
        log = output / (name + ".log")
        command = [build / name] + ([fixtures / args[name]] if name in args else [])
        code, elapsed = run(command, fixtures, log, timeout=300)
        text = log.read_text(errors="replace")
        skipped = [line for line in text.splitlines() if line.startswith("SKIP")]
        results.append({"test": name, "code": code, "seconds": elapsed, "skipped": skipped})
        print(f"{'FAIL' if code else 'SKIP' if skipped else 'PASS'} {name} ({elapsed:.1f}s)", flush=True)
    return results


def smoke_checks(build, game, output):
    results = []
    for pipeline in ("original", "enhanced"):
        for hdr in (False, True):
            for upscaler in ("off", "metalfx"):
                name = f"{pipeline}-{'hdr' if hdr else 'sdr'}-{upscaler}"
                case = output / name
                case.mkdir()
                settings = case / "pt.ini"
                settings.write_text(f"[display]\nrendering_pipeline={pipeline}\nhdr={int(hdr)}\n"
                                    "hdr_paper_white=200\nhdr_peak=1000\n[network]\ncheck_updates=0\n"
                                    f"[upscaling]\nupscaler={upscaler}\nquality=quality\n"
                                    "[graphics]\nfilm_grain=0\n[enhanced_graphics]\nfilm_grain=0\n")
                route = case / "route.txt"
                parts = ["start", "walk"]
                lines = []
                for part in parts:
                    for line in (ROOT / f"tests/walkthrough/{part}.txt").read_text().splitlines():
                        if line.strip() and not line.lstrip().startswith("#"):
                            lines.append("700 " + line.replace("{shots}", case.as_posix()))
                lines += ["700 swait 60", "700 sshot scene.png", "700 slog", "700 squit"]
                route.write_text("\n".join(lines) + "\n")
                env = {**os.environ, "PT_SYSTEM_LANGUAGE": "en-US", "PT_MEMORY_GATE": "0",
                       "PT_PHOTO_DIR": str(case / "photos")}
                env.pop("PT_HDR_HEADLESS", None)
                if hdr:
                    env["PT_HDR_HEADLESS"] = "scrgb"
                    env["PT_HDR_DUMP"] = "1"
                command = [build / "pt", "--game", game, "--headless", "--no-save", "--no-mods",
                           "--no-update-check", "--settings", settings, "--input-script", route,
                           "--frames", "4000", "--width", "960", "--height", "540", "--seed", "1",
                           "--audio-offline", "--shot-warmup", "16", "--shot-settle"]
                code, elapsed = run(command, case, case / "console.log", env, timeout=300)
                log = (case / "pt.log").read_text(errors="replace") if (case / "pt.log").exists() else ""
                errors = [line for line in log.splitlines() if re.search(r"\berror\b|VK_ERROR|FAIL", line)]
                good = (code == 0 and f"renderer: {pipeline} pipeline" in log and
                        (case / "scene.png").is_file() and "floor: NextFloor" in log and not errors)
                if upscaler == "metalfx":
                    good = good and "upscale: Apple MetalFX Quality" in log
                results.append({"test": name, "code": 0 if good else 1, "process_code": code,
                                "seconds": elapsed, "errors": errors})
                print(f"{'PASS' if good else 'FAIL'} {name} ({elapsed:.1f}s)", flush=True)
    return results


def walkthrough_checks(build, game, output):
    spec = importlib.util.spec_from_file_location("pt_walkthrough", ROOT / "tools/walkthrough.py")
    walkthrough = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(walkthrough)
    results = []
    for pipeline in ("original", "enhanced"):
        scenario = dict(walkthrough.SCENARIOS["full"])
        # Rendering the captures throughout the route also checks scene/effect
        # transitions. Settings and gameplay saves belong to the runner's fixture.
        scenario["work_files"] = {"pt.ini": (f"[display]\nrendering_pipeline={pipeline}\n"
            "[network]\ncheck_updates=0\n[voice]\nmicrophone_enabled=0\n"
            "[upscaling]\nupscaler=metalfx\nquality=quality\n").encode()}
        scenario["env"] = {"PT_MEMORY_GATE": "0"}
        name, milestones, elapsed, stop, work = walkthrough.run(
            f"dual_{pipeline}", scenario, build / "pt", game, 20.0, True,
            output / pipeline, pt_args=("--audio-offline", "--no-update-check", "--shot-settle"))
        passed = sum(position is not None for _, position in milestones)
        result = {"test": name, "code": int(passed != len(milestones)), "passed": passed,
                  "checks": len(milestones), "seconds": elapsed, "stop": stop, "work": str(work),
                  "milestones": milestones}
        print(f"{'PASS' if result['code'] == 0 else 'FAIL'} {name}: {passed}/{len(milestones)} milestones ({elapsed:.1f}s)", flush=True)
        results.append(result)
    return results


def menu_checks(build, game, output):
    settings = output / "pt.ini"
    settings.write_text("[display]\nrendering_pipeline=original\n[graphics]\nfilm_grain=.25\n"
                        "[enhanced_graphics]\nfilm_grain=.75\n[network]\ncheck_updates=0\n")
    base = (ROOT / "tests/pad/rendering_pipeline.txt").read_text()
    back = base.replace("pc_pipeline_original", "pc_pipeline_temporary").replace(
        "pc_pipeline_enhanced", "pc_pipeline_original").replace("pc_pipeline_temporary", "pc_pipeline_enhanced")
    back = back.replace("menukey right", "menukey left").replace("pipeline_enhanced_next_start.png", "pipeline_original_next_start.png")
    restored = "\n".join(("700 sstep 15", "700 swait 30", "700 sui menu pc", "700 swait 90",
                           "700 sexpect pcvaluekey pc_rendering_pipeline pc_pipeline_original",
                           "700 sshot pipeline_original_restored.png", "700 squit")) + "\n"
    results = []
    for index, (active, script) in enumerate((("original", base), ("enhanced", back), ("original", restored))):
        route = output / f"menu-{index}.txt"
        route.write_text(script)
        log = output / f"menu-{index}.log"
        command = [build / "pt", "--headless", "--no-save", "--no-update-check", "--no-mods", "--audio-offline",
                   "--game", game, "--settings", settings, "--input-script", route, "--log", log,
                   "--frames", "4000", "--width", "1280", "--height", "720", "--seed", "1"]
        env = {**os.environ, "PT_SYSTEM_LANGUAGE": "en-US", "PT_MEMORY_GATE": "0"}
        code, elapsed = run(command, output, output / f"menu-{index}-console.log", env, timeout=180)
        text = log.read_text(errors="replace")
        good = code == 0 and f"renderer: {active} pipeline" in text and bool(re.search(r"input script: \d+ expectations, 0 failed", text))
        results.append({"test": f"menu-restart-{index}-{active}", "code": 0 if good else 1, "seconds": elapsed})
        print(f"{'PASS' if good else 'FAIL'} menu restart {index}: {active}", flush=True)
    return results


def framegen_checks(build, game, output):
    """Exercise native Mac HDR presentation and real MetalFX interpolation."""
    results = []
    for pipeline in ("original", "enhanced"):
        case = output / pipeline
        case.mkdir()
        settings = case / "pt.ini"
        settings.write_text(f"[display]\nrendering_pipeline={pipeline}\nhdr=1\nfullscreen=0\n"
                            "width=960\nheight=540\nfps_limit=30\n[voice]\nmicrophone_enabled=0\n"
                            "[network]\ncheck_updates=0\n[upscaling]\nupscaler=metalfx\n"
                            "quality=quality\nframe_generation=metalfx\n")
        route = case / "route.txt"
        route.write_text("0 focus 1\n20 smenu 1\n20 swait 30\n20 sktap Escape\n"
                         "20 smenu 0\n20 sstep 15\n20 swait 90\n"
                         "20 sshot framegen.png\n20 slog\n20 squit\n")
        env = {**os.environ, "PT_SYSTEM_LANGUAGE": "en-US", "PT_MEMORY_GATE": "0"}
        env.pop("PT_HDR_HEADLESS", None)
        command = [build / "pt", "--game", game, "--options-menu", "--no-save", "--no-mods",
                   "--no-update-check", "--settings", settings, "--input-script", route, "--log", case / "pt.log",
                   "--frames", "4000", "--width", "960", "--height", "540", "--seed", "1",
                   "--audio-offline"]
        code, elapsed = run(command, case, case / "console.log", env, timeout=180)
        log = (case / "pt.log").read_text(errors="replace") if (case / "pt.log").exists() else ""
        generated = re.search(r"MetalFX queued (\d+) interpolated frames", log)
        transport = "extended signed" if pipeline == "enhanced" else "original gain"
        errors = [line for line in log.splitlines() if re.search(r"\berror\b|VK_ERROR|FAIL", line)]
        good = (code == 0 and f"renderer: {pipeline} pipeline" in log and
                f"{transport} HDR RGB transport" in log and generated and int(generated[1]) > 0 and
                (case / "framegen.png").is_file() and not errors)
        results.append({"test": f"{pipeline}-native-hdr-framegen", "code": 0 if good else 1,
                        "process_code": code, "seconds": elapsed, "errors": errors,
                        "generated": int(generated[1]) if generated else 0})
        print(f"{'PASS' if good else 'FAIL'} {pipeline} native HDR + MetalFX frame generation", flush=True)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--game", type=Path)
    parser.add_argument("--phase", choices=("unit", "smoke", "walkthrough", "menu", "framegen"), required=True)
    args = parser.parse_args()
    build, output = args.build.resolve(), args.out.resolve()
    output.mkdir(parents=True, exist_ok=False)
    if args.phase != "unit" and not args.game:
        parser.error("--game is required for gameplay checks")
    if args.phase == "unit":
        results = unit_checks(build, output)
    elif args.phase == "smoke":
        results = smoke_checks(build, args.game.resolve(), output)
    elif args.phase == "menu":
        results = menu_checks(build, args.game.resolve(), output)
    elif args.phase == "framegen":
        results = framegen_checks(build, args.game.resolve(), output)
    else:
        results = walkthrough_checks(build, args.game.resolve(), output)
    (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    return int(any(r.get("code", 0) for r in results))


if __name__ == "__main__":
    raise SystemExit(main())
