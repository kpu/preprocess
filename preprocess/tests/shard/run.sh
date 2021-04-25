#!/bin/bash
. "$(dirname "$0")"/../vars
"$BIN/shard" "$TMP"/test_a "$TMP"/test_b <"$CUR"/input
diff <(sort "$TMP"/test_a "$TMP"/test_b) <(sort "$CUR"/input)
"$BIN/shard" --prefix "$TMP"/test --number 4 <"$CUR"/input
diff <(sort "$TMP"/test{0,1,2,3}) <(sort "$CUR"/input)
"$BIN/shard" --prefix "$TMP"/test -c gzip --number 4 <"$CUR"/input
diff <(zcat "$TMP"/test{0,1,2,3} |sort) <(sort "$CUR"/input)
"$BIN/shard" --prefix "$TMP"/test -c bzip2 --number 4 <"$CUR"/input
diff <(bzcat "$TMP"/test{0,1,2,3} |sort) <(sort "$CUR"/input)
rm "$TMP"/test_a "$TMP"/test_b "$TMP"/test{0,1,2,3}
