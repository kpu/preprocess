#!/bin/bash
. "$(dirname "$0")"/../vars
diff <("$BIN"/cache cat <"$CUR"/input) "$CUR"/input
diff <("$BIN"/cache -t " " -k 1 cat <"$CUR"/input) "$CUR"/space_expected

