#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import shlex
import subprocess
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build and run the standalone ECS benchmark.",
    )
    parser.add_argument("--compiler", default=os.environ.get("CXX", "c++"), help="C++ compiler to use.")
    parser.add_argument(
        "--build-dir",
        default="build/benchmarks",
        help="Directory used for the compiled benchmark binary.",
    )
    parser.add_argument("--skip-build", action="store_true", help="Run the existing benchmark binary without rebuilding.")
    parser.add_argument("--build-only", action="store_true", help="Build the benchmark and exit.")
    parser.add_argument("--entities", type=int, default=100_000, help="Entity count passed to the benchmark.")
    parser.add_argument("--frames", type=int, default=500, help="Frame count passed to the benchmark.")
    parser.add_argument("--iterations", type=int, default=5, help="Measured iterations passed to the benchmark.")
    parser.add_argument("--warmup", type=int, default=1, help="Warmup iterations passed to the benchmark.")
    return parser.parse_args()


def command_display(command: list[str]) -> str:
    return shlex.join(command)


def run(command: list[str]) -> None:
    print(f"+ {command_display(command)}", flush=True)
    subprocess.run(command, check=True)


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[1]
    build_dir = (repo_root / args.build_dir).resolve()
    build_dir.mkdir(parents=True, exist_ok=True)

    suffix = ".exe" if os.name == "nt" else ""
    output = build_dir / f"ecs_benchmark{suffix}"

    benchmark_sources = [
        repo_root / "benchmarks/ecs_benchmark.cpp",
        repo_root / "src/engine/core/ecs/entity.cpp",
        repo_root / "src/engine/core/ecs/entity_manager.cpp",
        repo_root / "src/engine/core/ecs/system_manager.cpp",
    ]

    if not args.skip_build:
        compile_command = [
            args.compiler,
            "-std=c++17",
            "-O3",
            "-DNDEBUG",
            "-I",
            str(repo_root / "src"),
            *[str(source) for source in benchmark_sources],
            "-o",
            str(output),
        ]
        run(compile_command)

    if args.build_only:
        print(f"built benchmark: {output}")
        return 0

    if not output.exists():
        raise FileNotFoundError(f"benchmark binary not found: {output}")

    benchmark_command = [
        str(output),
        "--entities",
        str(args.entities),
        "--frames",
        str(args.frames),
        "--iterations",
        str(args.iterations),
        "--warmup",
        str(args.warmup),
    ]
    run(benchmark_command)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        print(f"command failed with exit code {error.returncode}", file=sys.stderr)
        raise SystemExit(error.returncode)
