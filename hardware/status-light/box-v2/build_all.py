#!/usr/bin/env python3
"""Build every v2 part.  Run in the img23d env:  conda run -n img23d python build_all.py"""
import build_case, build_cover

if __name__ == "__main__":
    build_case.build_case().export("case.stl")
    build_cover.build_cover().export("cover.stl")
    print("case.stl + cover.stl written")
