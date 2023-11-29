#pragma once

#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/vars.h>

#include <util/generic/hash.h>

struct TVarStrEx: public TVarStr {
    ui64 ElemId;

    TVarStrEx(const TStringBuf& name)
        : TVarStr(name)
        , ElemId(0)
    {
    }

    TVarStrEx(const TString& name)
        : TVarStr(name)
        , ElemId(0)
    {
    }

    TVarStrEx(const TVarStr& f)
        : TVarStr(f)
        , ElemId(0)
    {
    }

    TVarStrEx(const TStringBuf& name, ui64 elemId, bool isPathResolved)
        : TVarStr(name, false, isPathResolved)
        , ElemId(elemId)
    {
    }

    bool operator==(const TVarStrEx& other) const {
        if ((ElemId) && (other.ElemId)) {
            return (ElemId == other.ElemId) // same ElemId
                && (IsMacro == other.IsMacro); // in same store
        } else {
            // FIXME(dimdim11) Usage compare as TVarStr as code below fault many tests
            // return static_cast<const TVarStr&>(*this) == static_cast<const TVarStr&>(other);
            return Name == other.Name;
        }
    }
};


template <>
struct THash<TVarStrEx> {
    size_t operator()(const TVarStrEx& var) const {
        // FIXME(dimdim11) Usage hash of TVarStr as code below fault many tests
        // return THash<TVarStr>()(var);
        return THash<TString>()(var.Name);
    }
};

template <>
class NUniqContainer::TRefWithIndex<TVarStrEx> {
public:
    TRefWithIndex(size_t id) : index(id) {}
    static std::string ToString(const TVector<TVarStrEx>& vec, size_t index) {
        return vec[index].Name;
    }
    static std::string ToString(const TVector<TVarStrEx>& vec, const TRefWithIndex<TVarStrEx>& ref) {
        return vec[ref.index].Name;
    }

public:
    size_t index;
};

using TSpecFileArr = TVector<TVarStrEx>;
// Read TUniqContainerImpl documentation about "thresholds".
// This particular value is from benchmarks.
// (THashTable does rehashing when it grows above 17).
constexpr size_t SpecFileListThreshold = 17;
using TSpecFileList = TUniqContainerImpl<TVarStrEx, NUniqContainer::TRefWithIndex<TVarStrEx>, SpecFileListThreshold, TSpecFileArr, true>;
