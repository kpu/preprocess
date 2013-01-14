#!/usr/bin/env perl
#More preprocessing.  This assumes that avenue/util/process_unicode is run with at minimum --flatten 1 --normalize 1 first.  

use strict;
use utf8;

binmode STDIN, ":utf8";
binmode STDOUT, ":utf8";
binmode STDERR, ":utf8";

my $language = "en";

while (@ARGV) {
  $_ = shift;
  /^-l$/ && ($language = shift, next);
}

while(my $eline = <STDIN>)
{
  chomp $eline;
  $eline = " $eline ";

  #Normalize long chains of underscores to just two.
  $eline =~ s/_\s*_[\s_]*/ __ /g;

  #Silja dropped * entirely.  I keep one.  Bullet points are converted to * by a Chris Dyer rule in process_unicode.   
  $eline =~ s/\*\s*\*[\s\*]*/ * /g;
  #Silja, originally for prepgigaword-silja.pl
  $eline =~ s/#+//g;
  $eline =~ s/[\!]+/!/g;
  $eline =~ s/!([^ ])/! $1/g;
  $eline =~ s/\.([^\s\d.])/. $1/g;
  $eline =~ s/\+(\D)/+ $1/g;
  $eline =~ s/(\D)\+/$1 +/g;
  $eline =~ s/,(\D)/, $1/g;
  $eline =~ s/(\s)-([^\s\d\-])/$1- $2/g;
  $eline =~ s/^ *-- *//g;
  #The next rule was botching ellipses. . . 
  #$eline =~ s/\.\./ . /g;

  #Greg
  #Gigaword apw does this.  
  $eline =~ s/ dlrs / \$ /g;
  if ($language == "fr") {
    $eline =~ s/([^ -]+)-t-(je|j'|tu|il|elle|on|nous|vous|ils|elles|me|m'|te|t'|le|l'|la|les|lui|leur|moi|toi|eux|elles|ce|c'|ça|ceci|cela|qui|ci|là) /\1 -t-\2 /gi;
    $eline =~ s/([^ -]+)-(je|j'|tu|il|elle|on|nous|vous|ils|elles|me|m'|te|t'|le|l'|la|les|lui|leur|moi|toi|eux|elles|ce|c'|ça|ceci|cela|qui|ci|là) /\1 -\2 /gi;
    $eline =~ s/\s+(qu|c|d|l|j|s|n|m|lorsqu|puisqu)\s+'\s+/ \1' /gi;
    $eline =~ s/\s+aujourd\s*'\s*hui\s+/ aujourd'hui /gi;
  }

  #Chris Dyer, t2.perl
  if ($language == "en") {
    $eline =~ s/ élite / elite /gi;
    $eline =~ s/ (s|at) & (t|p) / $1&$2 /ig;
    $eline =~ s/ (full|half|part) - (time) / $1-$2 /ig;
    $eline =~ s/ (vis|viz) - (.|..) - (vis|viz) / vis-à-vis /ig;
    $eline =~ s/ (short|long|medium|one|half|two|on|off|in|post|ex|multi|de|mid|co|inter|intra|anti|re|pre|e|non|pro|self) - / $1- /ig;

    #kheafiel
    $eline =~ s/ (ca|are|do|could|did|does|do|had|has|have|is|must|need|should|was|were|wo|would)n 't / \1n't /gi;
  }
  $eline =~ s/ ([AaEe][Ll]) - / \1-/g;

  if ($language != "de") {
    #Take out any "words" that are longer than 50 chars
    $eline =~ s/\S{50,}/-/g;
  }

  $eline =~ s/\.\s*\.\s*\.\s*[\.\s]*/ ... /g;
  $eline =~ s/!\s*![!\s]*/ ! /g;
  $eline =~ s/\?\s*\?[\?\s]*/ ? /g;
  $eline =~ s/ ' s / 's /g;
  #cut multiple hyphens down to one and space separate it (single hyphens are not space separated) 
  $eline =~ s/([^-])--+([^-])/$1 - $2/g;

  #Delete excess spaces:
  $eline =~ s/\s+/ /g;
  $eline =~ s/^\s+//;
  $eline =~ s/\s+$//;

  print "$eline\n";
}

