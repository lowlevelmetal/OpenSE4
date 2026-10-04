#!/usr/bin/env bash
# Adds the ARM Linux packages that CI built for a version tag to dist/, next to
# the packages tools/package_release.sh made here (docs/BUILDING.md, "Release
# packages"):
#
#   dist/OpenSE4-<version>-linux-aarch64.tar.gz
#   dist/OpenSE4-<version>-linux-armhf.tar.gz
#
# and writes dist/OpenSE4-<version>-SHA256SUMS.txt again over every archive of
# that version in dist/. The release workflow (.github/workflows/release.yml)
# builds them when the tag is pushed; this downloads its artifacts with the
# GitHub CLI (gh, logged in), after checking that the run succeeded and built
# the commit the tag names.
#
#   tools/fetch_arm_packages.sh v0.8.2

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

if [ $# -ne 1 ] || [[ "$1" != v* ]]; then
    echo "usage: $0 <version tag, e.g. v0.8.2>" >&2
    exit 2
fi
tag=$1
version=${tag#v}
command -v gh > /dev/null || { echo "The GitHub CLI (gh) is needed." >&2; exit 1; }

commit=$(git rev-parse --verify --quiet "$tag^{commit}") || { echo "No tag $tag here: fetch the tags first." >&2; exit 1; }

# The newest run of the release workflow for the tag.
read -r run status conclusion sha < <(gh run list --workflow release.yml --branch "$tag" --limit 1 \
    --json databaseId,status,conclusion,headSha --jq '.[0] | "\(.databaseId) \(.status) \(.conclusion) \(.headSha)"' 2> /dev/null) ||
    { echo "No run of the release workflow for $tag." >&2; exit 1; }
if [ "$status" != completed ] || [ "$conclusion" != success ]; then
    echo "The release workflow's run $run for $tag is $status ($conclusion): wait for it, or look at gh run view $run." >&2
    exit 1
fi
if [ "$sha" != "$commit" ]; then
    echo "Run $run built $sha, but $tag is $commit." >&2
    exit 1
fi

dist="$root/dist"
mkdir -p "$dist"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
echo "==> downloading the ARM packages of run $run ($tag)"
gh run download "$run" --name package-linux-aarch64 --name package-linux-armhf --dir "$tmp"
for arch in aarch64 armhf; do
    file="OpenSE4-$version-linux-$arch.tar.gz"
    found=$(find "$tmp" -name "$file" -print -quit)
    [ -n "$found" ] || { echo "Run $run has no $file." >&2; exit 1; }
    tar -tzf "$found" > /dev/null
    mv "$found" "$dist/$file"
    echo "    $dist/$file"
done

# The checksums, as tools/package_release.sh writes them.
(cd "$dist" && sha256sum OpenSE4-"$version"-*.tar.gz OpenSE4-"$version"-*.zip OpenSE4-"$version"-*-setup.exe 2> /dev/null > "OpenSE4-$version-SHA256SUMS.txt" || true)
echo "==> checksums: $dist/OpenSE4-$version-SHA256SUMS.txt"
cat "$dist/OpenSE4-$version-SHA256SUMS.txt"
