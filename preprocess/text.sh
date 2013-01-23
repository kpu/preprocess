#!/bin/bash
set -e -o pipefail
BINDIR="$(dirname "$0")"
#Argument 1 is language, argument 2 is lowercase (1) or not (0)
l="$1"
if [ ${#l} != 2 ]; then
  echo "Usage: \"$0 language lowercase\" where lowercase is 0 or 1." 1>&2
  exit 1
fi
if [ "$2" != 1 ] && [ "$2" != 0 ]; then
  echo "Second argument (lowercase) should be 0 or 1" 1>&2
  exit 1
fi
#If statement hack to only run process unicode if lowercasing.
"$BINDIR"/process_unicode --language $l --flatten --normalize |"$BINDIR"/moses/tokenizer/tokenizer.perl -l $l | "$BINDIR"/heuristics.perl -l $l | if [ "$2" == 1 ]; then
  "$BINDIR"/moses/normalize-punctuation.perl $l | "$BINDIR"/process_unicode --language $l --lower
else
  "$BINDIR"/moses/normalize-punctuation.perl $l
fi
