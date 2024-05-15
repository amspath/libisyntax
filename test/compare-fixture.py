#!/usr/bin/env python3

import argparse
import subprocess
import tempfile

parser = argparse.ArgumentParser(
    'compare_fixture.py',
    description='run program with arguments, compare output to fixture'
)
parser.add_argument(
    '-f', '--fixture', type=argparse.FileType('rb'), required=True
)
parser.add_argument('command')
parser.add_argument('arg', nargs='*')
args = parser.parse_args()

with tempfile.NamedTemporaryFile() as temp:
    subprocess.run(
        [args.command] + args.arg + [temp.name], check=True
    )
    output = temp.read()
    if not output:
        raise Exception('Program produced no output')
    if output != args.fixture.read():
        raise Exception('Output does not match fixture')
