#include "logger_filter.h"

namespace NYa {
    const TString TOKEN_PREFIX_ = "AQAD-";

    TYaTokenFilter::TYaTokenFilter(const TVector<TStringBuf>& args){
        for(size_t i = 1; i < args.size(); ++i) {
            const TStringBuf arg = args[i];
            if (arg.EndsWith("token") && arg.StartsWith("-")) {
                ++i;
                if (i < args.size()) {
                    Replacements_.emplace(args[i]);
                }
            } else if (size_t pos = arg.find("token="); pos != std::string::npos) {
                const TStringBuf token = arg.SubStr(pos + strlen("token="));
                Replacements_.emplace(token);
            } else if (arg.Contains(TOKEN_PREFIX_)) {
                Replacements_.emplace(arg);
            }
        }
        for (const auto& [key, value] : Environ()) {
            if (value) {
                TString lkey = to_lower(key);
                if (value.Contains(TOKEN_PREFIX_)) {
                    Replacements_.insert(value);
                } else if (lkey.EndsWith("token") || lkey.EndsWith("secret")) {
                    // We treat short enough (10^10 ~= 2^30) numbers as non-secrets
                    // This should be enough to avoid reasonable numbers in variables
                    // Lambda needs to convert char to unsigned char before isdigit() call
                    if (value.size() >= 10 || !AllOf(value, [](unsigned char c){ return std::isdigit(c);})) {
                        Replacements_.insert(value);
                    }
                }
            }
        }
    }

    TString TYaTokenFilter::operator()(ELogPriority, TStringBuf message) const {
        TString result{message};
        for (const TString& repl : Replacements_) {
            SubstGlobal(result, repl, "[SECRET]");
        }
        return result;
    }
}
