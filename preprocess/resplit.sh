#!/bin/bash
set -e -o pipefail
BINDIR="$(dirname "$0")"
#Argument 1 is language
l="$1"
if [ ${#l} == 0 ]; then
  echo "Argument is language" 1>&2
  exit 1
fi
sed 's/^/<P>\n/' | $BINDIR/../moses/ems/support/split-sentences.perl -l $1 |fgrep -vx "<P>"
