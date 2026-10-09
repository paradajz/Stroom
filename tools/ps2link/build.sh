#!/bin/sh
# Build the pinned launcher against the selected complete SDK installation.
set -eu

while [ "$#" -gt 0 ]
do
    case "$1" in
        --source-dir)
            source_dir=$2
            shift 2
            ;;

        --build-dir)
            build_dir=$2
            shift 2
            ;;

        --sdk-dir)
            sdk_root=$2
            shift 2
            ;;

        --variant)
            variant=$2
            shift 2
            ;;

        *)
            printf 'Unknown option: %s\n' "$1" >&2
            exit 1
            ;;
    esac
done

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
install_root="$sdk_root/$variant"

. "$install_root/environment.sh"

revision=$(git -C "$source_dir" rev-parse --verify 'HEAD^{commit}')
version=$(printf '%.8s-%s' "$revision" "$variant")

mkdir -p "$build_dir"
build_dir=$(cd "$build_dir" && pwd)
work_dir=$(mktemp -d "$build_dir/source.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT HUP INT TERM

git -C "$source_dir" archive \
    --format=tar --output="$work_dir/source.tar" "$revision"
tar -xf "$work_dir/source.tar" -C "$work_dir"

# Build the SDK text screen locally so normal and exception screens consume the
# same display profile as the player, without changing the installed SDK.
git -C "$repo_root/third_party/ps2sdk" show HEAD:ee/debug/src/scr_printf.c > "$work_dir/ee/debug_screen.c"
patch -d "$work_dir" -p1 --batch --forward -i "$repo_root/patches/ps2link/display.patch"
patch -d "$work_dir" -p1 --batch --forward -i "$repo_root/patches/ps2link/no-cd-stop.patch"
cp "$repo_root/tools/ps2link/display_memory.c" "$work_dir/ee/display_memory.c"
node "$repo_root/tools/contracts/generate.mjs" "$work_dir/generated/contracts"

make -C "$work_dir" APP_VERSION="$version" STROOM_ROOT="$repo_root"
cp "$work_dir/bin/PS2LINK.ELF" "$build_dir/PS2LINK.ELF"
cp "$work_dir/ee/ps2link.map" "$build_dir/ps2link.map"

printf 'Built %s/PS2LINK.ELF (%s, source %s)\n' "$build_dir" "$version" "$revision"
