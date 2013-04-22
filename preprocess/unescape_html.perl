#!/usr/bin/perl -w
binmode(STDIN, ":utf8");
binmode(STDOUT, ":utf8");

use HTML::Entities;
use utf8;

while(<STDIN>) {
  $str = decode_entities($_);
  $str =~ s/﻿/ /g;
  print $str;
}
