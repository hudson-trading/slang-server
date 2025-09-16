#! /usr/bin/env python3
import subprocess
import sys


def main():
    icon = sys.argv[1]
    for scheme in ["light", "dark"]:
        subprocess.run(
            f"cp $VSCODE_ICON_REPO/icons/{scheme}/{icon}.svg resources/{scheme}/",
            shell=True,
        )


if __name__ == "__main__":
    main()
