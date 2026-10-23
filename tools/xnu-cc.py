#!/usr/bin/env python3
"""Compatibility entry point for the shared XNU compile helper."""
from pathlib import Path
import runpy

if __name__ == '__main__':
    helper = Path(__file__).resolve().parents[1] / 'x676c/tools/xnu-cc.py'
    runpy.run_path(str(helper), run_name='__main__')
