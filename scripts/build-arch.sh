#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output_dir="$project_dir/build/arch"
pkgver=$(sed -n 's/^project(KPlayer VERSION \([^ ]*\) LANGUAGES CXX)$/\1/p' "$project_dir/CMakeLists.txt")
if [[ -z "$pkgver" ]]; then
    echo 'Cannot determine the KPlayer version from CMakeLists.txt.' >&2
    exit 1
fi
command -v makepkg >/dev/null || { echo 'This script requires Arch Linux makepkg.' >&2; exit 1; }

mkdir -p "$output_dir"
archive="kplayer-${pkgver}.tar.gz"
tar -czf "$output_dir/$archive" \
    --transform="s,^,kplayer-${pkgver}/," \
    -C "$project_dir" CMakeLists.txt README.md src qml translations assets/icons cmake packaging/linux
checksum=$(sha256sum "$output_dir/$archive")
checksum=${checksum%% *}
sed -e "s/^pkgver=.*/pkgver=$pkgver/" \
    -e "s/^sha256sums=.*/sha256sums=('$checksum')/" \
    "$project_dir/packaging/arch/PKGBUILD" > "$output_dir/PKGBUILD"

cd "$output_dir"
makepkg --cleanbuild --force "$@"
printf '\nPackage output: %s\n' "$output_dir"
