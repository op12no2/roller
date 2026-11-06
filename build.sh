#!/bin/bash
# build.sh - builds roller (linux) and roller.exe (static windows); clang is ~18% faster than gcc here
set -e
cd "$(dirname "$0")"
CC=${CC:-clang}
if [ "$CC" = clang ]; then
  clang -O3 -Wall -Wextra -o roller roller.c -lpthread -lm
  clang --target=x86_64-w64-mingw32 -O3 -static -o roller.exe roller.c -lpthread
else
  gcc -O3 -Wall -Wextra -o roller roller.c -lpthread -lm
  x86_64-w64-mingw32-gcc -O3 -static -o roller.exe roller.c -lpthread -lm
fi
echo "built roller roller.exe with $CC"
