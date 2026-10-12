#!/usr/bin/env python3
"""Keep the generated firmware name header from showing up as a local edit.

PlatformIO runs this on Linux CI as well as on Windows. The previous script
passed a single Windows path string to Popen (so exec failed) and used
backslashes, which made `git update-index` fail and aborted the build.
"""

import subprocess
import sys

HEADER = "src/firmware-build-name.h"


def main():
    try:
        subprocess.run(
            ["git", "update-index", "--assume-unchanged", HEADER],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except OSError:
        # No git, or git cannot see the index (tarball builds). The header
        # is still generated; it just may show as modified.
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
