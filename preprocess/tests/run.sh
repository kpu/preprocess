#!/bin/bash
CURRENT="$(dirname "$0")"
set -eo pipefail
for i in "$CURRENT"/*/; do
  "${i}"run.sh || echo "FAILURE: ${i}" 1>&2
done
