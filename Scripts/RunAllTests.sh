#!/usr/bin/env sh

set -eu

ScriptDirectory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SourceDirectory=$(CDPATH= cd -- "${ScriptDirectory}/.." && pwd)
BuildDirectory=${1:-"${SourceDirectory}/build-tests"}
BuildConfiguration=${2:-Debug}

ConfigureTests()
{
    printf 'Configuring tests in "%s"...\n' "${BuildDirectory}"
    cmake \
        -S "${SourceDirectory}" \
        -B "${BuildDirectory}" \
        -DARDASHIR_BUILD_TESTS=ON \
        -DCMAKE_BUILD_TYPE="${BuildConfiguration}"
}

BuildTests()
{
    printf 'Building tests using configuration "%s"...\n' "${BuildConfiguration}"
    cmake \
        --build "${BuildDirectory}" \
        --config "${BuildConfiguration}" \
        --parallel
}

RunTests()
{
    printf 'Running all project tests...\n'
    ctest \
        --test-dir "${BuildDirectory}" \
        -C "${BuildConfiguration}" \
        --output-on-failure
}

ConfigureTests
BuildTests
RunTests
