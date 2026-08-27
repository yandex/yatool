#include "logger_filter.h"

#include <cctype>

namespace NYa {
    namespace {
        constexpr TStringBuf TokenPrefix = "AQAD-";
        constexpr TStringBuf PrivateKeyPrefix = "-----BEGIN ";

        bool IsSecretKey(TStringBuf key) {
            const TString lowerKey = to_lower(TString{key});
            return lowerKey.EndsWith("token") ||
                   lowerKey.EndsWith("secret") ||
                   lowerKey.EndsWith("password") ||
                   lowerKey.EndsWith("_rsa") ||
                   lowerKey.Contains("access_key") ||
                   lowerKey.Contains("secret_key");
        }

        bool IsTokenCharacter(unsigned char c) {
            return std::isalnum(c) || c == '_' || c == '-' || c == '\\';
        }

        void AddTokenReplacements(TStringBuf value, THashSet<TString>& replacements) {
            size_t start = 0;
            while ((start = value.find(TokenPrefix, start)) != TStringBuf::npos) {
                size_t end = start + TokenPrefix.size();
                while (end < value.size() && IsTokenCharacter(value[end])) {
                    ++end;
                }
                if (end > start + TokenPrefix.size()) {
                    replacements.emplace(value.SubStr(start, end - start));
                }
                start = end;
            }
        }

        void AddValueReplacements(TStringBuf value, THashSet<TString>& replacements) {
            AddTokenReplacements(value, replacements);
            if (value.Contains(PrivateKeyPrefix) && value.Contains("PRIVATE KEY")) {
                replacements.emplace(value);
            }
        }
    } // namespace

    TYaTokenFilter::TYaTokenFilter(const TVector<TStringBuf>& args) {
        for (size_t i = 1; i < args.size(); ++i) {
            const TStringBuf arg = args[i];
            if (arg.StartsWith("--")) {
                const size_t separator = arg.find('=');
                if (separator != TStringBuf::npos) {
                    const TStringBuf key = arg.Head(separator);
                    const TStringBuf value = arg.SubStr(separator + 1);
                    if (IsSecretKey(key) && value) {
                        Replacements_.emplace(value);
                    }
                } else if (IsSecretKey(arg) && i + 1 < args.size() && !args[i + 1].StartsWith("--")) {
                    Replacements_.emplace(args[++i]);
                }
            }
            AddValueReplacements(arg, Replacements_);
        }
        for (const auto& [key, value] : Environ()) {
            if (value) {
                AddValueReplacements(value, Replacements_);
                if (IsSecretKey(key)) {
                    // We treat short enough (10^10 ~= 2^30) numbers as non-secrets
                    // This should be enough to avoid reasonable numbers in variables
                    // Lambda needs to convert char to unsigned char before isdigit() call
                    if (value.size() >= 10 || !AllOf(value, [](unsigned char c) { return std::isdigit(c); })) {
                        Replacements_.insert(value);
                    }
                }
            }
        }
    }

    TString TYaTokenFilter::Sanitize(TStringBuf value) const {
        TString result{value};
        for (const TString& repl : Replacements_) {
            SubstGlobal(result, repl, "[SECRET]");
        }
        return result;
    }

    TString TYaTokenFilter::operator()(ELogPriority, TStringBuf message) const {
        return Sanitize(message);
    }
}
