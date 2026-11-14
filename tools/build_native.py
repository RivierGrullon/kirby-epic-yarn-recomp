#!/usr/bin/env python3
"""Build the Kirby bootstrap with a locally adapted WiiCompiled runtime.

The build order and Linux flags follow WarioRecomp's tools/build-wario.sh (GPL-3.0).
The initial headless bootstrap rejects unaudited hooks and reports hardware gaps.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from kirby import ROOT, run_logged
from build_layout import BuildLayout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configure-only", action="store_true")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--translated-jobs", type=int, default=2)
    parser.add_argument("--stage-root",type=Path,help="Use isolated outputs below local/setup/builds/JOB")
    args = parser.parse_args()
    try:
        layout=BuildLayout(ROOT,args.stage_root)
        layout.ensure()
        if not (layout.generated / "build_shards/shards.cmake").is_file():
            raise ValueError("Run tools/kirby.py translate first")
        if not (layout.generated / "KirbyBootConfig.h").is_file():
            raise ValueError("Run tools/kirby.py prepare to verify the DOL and generate Kirby boot metadata")
        aurora = ROOT / "vendor/wiicompiled/aurora-main"
        if not (aurora / "CMakeLists.txt").is_file():
            raise ValueError("Aurora is missing; run tools/bootstrap.py")
        # runtime/ must be a physical directory: resolving a symlink followed
        # by /.. otherwise makes CMake and Clang miss sibling generated/.
        workspace = layout.workspace
        workspace.mkdir(parents=True, exist_ok=True)
        source = ROOT / "vendor/wiicompiled/runtime"
        runtime = workspace / "runtime"
        stamp = workspace / ".runtime-source"
        commit = json.loads((ROOT / "dependencies.json").read_text())["wiicompiled"]["commit"]
        if runtime.is_symlink() and runtime.resolve() == source.resolve():
            runtime.unlink()  # Migrate the owned symlink from the first build attempt.
        if not runtime.exists():
            shutil.copytree(source, runtime)
            shutil.copy2(ROOT / "vendor/wiicompiled/LICENSE", workspace / "WiiCompiled.LICENSE")
            stamp.write_text(commit + "\n")
        if not stamp.is_file() or stamp.read_text().strip() != commit:
            raise ValueError("Runtime snapshot is partial or from a different revision; move local/native-workspace before retrying")
        link, target = workspace / "generated", layout.generated
        if not (link.is_symlink() and link.resolve() == target.resolve()):
            if link.exists() or link.is_symlink():
                raise ValueError(f"Unexpected existing path: {link}; left untouched")
            link.symlink_to(os.path.relpath(target, workspace), target_is_directory=True)
        apply = ["git", "apply", f"--directory={workspace.relative_to(ROOT).as_posix()}"]
        for patch in sorted((ROOT / "patches/wiicompiled").glob("*.patch")):
            if subprocess.run([*apply, "--check", str(patch)], cwd=ROOT, capture_output=True).returncode == 0:
                subprocess.run([*apply, str(patch)], cwd=ROOT, check=True)
            elif subprocess.run([*apply, "--reverse", "--check", str(patch)], cwd=ROOT, capture_output=True).returncode:
                raise ValueError(f"{patch.name} does not apply cleanly; local source edits were left untouched")
        aurora_snapshot = workspace / "aurora"
        aurora_stamp = workspace / ".aurora-source"
        aurora_commit = json.loads((ROOT / "dependencies.json").read_text())["wiicompiled"]["submodules"]["aurora-main"]
        if not aurora_snapshot.exists():
            shutil.copytree(aurora, aurora_snapshot)
            aurora_stamp.write_text(aurora_commit + "\n")
        if not aurora_stamp.is_file() or aurora_stamp.read_text().strip() != aurora_commit:
            raise ValueError("Aurora snapshot is partial or from a different revision; left untouched")
        for patch in sorted((ROOT / "patches/aurora").glob("*.patch")):
            if subprocess.run([*apply, "--check", str(patch)], cwd=ROOT, capture_output=True).returncode == 0:
                subprocess.run([*apply, str(patch)], cwd=ROOT, check=True)
            elif subprocess.run([*apply, "--reverse", "--check", str(patch)], cwd=ROOT, capture_output=True).returncode:
                raise ValueError(f"{patch.name} does not apply cleanly; local source edits were left untouched")
        aurora = aurora_snapshot
        build = layout.build
        configure=["cmake", "-S", workspace / "runtime", "-B", build, "-G", "Ninja",
                                "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++",
                                "-DCMAKE_BUILD_TYPE=Release", "-DAURORA_DAWN_PROVIDER=package",
                                "-DAURORA_SDL3_PROVIDER=vendor",
                                f"-DKIRBY_PROJECT_ROOT={ROOT}",
                                f"-DKIRBY_GENERATED_ROOT={layout.generated}",
                                f"-DCMAKE_PROJECT_mkw_recompiled_INCLUDE={ROOT / 'cmake/native_project.cmake'}",
                                f"-DMKW_AURORA_DIR={aurora}",
                                f"-DMKW_TRANSLATED_SHARD_MANIFEST={layout.generated / 'build_shards/shards.cmake'}",
                                f"-DMKW_TRANSLATED_COMPILE_JOBS={max(1, args.translated_jobs)}"]
        if layout.stage is not None:
            configure.append(f"-DCMAKE_RUNTIME_OUTPUT_DIRECTORY={build}")
        run_logged("configure",configure,layout=layout)
        if not args.configure_only:
            run_logged("compile", ["cmake", "--build", build, "--target", "WiiCompiled", "kirby-launcher", "--parallel", str(max(1, args.jobs))],layout=layout)
            print("Native Kirby bootstrap built. Run --verify-bootstrap, then --probe-init and a bounded --headless probe.")
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
