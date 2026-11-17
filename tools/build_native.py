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


def find_windows_toolchain(root=None):
    if root is None:
        compiler = shutil.which('x86_64-w64-mingw32-clang')
        if compiler is None:
            raise ValueError('LLVM-MinGW was not found on PATH; add its bin directory '
                             'or use --llvm-mingw-root')
        # Preserve driver symlinks: their directory identifies the installation.
        root = Path(compiler).absolute().parent.parent
    root = Path(root).absolute()
    suffix = '.exe' if os.name == 'nt' else ''
    for name in ('clang', 'clang++', 'windres'):
        tool = root/'bin'/f'x86_64-w64-mingw32-{name}{suffix}'
        if not tool.is_file():
            raise ValueError(f'Incomplete LLVM-MinGW installation: missing {tool}')
    return root


def apply_snapshot_patch(patch, workspace):
    # Git for Windows may have checked out clean dependency files as CRLF.
    # Only normalize the owned build copy; upstream worktrees stay untouched.
    content = patch.read_bytes().replace(b'\r\n', b'\n')
    for line in content.splitlines():
        if not line.startswith(b'+++ b/'):
            continue
        target = workspace/line[6:].decode('utf-8')
        if target.is_symlink() or not target.resolve().is_relative_to(workspace.resolve()):
            raise ValueError('Patch target escapes the private build snapshot')
        if target.is_file():
            original = target.read_bytes()
            if b'\0' not in original and b'\r\n' in original:
                target.write_bytes(original.replace(b'\r\n', b'\n'))
    apply = ['git', 'apply', '--ignore-space-change', f'--directory={workspace.relative_to(ROOT).as_posix()}']
    if subprocess.run([*apply, '--check'], input=content, cwd=ROOT, capture_output=True).returncode == 0:
        subprocess.run(apply, input=content, cwd=ROOT, check=True)
    elif subprocess.run([*apply, '--reverse', '--check'], input=content, cwd=ROOT, capture_output=True).returncode:
        raise ValueError(f'{patch.name} does not apply cleanly; local source edits were left untouched')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configure-only", action="store_true")
    parser.add_argument("--target", choices=("linux", "windows"), default="windows" if os.name == "nt" else "linux")
    parser.add_argument("--llvm-mingw-root", type=Path, help="LLVM-MinGW installation (default: discover on PATH)")
    parser.add_argument("--cppwinrt-include", type=Path, help="Directory containing winrt/base.h (default: installed Windows SDK)")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--translated-jobs", type=int, default=2)
    parser.add_argument("--stage-root",type=Path,help="Use isolated outputs below local/setup/builds/JOB")
    parser.add_argument("--profile-dawn-calls", action="store_true",
                        help="Instrument Linux WebGPU calls; requires an isolated --stage-root")
    args = parser.parse_args()
    try:
        if args.profile_dawn_calls and (args.stage_root is None or args.target != 'linux'):
            raise ValueError("--profile-dawn-calls requires a Linux build with --stage-root")
        mingw_root = find_windows_toolchain(args.llvm_mingw_root) if args.target == 'windows' else None
        stage = args.stage_root
        if args.target == 'windows' and os.name != 'nt' and stage is None:
            stage = ROOT/'local/setup/builds/windows-developer'
        layout=BuildLayout(ROOT,stage)
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
        if not ((link.is_symlink() or (os.name == "nt" and link.is_junction())) and link.resolve() == target.resolve()):
            if link.exists() or link.is_symlink():
                raise ValueError(f"Unexpected existing path: {link}; left untouched")
            if os.name == "nt":
                link_literal = str(link).replace("'", "''")
                target_literal = str(target).replace("'", "''")
                subprocess.run(["powershell", "-NoProfile", "-Command",
                                f"New-Item -ItemType Junction -Path '{link_literal}' -Target '{target_literal}' | Out-Null"], check=True)
            else:
                link.symlink_to(os.path.relpath(target, workspace), target_is_directory=True)
        for patch in sorted((ROOT / "patches/wiicompiled").glob("*.patch")):
            apply_snapshot_patch(patch, workspace)
        aurora_snapshot = workspace / "aurora"
        aurora_stamp = workspace / ".aurora-source"
        aurora_commit = json.loads((ROOT / "dependencies.json").read_text())["wiicompiled"]["submodules"]["aurora-main"]
        if not aurora_snapshot.exists():
            shutil.copytree(aurora, aurora_snapshot)
            aurora_stamp.write_text(aurora_commit + "\n")
        if not aurora_stamp.is_file() or aurora_stamp.read_text().strip() != aurora_commit:
            raise ValueError("Aurora snapshot is partial or from a different revision; left untouched")
        for patch in sorted((ROOT / "patches/aurora").glob("*.patch")):
            apply_snapshot_patch(patch, workspace)
        aurora = aurora_snapshot
        build = layout.build
        platform_options = []
        if os.name == "nt":
            headers = args.cppwinrt_include
            if headers is None:
                sdk = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Windows Kits/10/Include"
                candidates = sorted(sdk.glob("*/cppwinrt/winrt/base.h"), reverse=True)
                headers = candidates[0].parent.parent if candidates else None
            if headers is None or not (headers / 'winrt/base.h').is_file():
                raise ValueError("Windows SDK C++/WinRT headers are required for the pinned runtime")
            platform_options.append(f"-DMKW_CPPWINRT_INCLUDE_DIR={headers.resolve().as_posix()}")
        configure=["cmake", "-S", workspace / "runtime", "-B", build, "-G", "Ninja",
                                "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++",
                                "-DCMAKE_BUILD_TYPE=Release", "-DAURORA_DAWN_PROVIDER=package",
                                f"-DPython3_EXECUTABLE={sys.executable}",
                                "-DAURORA_SDL3_PROVIDER=vendor",
                                f"-DAURORA_SDL3_LINKAGE={'shared' if args.target == 'windows' else 'static'}",
                                f"-DKIRBY_PROJECT_ROOT={ROOT}",
                                f"-DKIRBY_GENERATED_ROOT={layout.generated}",
                                f"-DKIRBY_PROFILE_DAWN_CALLS={'ON' if args.profile_dawn_calls else 'OFF'}",
                                f"-DCMAKE_PROJECT_mkw_recompiled_INCLUDE={ROOT / 'cmake/native_project.cmake'}",
                                f"-DMKW_AURORA_DIR={aurora}",
                                f"-DMKW_TRANSLATED_SHARD_MANIFEST={layout.generated / 'build_shards/shards.cmake'}",
                                f"-DMKW_TRANSLATED_COMPILE_JOBS={max(1, args.translated_jobs)}", *platform_options]
        if layout.stage is not None:
            configure.append(f"-DCMAKE_RUNTIME_OUTPUT_DIRECTORY={build}")
        if args.target == 'windows':
            configure = [argument for argument in configure if not str(argument).startswith(
                ('-DCMAKE_C_COMPILER=', '-DCMAKE_CXX_COMPILER='))]
            configure.append(f"-DKIRBY_LLVM_MINGW_ROOT={mingw_root.as_posix()}")
            if os.name != 'nt':
                configure.append(f"-DCMAKE_TOOLCHAIN_FILE={ROOT/'cmake/windows-toolchain.cmake'}")
            else:
                for language, tool in (('C', 'clang'), ('CXX', 'clang++'), ('RC', 'windres')):
                    configure.append(f"-DCMAKE_{language}_COMPILER="
                                     f"{mingw_root.as_posix()}/bin/x86_64-w64-mingw32-{tool}.exe")
        run_logged("configure",configure,layout=layout)
        if not args.configure_only:
            run_logged("compile", ["cmake", "--build", build, "--target", "WiiCompiled", "kirby-launcher", "--parallel", str(max(1, args.jobs))],layout=layout)
            if args.target == 'windows':
                # LLVM's C++ runtime and Dawn may be shared libraries. Keep DLLs
                # next to the products so native Windows can resolve them.
                libraries = list(build.rglob('*.dll'))
                toolchain = mingw_root/'x86_64-w64-mingw32/bin'
                libraries += list(toolchain.glob('*.dll'))
                for library in libraries:
                    if library.parent != build:
                        destination = build/library.name
                        if destination.exists() and destination.read_bytes() != library.read_bytes():
                            raise ValueError(f'Conflicting Windows runtime library: {library.name}')
                        shutil.copy2(library, destination)
            print("Native Kirby bootstrap built. Run --verify-bootstrap, then --probe-init and a bounded --headless probe.")
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
