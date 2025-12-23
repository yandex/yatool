#pragma once

#include <library/cpp/digest/md5/md5.h>

#include <util/generic/algorithm.h>
#include <util/generic/yexception.h>
#include <util/string/join.h>
#include <util/string/split.h>
#include <memory>

THashMap<TString, TString> PrepareInputs(const TVector<TString>& inputs) {
    THashMap<TString, TString> inputToLink;
    for (const auto& in : inputs) {
        TString link = TFileConf::ConstructLink(ELinkType::ELT_Text, NPath::ConstructPath(in));
        inputToLink[in] = link;
    }
    return inputToLink;
};

void ReplaceInputs(TVector<TString>& args, const THashMap<TString, TString>& inputsMap) {
    for (auto& arg : args) {
        if (const auto it = inputsMap.find(arg); it != inputsMap.end()) {
            arg = it->second;
        }
    }
}

template <typename T>
concept CIsString = std::convertible_to<T, std::string_view>;

template <CIsString T, typename U>
void append_impl(TVector<T>& lhs, const U& rhs) {
    if constexpr (CIsString<std::decay_t<U>>) {
        lhs.push_back(T{rhs});
    } else {
        lhs.insert(lhs.end(), rhs.begin(), rhs.end());
    }
}

template <CIsString T, typename... Args>
void append(TVector<T>& lhs, Args... args) {
    (append_impl(lhs, args), ...);
}

template <CIsString T, typename... Args>
void append_if(bool need, TVector<T>& lhs, Args... args) {
    if (need) {
        (append_impl(lhs, args), ...);
    }
}

namespace NYMake::NResourcePacker {

    struct IResourcePacker {
        IResourcePacker(TPluginUnit& Unit)
            : Unit_{Unit}
        {
        }
        virtual void HandleResource(TStringBuf path, TStringBuf name) = 0;
        virtual void Finalize(bool force = false) = 0;
        virtual ~IResourcePacker() = default;

    protected:
        TPluginUnit& Unit_;

    protected:
        /*
            This function generates hash that can be used for output filenames.
            The hash takes into account the path of the current module and its tag.

            The input parameter `list` is used as a initial seed for the target hash generation.
        */
        TString GetHashForOutput(TVector<TStringBuf>&& list) {
            constexpr size_t LEN_LIMIT = 26ULL;

            auto unitPath = Unit_.UnitPath();
            auto moduleTag = Unit_.Get("MODULE_TAG"sv);
            list.emplace_back(unitPath);
            Sort(list);
            TString stringify = TString::Join(JoinSeq(",", list), moduleTag);

            TString hash = MD5::Calc(stringify).substr(0, LEN_LIMIT);
            hash.to_lower(0, LEN_LIMIT);
            return hash;
        }

        void RunMacro(TStringBuf macro, const TVector<TString>& args) {
            TVector<TStringBuf> view = TVector<TStringBuf>(args.begin(), args.end());
            Unit_.CallMacro(macro, view);
        }

        void RunMacro(TStringBuf macro, const TVector<TString>& args, TVars extraVars) {
            TVector<TStringBuf> view = TVector<TStringBuf>(args.begin(), args.end());
            Unit_.CallMacro(macro, view, std::move(extraVars));
        }

    protected:
        static constexpr int MAX_CMD_LEN = 8000;
        static constexpr int ROOT_CMD_LEN = 200;

        static constexpr std::string_view AUX_CPP_EXT = ".auxcpp";
    };

} // namespace NYMake::NResourcePacker
