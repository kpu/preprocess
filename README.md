preprocess
==========

Pipelines for preprocessing corpora.  

Compile with bjam.

```
bin/text.sh $language $lower
```
does all the tokenization and normalization for normal text that has already
been extracted and sentence split.

```
bin/gigaword_unwrap
```
takes Gigaword XML files on stdin and outputs text with P tags intended
to be used as input to the sentence splitter.  Also removes or normalizes many
ad-hoc parenthesized expressions like (UNDERLINE) and consecutive duplicate
lines.

```
bin/moses/ems/support/split-sentences.perl -l $language
```
is the Moses/Europarl sentence splitter with a bugfix to also split sentences
separated by two spaces. 

```
bin/resplit.sh $language
```
preserves existing line breaks and introduces additional breaks when multiple sentences appear in the same line.  This is useful when you want to use the target side of parallel corpora for language modeling.  


```
bin/gigaword_extract.sh $language
```
combines the unwrap and sentence split steps.

```
bin/dedupe
```
deduplicates text at the line level.

```
bin/remove_long_lines
```
removes lines longer than 2000 characters. 

```
bin/select_latin
```
Removes lines that contain bad UTF8; contain control characters; have less than
90% Latin, Common, or Inherited characters (except angle brackets); or have less
than 50% Latin characters.  I used this for giga-fren.  

```
bin/process_unicode -l $language [--flatten] [--normalize] [--lower]
```
Processes UTF8.

* --lower lowercases
* --normalize applies the ICU normalization function
* --flatten applies a bunch of substitutions for punctuation

```
bin/heuristics.perl -l $language
```
A collection of substitution heuristics from various people.

```
bin/moses/tokenizer/tokenizer.perl -l $language
```
The Moses tokenizer.
