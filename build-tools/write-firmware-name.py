#!/usr/bin/env python3
"""Generate src/firmware-build-name.h for the PlatformIO build.

Invoked as a PlatformIO `!` build flag. Stdout must stay empty: PlatformIO
treats that stdout as extra compiler flags. The header is written directly
so the command does not depend on shell redirection.
"""

import importlib.util
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
HEADER = os.path.join(ROOT, "src", "firmware-build-name.h")


def main():
    os.chdir(ROOT)
    # Best-effort. A missing index or a tarball checkout must not fail the build.
    try:
        subprocess.run(
            ["git", "update-index", "--assume-unchanged", "src/firmware-build-name.h"],
            cwd=ROOT,
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except OSError:
        pass

    # The generator's filename has a hyphen, so it is not a normal import.
    generator_path = os.path.join(os.path.dirname(__file__), "gen-build-name.py")
    spec = importlib.util.spec_from_file_location("gen_build_name", generator_path)
    gen_build_name = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gen_build_name)

    # gen_build_name prints the header line. Capture it instead of letting
    # PlatformIO consume it as build flags.
    from io import StringIO

    buffer = StringIO()
    saved = sys.stdout
    try:
        sys.stdout = buffer
        gen_build_name.main()
    finally:
        sys.stdout = saved

    line = buffer.getvalue()
    if not line.endswith("\n"):
        line += "\n"
    with open(HEADER, "w", encoding="ascii") as handle:
        handle.write(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
