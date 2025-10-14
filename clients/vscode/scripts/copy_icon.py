#! /usr/bin/env python3
import subprocess
import sys

# See icons at https://code.visualstudio.com/api/references/icons-in-labels
# Clone repo at https://github.com/microsoft/vscode-icons/tree/main/icons/light and VSCODE_ICON_REPO to the path


def main():
    icon = sys.argv[1]
    for scheme in ["light", "dark"]:
        subprocess.run(
            f"cp $VSCODE_ICON_REPO/icons/{scheme}/{icon}.svg clients/vscode/resources/{scheme}/",
            shell=True,
        )


if __name__ == "__main__":
    main()
