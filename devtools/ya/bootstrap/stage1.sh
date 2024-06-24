#!/usr/bin/env sh

set -xeu

SR=${1}
BR=${2}

RR=${3}/stage1

mkdir -p ${RR}

# local file is for debug only
if [ -f ${SR}/devtools/ya/bootstrap/all.json ]; then
    python3 ${SR}/devtools/ya/bootstrap/graph_executor.py ${SR} ${BR} 120 ${SR}/devtools/ya/bootstrap/all.json
else
    python3 ${SR}/devtools/ya/bootstrap/graph_executor.py ${SR} ${BR} 120
fi

cd ${BR}

mv $(find . | grep devtools/ymake/bin/ymake) ${RR}
mv $(find . | grep devtools/ya/bin/ya-bin) ${RR}

# Libiconv stored in devtools/ymake/bin is always a symlink.
ICONV_PATH=$(find . | grep devtools/ymake/bin/libiconv.so)
mv $(readlink -f $ICONV_PATH) ${RR}
