preprocess
==========

Pipelines for preprocessing corpora.

Compile with cmake:
```bash
mkdir build
cd build
cmake ..
make -j4
```

Paths are relative to the build directory.

```bash
bin/text.sh $language $lower
```
does all the tokenization and normalization for normal text that has already
been extracted and sentence split.

```bash
bin/gigaword_unwrap
```
takes Gigaword XML files on stdin and outputs text with P tags intended
to be used as input to the sentence splitter.  Also removes or normalizes many
ad-hoc parenthesized expressions like (UNDERLINE) and consecutive duplicate
lines.

```bash
moses/ems/support/split-sentences.perl -l $language
```
is the Moses/Europarl sentence splitter with a bugfix to also split sentences
separated by two spaces.

```bash
bin/resplit.sh $language
```
preserves existing line breaks and introduces additional breaks when multiple sentences appear in the same line.  This is useful when you want to use the target side of parallel corpora for language modeling.


```bash
bin/gigaword_extract.sh $language
```
combines the unwrap and sentence split steps.

```bash
bin/dedupe
```
deduplicates text at the line level.

```bash
bin/cache slow_program slow_program_args...
```
Wraps a deterministic line-based program with a deduplicating cache.  The program should always write exactly one line for each line it reads.
Example input:
```
Repeated line
Some text
Repeated line
More text
```
The slow program will only see unique lines in its input:
```
Repeated line
Some text
More text
```
Suppose the slow program translates to French:
```
Ligne répétée
Du texte
Plus de texte
```
Then `cache` will reinsert the cached results for final output:
```
Ligne répétée
Du texte
Ligne répétée
Plus de texte
```

```bash
bin/shard $prefix $shard_count
```
Shards stdin into multiple files named prefix0 prefix1 prefix2 etc.  This is useful when the deduper above runs out of memory.

```bash
bin/remove_long_lines $length_limit
```
removes lines longer than the specified length in bytes.  The default is 2000 bytes.

```bash
bin/remove_invalid_utf8
```
removes lines with invalid UTF-8

```bash
bin/select_latin
```
Removes lines that contain bad UTF8; contain control characters; have less than
90% Latin, Common, or Inherited characters (except angle brackets); or have less
than 50% Latin characters.  I used this for giga-fren.

```bash
bin/process_unicode -l $language [--flatten] [--normalize] [--lower]
```
Processes UTF8.

* --lower lowercases
* --normalize applies the ICU normalization function
* --flatten applies a bunch of substitutions for punctuation

```bash
bin/heuristics.perl -l $language
```
A collection of substitution heuristics from various people.

```bash
moses/tokenizer/tokenizer.perl -l $language
```
The Moses tokenizer.

```bash
bin/truecase --model $model
```
is a fast reimplementation of the Moses `truecase.perl` script.  It does not support factors.

```bash
xzcat $language.*.raw.xz |commoncrawl_dedupe /dev/null |xz >$language.deduped.xz
```
Process the CommonCrawl n-grams raw files into the deduped files:
* Remove lines beginning with df6fa1abb58549287111ba8d776733e9 (these mark document boundaries)
* Strip leading and trailing whitespace
* Deduplicate, preserving the first instance of the line
* Remove any lines with invalid UTF-8

```bash
zcat sentences.gz | b64filter program program-args | gzip -c > processed.gz
```
Wraps a program that is not able to ingest base64 documents, but is able to
process lines of text in such a way that it will always output an equal amount
of lines as went into it. For example an MT system, or a tokenizer.

```bash
< long_sentences.txt foldfilter -w 1000 translate.sh > long_english_sentences.txt
```

Think of it as a wrapper version of [fold](https://linux.die.net/man/1/fold).

Wrap an MT system that does not like long sentences and this tool chops those
lines (assuming each line is a single sentence) temporarily into multiple lines.
It uses some heuristics to determine where to break up lines, but if it can't
find a good break point it will still just chop words in half.


```
Usage: foldfilter [ -w INT ] [ -d DELIMITERS ] [ -s ] command [ args ... ]

Arguments:
  -w INT         Split input lines into lines of at most INT bytes. Default: 80
  -d DELIMITERS  Use these delimiters instead of the default set `:, -./`. Note
                 that the order specified here is also the order of preference
                 when breaking a line. The first delimiter of this set that
                 breaks the line into a width smaller/equal to the wanted width
                 will be selected, not the last! I.e. with the default set it
                 will prefer to break on white space instead of breaking up a
                 word with a dash in it. The default set assumes the input is
                 already split into a single sentence per line, and `.` and `/`
                 are mainly here to break up URIs.
  -s             Skip sending the delimiters to the wrapped command. If added,
                 delimiters around the breaking point (both before and after)
                 will be pass directly to the output, and not to the wrapped
                 command. Useful if you do not trust the wrapped program to not
                 trim them off. Delimiters inside lines, i.e. that are not at
                 the beginning or end of a line are always sent.
```

The program's exit code is that of the wrapped command, or 1 if the arguments
could not be interpreted or an error occurred, i.e. invalid utf8 was passed in.

**utf8 safe:** This tool won't break up unicode characters, and you can use
unicode characters as delimiters.

```bash
docenc -d $INDICES sentences.gz
```

Bit like b64filter, but split into a decoding and an encoding process. Can be
helpful constructing or debugging base64 encoded document collections. Accepts
gzip-encoded input.

```
Usage: docenc [ -d ] [ -0 ] [ -q | -v ] [ -n ] [ index ... ] [ file ... ]

The indexes can be specified as a single index or a range in the form INT-INT.
You can specify multiple indices or ranges at once. The rest of the arguments
are interpreted as files to encode/decode.

Arguments:
  -d      Decode mode: convert base64 into plain text. If not specified it will
          default to encode mode.
  -0      Use null byte instead of double newline as document separator.
  -q      Do not complain if the document separator is encountered in the output
          while decoding.
  -v      Print how many documents were encoded/decoded to stderr.
  -n      When decoding, prefix each line with the document index.

Modes:
  encode  Interpret the input as plain text documents that need to be base64
          encoded. The document separator (double newline) will cause a new
          document to be generated.
  decode  Interpret each line as a base64 encoded document that needs to be
          converted to plain text. The plain text will have double newlines
          (or null bytes) to indicate a document has ended and the next one
          begins.
```

I.e this will behave similarly:
```
docenc -d plain_text.gz | tr '[:lower:]' '[:upper:]' | docenc | gzip -c > loud_text.gz
```
and
```
gzip -cd plain_text.gz | b64filter tr '[:lower:]' '[:upper:]' | docenc | gzip -c > loud_text.gz
```

Pretty similar to what can be achieved with something like the following set
on a Linux machine using GNU base64 (Note: macOS base64 will not accept multiple
lines):

```
zcat $@ | head -nX | tail -nY | base64 -d
```

Mostly useful for debugging, but can also be useful to convert plain text into
base64-encoded documents, i.e. converting single line html into multi-line
documents:
```
cat lines_of_html.tsv \
  | cut -d$'\t' -f2 \
  | sed -r 's/$/\x0/g' \
  | sed -r 's/<br\/>|<\/p><p>/\n/g' \
  | sed -r 's/<\/?p>//g' \
  | docenc -0 \
  > sentences.gz
```