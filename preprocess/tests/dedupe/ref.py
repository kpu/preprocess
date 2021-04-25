#!/usr/bin/env python
import sys
lines = set()
for l in sys.stdin:
  if l not in lines:
    lines.add(l)
    sys.stdout.write(l)
