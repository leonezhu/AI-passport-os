#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_dir="$(mktemp -d /tmp/passport-host.XXXXXX)"
trap 'rm -rf -- "$test_dir"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/apps/voice \
    tests/test_ui_pixel_math.c main/apps/voice/ui_pixel_math.c -o "$test_dir/math"
"$test_dir/math"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
    tests/test_tetris.c main/apps/game/tetris_core.c -o "$test_dir/tetris"
"$test_dir/tetris"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/test_terminal.cc -o "$test_dir/terminal"
"$test_dir/terminal"
node tests/test_profile_page.js
python3 tests/test_verify_firmware.py
echo "Host regression tests: PASS"
