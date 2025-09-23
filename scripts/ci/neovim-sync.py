#!/usr/bin/env python3

import argparse
import logging
import os
import subprocess
import sys

from git import Repo
from git.util import Actor
from tempfile import TemporaryDirectory

logger = logging.getLogger()


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

    temp_dir = TemporaryDirectory()
    plugin_repo = Repo.clone_from(
        f"https://x-access-token:{os.environ['GH_PAT']}@github.com/hudson-trading/slang-server.nvim.git",
        temp_dir.name,
    )
    plugin_remote = plugin_repo.remote()
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

    if args.dry_run:
        logging.warning("Dry run, not pushing")
        return

    logging.warning(f"Pushing to {branch}")
    plugin_remote.push(f"HEAD:{branch}")


if __name__ == "__main__":
    sys.exit(main())
