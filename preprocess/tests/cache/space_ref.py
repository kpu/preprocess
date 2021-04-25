#!/usr/bin/env python3
import sys
lines = {}
for l in sys.stdin:
  key = l[0:-1].split(' ')[0]
  if key in lines:
    sys.stdout.write(lines[key])
  else:
    lines[key] = l
    sys.stdout.write(l)
