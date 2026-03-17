#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 || $# -gt 3 ]]; then
    echo "usage: $0 <sdk-prefix> [arch] [deployment-target]" >&2
    exit 64
fi

sdk_prefix_input=$1
sdk_arch=${2:-$(uname -m)}
deployment_target=${3:-}

case "$sdk_arch" in
    arm64)
        : "${deployment_target:=11.0}"
        ;;
    x86_64)
        : "${deployment_target:=10.13}"
        ;;
    *)
        echo "unsupported macOS architecture: $sdk_arch" >&2
        exit 65
        ;;
esac

ffmpeg_version=8.0.1
ffmpeg_archive="ffmpeg-${ffmpeg_version}.tar.xz"
ffmpeg_url="https://ffmpeg.org/releases/${ffmpeg_archive}"
ffmpeg_sha256="05ee0b03119b45c0bdb4df654b96802e909e0a752f72e4fe3794f487229e5a41"

mkdir -p "$(dirname "$sdk_prefix_input")"
sdk_prefix=$(cd "$(dirname "$sdk_prefix_input")" && pwd)/$(basename "$sdk_prefix_input")
cache_root=$(dirname "$sdk_prefix")
archive_path="${cache_root}/${ffmpeg_archive}"
marker_path="${sdk_prefix}/ARCANUM_FFMPEG_SDK.txt"

mkdir -p "$cache_root"

if [[ -f "$marker_path" ]] \
    && grep -qx "version=${ffmpeg_version}" "$marker_path" \
    && grep -qx "arch=${sdk_arch}" "$marker_path" \
    && grep -qx "deployment_target=${deployment_target}" "$marker_path" \
    && [[ -f "${sdk_prefix}/include/libavcodec/avcodec.h" ]] \
    && [[ -f "${sdk_prefix}/lib/libavcodec.dylib" ]]; then
    echo "Using cached FFmpeg macOS SDK at ${sdk_prefix}"
    exit 0
fi

if [[ ! -f "$archive_path" ]]; then
    curl -L -o "$archive_path" "$ffmpeg_url"
fi

archive_sha256=$(shasum -a 256 "$archive_path" | awk '{print $1}')
if [[ "$archive_sha256" != "$ffmpeg_sha256" ]]; then
    echo "FFmpeg archive checksum mismatch: expected ${ffmpeg_sha256}, got ${archive_sha256}" >&2
    exit 66
fi

work_root=$(mktemp -d "${TMPDIR:-/tmp}/arcanum-ffmpeg-sdk.XXXXXX")
cleanup() {
    rm -rf "$work_root"
}
trap cleanup EXIT

tar -xf "$archive_path" -C "$work_root"

src_root="${work_root}/ffmpeg-${ffmpeg_version}"
build_root="${work_root}/build"
install_root="${work_root}/install"

mkdir -p "$build_root"
cd "$build_root"

extra_cflags="-arch ${sdk_arch} -mmacosx-version-min=${deployment_target}"
extra_ldflags="-arch ${sdk_arch} -mmacosx-version-min=${deployment_target}"

"${src_root}/configure" \
    --prefix="$install_root" \
    --cc=clang \
    --disable-programs \
    --disable-doc \
    --disable-static \
    --enable-shared \
    --disable-network \
    --disable-autodetect \
    --disable-avdevice \
    --disable-avfilter \
    --disable-postproc \
    --extra-cflags="$extra_cflags" \
    --extra-ldflags="$extra_ldflags"

make -j"$(sysctl -n hw.logicalcpu)"
make install

rm -rf "$sdk_prefix"
mkdir -p "$sdk_prefix"
cp -R "$install_root"/. "$sdk_prefix"/

cat >"$marker_path" <<EOF
version=${ffmpeg_version}
arch=${sdk_arch}
deployment_target=${deployment_target}
source=${ffmpeg_url}
sha256=${ffmpeg_sha256}
EOF

echo "Built FFmpeg macOS SDK at ${sdk_prefix}"
