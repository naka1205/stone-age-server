#!/usr/bin/env python3
"""Reproduce the 2026-09-11 audit without changing production source or legacy code.

Requires local CMake, Ninja, a Clang/GCC-compatible c++, and a cached doctest checkout.
Only the new server/client are built. The legacy server is never compiled or run.
Exit 1 = at least one fidelity expectation failed (expected on the audited revision).
Exit 2 = preparation/build/baseline-test failure; this is not a finding reproduction.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
SERVER = HERE.parents[3]
CLIENT = SERVER.parent / "stone-age-client"


def run(args, log, cwd=None):
    p = subprocess.run([str(x) for x in args], cwd=cwd, text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log.write_text(p.stdout, encoding="utf-8")
    return p.returncode, p.stdout


def git_head(repo):
    return subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"],
                                   text=True).strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build-root", type=Path)
    ap.add_argument("--doctest-source", type=Path, required=True)
    ap.add_argument("--cxx", default="c++")
    ns = ap.parse_args()
    root = (ns.build_root or Path(tempfile.mkdtemp(prefix="stoneage-audit-"))).resolve()
    root.mkdir(parents=True, exist_ok=True)
    if any((root / n / "CMakeCache.txt").exists() for n in ("server", "client")):
        print("Use a new build-root to avoid reusing binaries from another revision.", file=sys.stderr)
        return 2
    if not (ns.doctest_source / "doctest" / "doctest.h").is_file():
        print("--doctest-source must point to a local doctest checkout.", file=sys.stderr)
        return 2
    if not CLIENT.is_dir():
        print("Expected sibling repository: " + str(CLIENT), file=sys.stderr)
        return 2
    compiler = shutil.which(ns.cxx)
    if not compiler:
        print("C++ compiler not found: " + ns.cxx, file=sys.stderr)
        return 2

    metadata = {"server_head": git_head(SERVER), "client_head": git_head(CLIENT),
                "compiler": compiler, "build_root": str(root), "probes": {}}
    for name, repo, options in (
        ("server", SERVER, ["-DSA_WERROR=ON"]),
        ("client", CLIENT, ["-DSA_CLIENT_WERROR=ON",
                            "-DSA_SHARED_SOURCE_DIR=" + str(SERVER)]),
    ):
        build = root / name
        args = ["cmake", "-S", repo, "-B", build, "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
                "-DCMAKE_CXX_COMPILER=" + compiler,
                "-DFETCHCONTENT_SOURCE_DIR_DOCTEST=" + str(ns.doctest_source.resolve())] + options
        for phase, command in (
            ("configure", args),
            ("build", ["cmake", "--build", build, "--parallel", "4"]),
            ("ctest", ["ctest", "--test-dir", build, "--output-on-failure"]),
        ):
            rc, out = run(command, root / f"{name}-{phase}.log")
            print(f"{name} {phase}: exit={rc}", flush=True)
            if rc:
                print(out, file=sys.stderr)
                return 2

    includes = ["-I" + str(SERVER / "shared"),
                "-I" + str(SERVER / "idl/generated/cpp")]
    server_build, client_build = root / "server", root / "client"
    specs = [
        ("RulesProbe", "c++17", includes,
         [server_build / "shared/libsa_shared.a"], []),
        ("WorldProbe", "c++20", includes + [
            "-I" + str(SERVER / "src" / n / "include") for n in ("world", "net", "platform")],
         [server_build / "src/world/libsa_world.a", server_build / "src/net/libsa_net.a",
          server_build / "src/platform/libsa_platform.a", server_build / "shared/libsa_shared.a",
          server_build / "shared/libsa_wire.a"], []),
        ("ClientProbe", "c++17", includes + [
            "-I" + str(CLIENT / "src/net/include"), "-I" + str(CLIENT / "src/battle")],
         [client_build / "src/net/libsa_net.a", client_build / "src/battle/libsa_battle.a",
          client_build / "sa_shared/libsa_wire.a"], []),
        ("ClientBoundsProbe", "c++17", includes + ["-I" + str(CLIENT / "src/battle")],
         [CLIENT / "src/battle/BattlePresenter.cpp"],
         ["-fsanitize=address", "-fno-omit-frame-pointer", "-g"]),
    ]
    failing = 0
    for name, standard, inc, links, flags in specs:
        exe = root / name
        cmd = [compiler, "-std=" + standard, "-ffp-contract=off"] + flags + inc + [
            HERE / (name + ".cpp")] + links + ["-o", exe]
        rc, out = run(cmd, root / (name + "-build.log"))
        if rc:
            print(out, file=sys.stderr)
            return 2
        rc, out = run([exe], root / (name + ".log"))
        metadata["probes"][name] = {"exit_code": rc, "log": name + ".log"}
        failing += rc != 0
        print(f"{name}: exit={rc}", flush=True)
        for line in out.splitlines():
            if line.startswith(("FAIL", "PASS", "INFO", "OBSERVATION", "SUMMARY:")):
                print(line, flush=True)
    (root / "metadata.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
                                        encoding="utf-8")
    print("Logs: " + str(root), flush=True)
    return 1 if failing else 0


if __name__ == "__main__":
    raise SystemExit(main())
