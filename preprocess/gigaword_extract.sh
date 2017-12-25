#!/bin/bash
#Extract sentences from gigaword but don't process them
set -e -o pipefail
BINDIR="$(dirname "$0")"
if [ ${#1} != 2 ]; then
  echo "Expected language on the command line." 1>&2
  exit 1
fi
$BINDIR/gigaword_unwrap | $BINDIR/../moses/ems/support/split-sentences.perl -l $1 |fgrep -v "<P>"
