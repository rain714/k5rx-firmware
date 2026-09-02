#!/bin/sh
# Backward-compatible wrapper. The public K5RX build entry point is build.sh.
# No global Docker/container prune or unrelated image/volume removal is performed.
set -eu

case "${1:-k5rx}" in
  k5rx|K5RX)
    exec env BUILD_PROFILE=K5RX sh "$(dirname "$0")/build.sh"
    ;;
  default|DEFAULT)
    exec env BUILD_PROFILE=DEFAULT sh "$(dirname "$0")/build.sh"
    ;;
  *)
    echo "Usage: $0 [k5rx|default]" >&2
    exit 1
    ;;
esac
