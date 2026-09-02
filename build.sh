#!/bin/sh
set -eu

PROFILE="${BUILD_PROFILE:-K5RX}"
VERSION="${K5RX_VERSION:-dev}"
IMAGE="${BUILD_IMAGE:-k5rx-build}"
OUT_DIR="${OUT_DIR:-compiled-firmware}"
RUNTIME="${CONTAINER_RUNTIME:-auto}"

case "$RUNTIME" in
  auto)
    if command -v container >/dev/null 2>&1; then
      RUNTIME=container
    elif command -v docker >/dev/null 2>&1; then
      RUNTIME=docker
    else
      echo "error: neither Apple 'container' nor Docker was found" >&2
      exit 1
    fi
    ;;
  container|docker) ;;
  *)
    echo "error: CONTAINER_RUNTIME must be auto, container, or docker" >&2
    exit 1
    ;;
esac

case "$PROFILE" in
  K5RX|DEFAULT) ;;
  *)
    echo "error: BUILD_PROFILE must be K5RX or DEFAULT" >&2
    exit 1
    ;;
esac

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$ROOT/$OUT_DIR"
rm -f "$ROOT/$OUT_DIR"/k5rx-firmware.bin \
      "$ROOT/$OUT_DIR"/k5rx-firmware.packed.bin \
      "$ROOT/$OUT_DIR"/SHA256SUMS

echo "==> Building image '$IMAGE' with $RUNTIME"
"$RUNTIME" build -t "$IMAGE" "$ROOT"

echo "==> Building profile $PROFILE"
"$RUNTIME" run --rm --network none \
  -v "$ROOT:/work:rw" \
  --workdir /work \
  "$IMAGE" \
  sh -c "make clean all BUILD_PROFILE='$PROFILE' K5RX_VERSION='$VERSION'"

if [ "$PROFILE" = K5RX ]; then
  cp "$ROOT/f4hwn.bin" "$ROOT/$OUT_DIR/k5rx-firmware.bin"
  cp "$ROOT/f4hwn.packed.bin" "$ROOT/$OUT_DIR/k5rx-firmware.packed.bin"

  (
    cd "$ROOT/$OUT_DIR"
    if command -v shasum >/dev/null 2>&1; then
      shasum -a 256 k5rx-firmware.bin k5rx-firmware.packed.bin > SHA256SUMS
    elif command -v sha256sum >/dev/null 2>&1; then
      sha256sum k5rx-firmware.bin k5rx-firmware.packed.bin > SHA256SUMS
    else
      echo "error: shasum or sha256sum is required" >&2
      exit 1
    fi
  )

  echo "==> Artifacts"
  cat "$ROOT/$OUT_DIR/SHA256SUMS"
else
  echo "==> DEFAULT regression build completed; release artifacts were not exported"
fi
