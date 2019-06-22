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


