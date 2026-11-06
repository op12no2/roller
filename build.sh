#!/bin/bash
# build.sh - builds roller (linux) and roller.exe (static windows)
set -e
cd "$(dirname "$0")"
gcc -O3 -Wall -Wextra -o roller roller.c -lpthread -lm
x86_64-w64-mingw32-gcc -O3 -static -o roller.exe roller.c -lpthread -lm
echo "built roller roller.exe"
