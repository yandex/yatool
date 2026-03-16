#!/usr/bin/env sh

set -xeu

git clone --depth=1 --recurse-submodules --shallow-submodules https://github.com/yandex/toolchain-registry.git /ix

export PATH=/ix:${PATH}
export IX_ROOT=/ix
export IX_EXEC_KIND=local

cd /ix
ix mut output --materialize bin/ya/final --target=linux-x86_64

cp realm/output/bin/* /result
