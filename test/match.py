#!/usr/bin/env python3

import argparse
import re
import subprocess

parser = argparse.ArgumentParser(
    'match.py',
    description='run program with arguments, match stdout against regexes'
)
parser.add_argument('-e', '--regex', action='append')
parser.add_argument('command')
parser.add_argument('arg', nargs='*')
args = parser.parse_args()
if not args.regex:
    raise Exception('No regex specified')

result = subprocess.run(
    [args.command] + args.arg, capture_output=True, text=True, check=True
)
for regex in args.regex:
    if not re.search(regex, result.stdout):
        raise Exception(f'Did not match: {regex}: {result.stdout}')
