#!/usr/bin/env bash
set -euo pipefail

site_dir=$(realpath "${1:?Usage: publish-docs.sh SITE_DIR [previews/pr-N]}")
destination=${2:-.}
if [[ ! -f "$site_dir/index.html" ]]; then
    echo "Missing built documentation: $site_dir/index.html" >&2
    exit 1
fi
if [[ "$destination" != . && ! "$destination" =~ ^previews/pr-[1-9][0-9]*$ ]]; then
    echo "Invalid preview destination: $destination" >&2
    exit 1
fi

# Keep publishing isolated from the source checkout and its generated site.
git fetch --no-tags origin gh-pages
publish_dir=$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/gh-pages.XXXXXXXX")
git worktree add --detach "$publish_dir" FETCH_HEAD
trap 'git worktree remove --force "$publish_dir"' EXIT

for attempt in {1..10}; do
    base=$(git -C "$publish_dir" rev-parse HEAD)
    target="$publish_dir/$destination"
    mkdir -p "$target"
    if [[ "$destination" == . ]]; then
        rsync -a --delete --exclude=/.git --exclude=/previews/ "$site_dir/" "$target/"
    else
        rsync -a --delete "$site_dir/" "$target/"
    fi
    touch "$publish_dir/.nojekyll"
    git -C "$publish_dir" add --all
    if git -C "$publish_dir" diff --cached --quiet; then
        echo "Documentation at $destination is already up to date"
        exit 0
    fi
    git -C "$publish_dir" -c 'user.name=github-actions[bot]' \
        -c 'user.email=41898282+github-actions[bot]@users.noreply.github.com' \
        commit -m "Deploy documentation to $destination"
    commit=$(git -C "$publish_dir" rev-parse HEAD)
    # Use the source checkout for network operations so actions/checkout's
    # checkout-specific credential configuration also applies to publishing.
    if git push origin "$commit:refs/heads/gh-pages"; then
        exit 0
    fi

    git fetch --no-tags origin gh-pages
    latest=$(git rev-parse FETCH_HEAD)
    if [[ "$latest" == "$base" ]]; then
        echo "Push failed without a concurrent gh-pages update" >&2
        exit 1
    fi

    # Reapply only this destination on the winning commit. Rebasing generated files
    # can conflict, and force-pushing would discard other deployments.
    git -C "$publish_dir" reset --hard "$latest"
    if (( attempt < 10 )); then
        echo "gh-pages changed during publishing; retrying ($attempt/10)"
        sleep "$((1 + RANDOM % 5))"
    fi
done

echo "Could not publish documentation after 10 concurrent updates" >&2
exit 1
