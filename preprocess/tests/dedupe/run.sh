#!/bin/bash
. "$(dirname "$0")"/../vars
diff <("$BIN/dedupe" <"$CUR/input") "$CUR/expected"
"$BIN"/dedupe "$CUR"/input <(rev "$CUR"/input) "$TMP"/output0 "$TMP"/output1
diff "$CUR"/expected "$TMP"/output0
diff <(rev "$CUR"/expected) "$TMP"/output1
rm "$TMP"/output0 "$TMP"/output1
