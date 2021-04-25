#!/bin/bash
. "$(dirname "$0")"/../vars
#GPL has short columns
diff <("$BIN/foldfilter" cat <"$CUR"/input) "$CUR"/input
diff <("$BIN/foldfilter" -w 10 cat <"$CUR"/input) "$CUR"/input
"$BIN/foldfilter" -w 10 tee "$TMP/fold10" <"$CUR"/input >/dev/null
# Line breaks are not great with leading space but it does work
diff "$TMP/fold10" "$CUR/fold10.expected"
rm "$TMP/fold10"
