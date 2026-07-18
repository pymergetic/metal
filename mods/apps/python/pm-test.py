# Metal guest python smoke — same path on every platform:
#   /mods/apps/python.wasm /mods/apps/pm-test.py
import sys

assert sys.version_info[:2] == (3, 14), sys.version
print(1)
print("pm-test: ok")
