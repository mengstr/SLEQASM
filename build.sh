#!/bin/bash

pkg sleqasm.js --targets node14-macos-x64 --output sleqasm
#pkg subleq.js --targets node14-macos-x64 --output subleq
gcc -O3 -o subleq subleq.c

