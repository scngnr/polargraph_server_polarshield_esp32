#!/usr/bin/env python3
"""Print the firmwareBuildName header line.

Stdout is redirected at the firmware header by the PlatformIO build flag.
`git describe` needs tags; shallow clones and repos without tags fall back
to a short hash so the build still produces a valid header.
"""

import subprocess
from datetime import datetime


def git_output(args):
    try:
        return subprocess.check_output(
            args, stderr=subprocess.DEVNULL, text=True
        ).strip()
    except (subprocess.CalledProcessError, OSError):
        return ""


def main():
    version = git_output(["git", "describe", "--tags", "--always"])
    if not version:
        version = "unknown"

    status = git_output(["git", "status", "-s", "-uno"])
    if status:
        change_count = len([line for line in status.splitlines() if line.strip()])
        stamp = datetime.now().strftime("%Y%m%d.%H%M%S")
        output = "{0}@{1}+{2}ch".format(version, stamp, change_count)
    else:
        output = version

    output = output.replace("\\", "\\\\").replace('"', '\\"')
    print('String firmwareBuildName="{0}";'.format(output))


if __name__ == "__main__":
    main()
