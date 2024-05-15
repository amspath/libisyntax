#!/usr/bin/env python3

import pathlib
import requests
import shutil
import sys

url = sys.argv[1]
file = pathlib.Path(sys.argv[2])

if not file.exists():
    print(f'Fetching {url}...')
    resp = requests.get(url, stream=True)
    resp.raise_for_status()
    with file.open('wb') as fh:
        shutil.copyfileobj(resp.raw, fh)
