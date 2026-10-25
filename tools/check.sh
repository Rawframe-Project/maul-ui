#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The checks, in three tiers. Each tier includes the one before it.
#
#   tools/check.sh changed   the files changed against origin/main: the
#                            source rules, formatting, the documentation
#                            checks, and the tests the changes reach,
#                            integration tests left out: the core's in the
#                            linux-clang-20-asan preset, the renderer's and
#                            the glue's in linux-parts. Under two minutes;
#                            run it before every push.
#   tools/check.sh pr        what the pull request check runs on Linux:
#                            every rule, the layout fixture tables, the
#                            documentation site, clang-tidy, the whole
#                            linux-clang-20-asan and linux-parts suites and
#                            a short fuzzing pass.
#   tools/check.sh full      also the other Linux presets (ThreadSanitizer,
#                            shared, the core alone, the parts without
#                            text and with GCC, Clang and GCC Release) and
#                            longer fuzzing.
#
# The builds are the CI cells' CMake presets (CMakePresets.json), in
# out/<preset>; the other trees go to out/check-*. Needs what the
# organization's CI image has: gcc-14, clang-20, clang-tidy-20, ninja, the
# pinned clang-format, lavapipe with the Vulkan validation layer, and
# dbus-daemon. The organization's documentation check runs when its
# .github checkout is at ORG_DEFAULTS (default: ../../.github from the
# repository). The platform cells (macOS, iOS, Android, Windows, the web)
# run in CI; docs/operations/test-environments.md says how to run each by
# hand.
set -euo pipefail

tier=${1:-changed}
case "$tier" in
changed | pr | full) ;;
*)
    echo "usage: tools/check.sh [changed|pr|full]" >&2
    exit 2
    ;;
esac

cd "$(dirname "$0")/.."
root=$PWD
out=$root/out
mkdir -p "$out"
org=${ORG_DEFAULTS:-$root/../../.github}
jobs=${JOBS:-8}

step() { printf '\n== %s\n' "$*"; }

# A tree outside the presets (clang-tidy, fuzzing).
configure() { # name, then cmake arguments
    local name=check-$1
    shift
    cmake -S "$root" -B "$out/$name" -G Ninja -DMAUL_UI_WERROR=ON "$@" >"$out/$name.log"
}

# Configures a preset, checks its tests' registrations, and builds it.
build_preset() {
    cmake --preset "$1" >"$out/$1.log"
    python3 tools/check_test_hygiene.py --ctest "out/$1"
    cmake --build --preset "$1" -j "$jobs" >>"$out/$1.log"
}

# A CI cell: its configure, build and test presets.
preset() {
    step "preset $1"
    build_preset "$1"
    ctest --preset "$1" -j "$jobs"
}

# The ctest labels a changed path reaches, as a regular expression: "all"
# for every test of the core (shared code, the build, the headers),
# "parts" for the renderer's and the glue's, nothing for a path no Linux
# test runs.
labels_for() {
    local path=$1 name
    name=$(basename "$path")
    name=${name%.*}
    name=${name#test_}
    case "$path" in
    *.md | LICENSE | .github/* | docs/* | bench/* | java/* | test/android/* | test/ios/*) ;;
    tools/check_*.py | tools/build_docs.py | tools/gen_api.py | tools/run_* | tools/build_android_app.sh) ;;
    tools/screen_reader* | tools/random_layouts.py | tools/layout_oracle.mjs | tools/size_report.py) ;;
    test/fuzz_* | tools/fuzz_seed.py) ;;
    src/uia_* | src/ns_* | src/uikit_* | src/android_* | src/aria_* | src/web_js.h) ;;
    test/test_uia.c | test/test_*.m | test/test_android_* | test/test_aria.c | test/test_windows_*) ;;
    rhi/* | window/* | samples/* | test/test_rhi_* | test/test_window_* | test/draw_golden.inc) echo parts ;;
    src/atspi_* | src/dbus_api.*) echo "^atspi$" ;;
    test/layout/* | test/generated/* | test/layout_fixture.h | tools/gen_layout_fixtures.py)
        echo "^layout_fixtures$"
        ;;
    src/*.[ch] | test/test_*.c)
        if [ -f "test/test_$name.c" ]; then echo "^$name$"; else echo all; fi
        ;;
    *) echo all ;;
    esac
}

source_checks() {
    step "source, module, length, documentation, snippet and test hygiene rules, the shader container's stamp"
    python3 tools/check_source.py
    python3 tools/check_modules.py
    python3 tools/check_lengths.py
    python3 tools/check_docs.py
    python3 tools/check_guide.py
    python3 tools/check_text_headers.py
    python3 tools/check_litmus.py
    python3 tools/check_float_env.py
    python3 tools/check_test_hygiene.py --self-test
    python3 tools/check_test_hygiene.py
    python3 tools/gen_rhi_shaders.py --check
    python3 tools/gen_rhi_shaders.py --self-test
    if [ -f "$org/scripts/docs_check.py" ]; then
        python3 "$org/scripts/docs_check.py" --strict
    else
        echo "skipped the organization's documentation check: no checkout at $org"
    fi
}

# Every C file of the project; the renderer's shader container is
# generated.
all_c_files() {
    git ls-files 'include/*.h' 'src/*.[chm]' 'test/*.[chm]' 'tools/*.[ch]' 'bench/*.[ch]' \
        'samples/*.[ch]' 'rhi/*.[ch]' 'window/*.[ch]' | grep -v -E '_container\.h$|^test/generated/'
}

# Runs the tests of a build tree whose labels match, integration tests
# left out; by the tree rather than the test preset, whose own filter
# would replace -LE.
reached_tests() { # tree, label regex or ""
    local tests=(ctest --test-dir "$out/$1" --output-on-failure --timeout 300 -j "$jobs" -LE '^integration$')
    [ -n "$2" ] && tests+=(-L "$2")
    if [ "$("${tests[@]}" -N | grep -c 'Test *#' || true)" -gt 0 ]; then
        "${tests[@]}"
    else
        echo "no test outside the integration tests is reached"
    fi
}

if [ "$tier" = changed ]; then
    base=$(git merge-base HEAD origin/main 2>/dev/null || echo HEAD)
    mapfile -t changed < <({
        git diff --name-only "$base"
        git ls-files --others --exclude-standard
    } | sort -u)
    if [ ${#changed[@]} -eq 0 ]; then
        echo "nothing changed against origin/main"
        exit 0
    fi
    source_checks
    mapfile -t c_files < <(printf '%s\n' "${changed[@]}" | grep -E '\.[chm]$' |
        grep -v -E '_container\.h$|^test/generated/' || true)
    present=()
    for f in "${c_files[@]+"${c_files[@]}"}"; do [ -f "$f" ] && present+=("$f"); done
    if [ ${#present[@]} -gt 0 ]; then
        step "clang-format on ${#present[@]} changed files"
        clang-format --dry-run -Werror "${present[@]}"
    fi
    labels=""
    for f in "${changed[@]}"; do labels="$labels $(labels_for "$f")"; done
    core=$(printf '%s\n' $labels | grep -v '^parts$' | sort -u || true)
    if [ -n "$core" ]; then
        step "preset linux-clang-20-asan, integration tests left out"
        build_preset linux-clang-20-asan
        if printf '%s\n' $core | grep -qx all; then
            reached_tests linux-clang-20-asan ""
        else
            regex=$(printf '%s\n' $core | paste -sd '|')
            echo "tests labeled $regex"
            reached_tests linux-clang-20-asan "$regex"
        fi
    fi
    if [[ " $labels " == *" parts "* || " $labels " == *" all "* ]]; then
        step "preset linux-parts, integration tests left out"
        build_preset linux-parts
        reached_tests linux-parts "^(rhi_|window_)"
    fi
    [ -n "${labels// /}" ] || echo "no Linux test reached by the changes"
    exit 0
fi

source_checks

step "clang-format on every file"
all_c_files | xargs clang-format --dry-run -Werror

step "the layout fixture tables match the corpus"
python3 tools/gen_layout_fixtures.py --check

step "the documentation site builds"
python3 tools/build_docs.py >"$out/site.log"

step "clang-tidy on the Linux build's sources"
configure tidy -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DMAUL_UI_BUILD_TESTS=OFF -DMAUL_UI_RHI=ON -DMAUL_UI_WINDOW=ON -DMAUL_WINDOW_WAYLAND=OFF -DMAUL_WINDOW_X11=OFF
cmake --build "$out/check-tidy" -j "$jobs" >>"$out/check-tidy.log"
python3 tools/tidy_files.py "$out/check-tidy/compile_commands.json" >"$out/tidy-files.txt"
xargs -P "$jobs" -n 1 clang-tidy-20 -p "$out/check-tidy" --quiet --warnings-as-errors='*' <"$out/tidy-files.txt" \
    2>/dev/null

preset linux-clang-20-asan
preset linux-parts

fuzz_seconds=10
[ "$tier" = full ] && fuzz_seconds=60
step "fuzzing, $fuzz_seconds seconds per target"
configure fuzz -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 -DMAUL_UI_FUZZ=ON \
    -DMAUL_UI_BUILD_TESTS=OFF -DMAUL_UI_BUILD_BENCH=OFF
cmake --build "$out/check-fuzz" -j "$jobs" >>"$out/check-fuzz.log"
python3 tools/fuzz_seed.py "$out/corpus"
for target in "$out"/check-fuzz/fuzz_*; do
    name=$(basename "$target")
    "$target" -max_total_time=$fuzz_seconds -rss_limit_mb=4096 "$out/corpus/corpus-$name" \
        >"$out/$name.log" 2>&1 || {
        tail -30 "$out/$name.log"
        exit 1
    }
    echo "$name: no finding"
done

[ "$tier" = full ] || exit 0

for name in linux-clang-20-tsan linux-shared linux-core linux-parts-core linux-parts-gcc-14 linux-clang-20 linux-gcc-14; do
    preset "$name"
done

step "all checks passed"
