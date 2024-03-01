#include <util/datetime/base.h>
#include <util/string/cast.h>
#include <util/string/join.h>
#include <util/system/env.h>

void __attribute__ ((constructor)) SetState() {
    const TString YA_STAGES_ENV = "YA_STAGES";
    TString yaStages = GetEnv(YA_STAGES_ENV);
    TString val = Join("@", "binary-initialization", FloatToString(TInstant::Now().SecondsFloat()));
    if (yaStages) {
        yaStages += ':';
    }
    yaStages += val;
    SetEnv(YA_STAGES_ENV, yaStages);
}
