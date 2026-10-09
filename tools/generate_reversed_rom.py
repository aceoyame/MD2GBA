#!/usr/bin/env python3
"""Generate an optional PSC-style descending-address ROM view for MD2GBA."""
from pathlib import Path
import argparse, hashlib, json

def main():
    ap=argparse.ArgumentParser();ap.add_argument('rom',type=Path);ap.add_argument('output',type=Path);ap.add_argument('--manifest',type=Path)
    a=ap.parse_args();raw=a.rom.read_bytes();rev=raw[::-1];a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(rev)
    if a.manifest:a.manifest.write_text(json.dumps({'source_size':len(raw),'source_sha256':hashlib.sha256(raw).hexdigest(),'reversed_sha256':hashlib.sha256(rev).hexdigest(),'mapping':'reversed[N-1-address]'},indent=2)+'\n')
if __name__=='__main__':main()
