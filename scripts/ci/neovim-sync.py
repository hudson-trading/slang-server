#!/usr/bin/env python3

import argparse
import logging
import os
import re
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

from git import Repo
from git.util import Actor

logger = logging.getLogger()

ROCKSPEC_PATTERN = re.compile(
    r"^slang-server\.nvim-(?P<version>\d+(?:\.\d+)*)-\d+\.rockspec$"
)


def rockspec_version(directory, *, required=True):
    rockspecs = list(Path(directory).glob("slang-server.nvim-*.rockspec"))
    if len(rockspecs) != 1:
        raise ValueError(
            f"Expected one rockspec in {directory}, found {len(rockspecs)}"
        )
    rockspec = rockspecs[0]
    match = ROCKSPEC_PATTERN.match(rockspec.name)
    if not match:
        if required:
            raise ValueError(f"Invalid rockspec filename: {rockspec.name}")
        return None
    return match.group("version")


def main():
    parser = argparse.ArgumentParser(
        description="Synchronize the Neovim plugin mirror repo"
    )

    parser.add_argument(
        "--event", required=True, help="GH workflow event name (push | pull_request)"
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="Don't actually push to mirror repo"
    )

    args = parser.parse_args()

    branch = "main" if args.event == "push" else "sync-test"

    this_repo = Repo(os.getcwd())
    this_commit = this_repo.head.object
    commit_message = f"""\
{this_commit.message}

From:
https://github.com/hudson-trading/slang-server/commit/{this_commit}
"""
    logging.warning("Commit message:")
    logging.warning(commit_message)
    author = Actor("Hudson River Trading", "opensource@hudson-trading.com")

    gh_pat = os.environ.get("GH_PAT")
    if not gh_pat and not args.dry_run:
        sys.exit("GH_PAT is required for non-dry-run sync")

    auth = f"x-access-token:{gh_pat}@" if gh_pat else ""
    clone_url = f"https://{auth}github.com/hudson-trading/slang-server.nvim.git"

    temp_dir = TemporaryDirectory()
    plugin_repo = Repo.clone_from(clone_url, temp_dir.name)
    plugin_remote = plugin_repo.remote()
    source_version = rockspec_version("clients/neovim")
    mirrored_version = rockspec_version(temp_dir.name, required=False)
    version = source_version if source_version != mirrored_version else None

    branch_ref = f"origin/{branch}"
    if branch == "main":
        pass
    elif branch_ref in plugin_repo.refs:
        plugin_repo.git.checkout(branch_ref)
    else:
        plugin_repo.git.checkout("-b", branch)

    subprocess.run(
        [
            "rsync",
            "-av",
            "--delete",
            "--exclude",
            ".git",
            "clients/neovim/",
            f"{temp_dir.name}/",
        ],
        check=True,
    )

    plugin_repo.index.add("*")
    if not plugin_repo.is_dirty():
        logging.warning("No changes, not committing or pushing")
        return

    plugin_repo.index.commit(commit_message, author=author)

    tag = None
    if version and branch == "main":
        tag = f"v{version}"
        logging.warning(f"Rockspec version changed; creating tag {tag}")
        plugin_repo.create_tag(tag)

    if args.dry_run:
        logging.warning("Dry run, not pushing")
        return

    logging.warning(f"Pushing to {branch}")
    refspecs = [f"HEAD:{branch}"]
    if tag:
        refspecs.append(f"refs/tags/{tag}:refs/tags/{tag}")
    plugin_remote.push(refspecs)


if __name__ == "__main__":
    sys.exit(main())
