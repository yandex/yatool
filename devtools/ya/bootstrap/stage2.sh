#!/usr/bin/env sh

set -xeu

SR=${1}
BR=${2}

RR=${3}

export LD_LIBRARY_PATH=${RR}/stage1:
export YA_NO_RESPAWN=1
export YA_SOURCE_ROOT=${SR}
ldconfig

cd ${SR}

export FLAGS="
--ymake-bin ${RR}/stage1/ymake
-T --no-src-links
--host-platform-flag=USE_PREBUILT_TOOLS=no -DUSE_PREBUILT_TOOLS=no
--host-platform-flag=OPENSOURCE=yes -DOPENSOURCE=yes
--host-platform-flag=OPENSOURCE_PROJECT="ya" -DOPENSOURCE_PROJECT="ya"
--host-platform-flag=YMAKE_USE_PY3="yes" -DYMAKE_USE_PY3=yes
"

for platform in $(cat ${SR}/devtools/ya/bootstrap/platform_list); do
    mkdir -p ${RR}/stage2/${platform}
    mkdir -p ${RR}/stage2/additional/${platform}
    ${RR}/stage1/ya-bin m \
        ${SR}/devtools/ya/bin \
        ${SR}/devtools/ymake/bin \
        ${SR}/devtools/local_cache/toolscache/server \
        ${SR}/devtools/ya/test/programs/test_tool/bin3 \
        -I ${RR}/stage2/${platform} \
        --target-platform ${platform} \
        ${FLAGS}

    ${RR}/stage1/ya-bin m \
        ${SR}/devtools/yexport/bin \
        -I ${RR}/stage2/additional/${platform} \
        --target-platform ${platform} \
        ${FLAGS}
done
