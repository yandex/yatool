#pragma once

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/symbols/name_store.h>

#include <util/system/defaults.h>
#include <util/system/yassert.h>
#include <util/generic/vector.h>
#include <util/generic/hash.h>
#include <util/generic/hash_set.h>

#include <initializer_list>
#include <utility>

enum EDepType {
    EDT_Include = 1 /* "Include" */,
    EDT_BuildFrom = 2 /* "BuildFrom" */,
    EDT_BuildCommand = 3 /* "BuildCommand" */,
    EDT_Search = 4 /* "Search" */,          // src dir, module dependence for msvc projects
    EDT_Search2 = 5 /* "Search2" */,         // global include dir or global object
    EDT_Property = 6 /* "Property" */,        // only used during graph build or update
    EDT_OutTogether = 8 /* "OutTogether" */,     // (additional output -> main output)
    EDT_OutTogetherBack = 9 /* "OutTogetherBack" */, // (main output -> additional output)
    EDT_Group = 10 /* "Group" */,          //service deptype to group some the same type deps
    EDT_Last = EDT_Group,
};

enum ELogicalDepType {
    ELDT_FromDepType = 1 /* "FromDepType" */,
    ELDT_Depend = 2 /* "Depend" */, // direct depend edge from module to direct depend
    ELDT_Last = ELDT_Depend,
};

enum EMakeNodeType : ui8 {
    EMNT_Deleted = 0 /* "Deleted" */,
    EMNT_File = 1 /* "File" */,
    EMNT_MissingFile = 2 /* "MissingFile" */,
    EMNT_NonParsedFile = 3 /* "NonParsedFile" */,
    EMNT_Program = 4 /* "Program" */,
    EMNT_Library = 5 /* "Library" */,
    EMNT_Bundle = 6 /* "Bundle" */,
    EMNT_MakeFile = 7 /* "MakeFile" */,
    EMNT_Directory = 8 /* "Directory" */,
    EMNT_MissingDir = 9 /* "MissingDir" */,
    EMNT_NonProjDir = 10 /* "NonProjDir" */,
    EMNT_BuildCommand = 11 /* "BuildCommand" */,
    EMNT_UnknownCommand = 12 /* "UnknownCommand" */,
    EMNT_Property = 13 /* "Property" */, // actually may affect command unless entered via EDT_Property
    EMNT_BuildVariable = 14 /* "BuildVariable" */,
    EMNT_Last = EMNT_Property,
};

Y_FORCE_INLINE bool IsValidNodeType(EMakeNodeType type) {
    return (int)type >= EMNT_Deleted && (int)type <= EMNT_Property;
}

// A real file, we can use time stamps, etc.
inline bool IsFileType(EMakeNodeType type) {
    return (int)type >= EMNT_File && (int)type <= EMNT_MakeFile;
}

// A simple source file of any language (located in Arcadia or generated) that we can parse, to which we can attach induced deps etc.
inline bool IsSrcFileType(EMakeNodeType type) {
    return (int)type >= EMNT_File && (int)type <= EMNT_NonParsedFile;
}

// A directory, we can do listing, search for make-file
inline bool IsDirType(EMakeNodeType type) {
    return (int)type >= EMNT_Directory && (int)type <= EMNT_NonProjDir;
}

inline bool IsPropertyTypeNode(EMakeNodeType type) {
    return type == EMNT_Property;
}

inline bool IsInvalidDir(EMakeNodeType type) {
    return type == EMNT_MissingDir || type == EMNT_NonProjDir;
}

// An entry with id in file namespace
constexpr bool UseFileId(EMakeNodeType type) noexcept {
    return (int)type >= EMNT_File && (int)type <= EMNT_NonProjDir;
}

// A program, library or dll
inline bool IsModuleType(EMakeNodeType type) {
    return (int)type >= EMNT_Program && (int)type <= EMNT_Bundle;
}

inline bool IsMakeFileType(EMakeNodeType type) {
    return (int)type == EMNT_MakeFile;
}

// May be output file of a command
inline bool IsOutputType(EMakeNodeType type) {
    return (int)type >= EMNT_NonParsedFile && (int)type <= EMNT_Bundle;
}

enum class TDepsCacheId: ui64 {
    None = 0
};

Y_FORCE_INLINE constexpr TDepsCacheId MakeDepsCacheId(EMakeNodeType nodeType, ui32 elemId) noexcept {
    return static_cast<TDepsCacheId>((static_cast<ui64>(!UseFileId(nodeType)) << 63) | elemId);
}

Y_FORCE_INLINE constexpr TDepsCacheId MakeDepFileCacheId(ui32 elemId) noexcept {
    return static_cast<TDepsCacheId>(elemId);
}

Y_FORCE_INLINE constexpr bool IsFile(TDepsCacheId cacheId) noexcept {
    return !(static_cast<ui64>(cacheId) & (ui64{1} << 63));
}

Y_FORCE_INLINE constexpr ui32 ElemId(TDepsCacheId cacheId) noexcept {
    return static_cast<ui32>(static_cast<ui64>(cacheId) & ((ui64{1} << 63) - 1));
}

struct TAddDepDescr {
    EDepType DepType;
    EMakeNodeType NodeType;
    ui64 ElemId;    // FileId

    TAddDepDescr() {
        // leave garbage
    }

    TAddDepDescr(EDepType depType, EMakeNodeType nodeType, ui64 elemId)
        : DepType(depType)
        , NodeType(nodeType)
        , ElemId(elemId)
    {
    }

    Y_FORCE_INLINE TDepsCacheId CacheId() const noexcept {
        return MakeDepsCacheId(NodeType, ElemId);
    }

    bool operator==(const TAddDepDescr& other) const {
        return DepType == other.DepType && NodeType == other.NodeType && ElemId == other.ElemId;
    }

    size_t Hash() const {
        return CombineHashes(CombineHashes(static_cast<ui64>(ElemId), static_cast<ui64>(DepType)), static_cast<ui64>(NodeType));
    }
};

template <>
struct THash<TAddDepDescr> {
    size_t operator()(const TAddDepDescr& dep) const {
        return dep.Hash();
    }
};


struct TDeps {
    using TContainer = TVector<TAddDepDescr>;
    using iterator = TContainer::iterator;
    using const_iterator = TContainer::const_iterator;

    TDeps() = default;

    bool IsLocked() const {
        return Locked;
    }

    void Lock() {
        Locked = true;
    }

    void Add(EDepType depType, EMakeNodeType elemNodeType, ui64 elemId);
    void Add(const TDeps& what);
    bool AddUnique(EDepType depType, EMakeNodeType elemNodeType, ui64 elemId);

    void Replace(size_t idx, EDepType depType, EMakeNodeType elemNodeType, ui64 elemId);

    // Proxy collection interface

    auto Size() const {
        return Deps.size();
    }

    bool Empty() const {
        return Deps.empty();
    }

    void Clear() {
        Uniq.clear();
        Deps.clear();
    }

    auto Back() const {
        return Deps.back();
    }

    iterator begin() {
        return Deps.begin();
    }

    iterator end() {
        return Deps.end();
    }

    const_iterator begin() const {
        return Deps.begin();
    }

    const_iterator end() const {
        return Deps.end();
    }

    void Reserve(size_t sz) {
        Deps.reserve(sz);
    }

    void Erase(iterator b, iterator e);

    const TAddDepDescr& operator[](size_t idx) const {
        return Deps[idx];
    }

private:
    TContainer Deps;
    THashMultiSet<TAddDepDescr> Uniq;
    bool Locked = false;
};

inline EMakeNodeType FileTypeByRoot(NPath::ERoot root) {
    switch (root) {
        case NPath::Source:
            return EMNT_File;
        case NPath::Build:
            return EMNT_NonParsedFile;
        case NPath::Unset:
            return EMNT_MissingFile;
        case NPath::Link:
            Y_ABORT("NPath::Link in FileTypeByRoot(NPath::ERoot root)");
            Y_UNREACHABLE();
            return EMNT_Deleted;
        default:
            Y_UNREACHABLE();
            return EMNT_Deleted;
    }
}

inline EMakeNodeType FileTypeByRoot(const TStringBuf& fname) {
    auto root = NPath::GetType(fname);
    if (NPath::Link == root) {
        return FileTypeByRoot(NPath::GetTargetFromLink(fname));
    } else {
        return FileTypeByRoot(root);
    }
}

// Generic node kind. Sometimes node types are determined later than assigned. This enum is used
// to control compatibility of node type change: changes are allowed only within kind.
enum class EMakeNodeKind {
    Deleted = 0,
    AnyFile = 1,
    AnyDirectory = 2,
    AnyModule = 3,
    AnyCommand = 4,
    Property = 6,
};

inline EMakeNodeKind GetNodeKind(EMakeNodeType nodeType) {
    switch (nodeType) {
        case EMNT_Deleted:
            return EMakeNodeKind::Deleted;

        case EMNT_File:
        case EMNT_MissingFile:
        case EMNT_NonParsedFile:
        case EMNT_MakeFile:
            return EMakeNodeKind::AnyFile;

        case EMNT_Directory:
        case EMNT_MissingDir:
        case EMNT_NonProjDir:
            return EMakeNodeKind::AnyDirectory;

        case EMNT_Program:
        case EMNT_Library:
        case EMNT_Bundle:
            return EMakeNodeKind::AnyModule;

        case EMNT_BuildCommand:
        case EMNT_UnknownCommand:
        case EMNT_BuildVariable:
            return EMakeNodeKind::AnyCommand;

        case EMNT_Property:
            return EMakeNodeKind::Property;
    }
    Y_UNREACHABLE();
    return EMakeNodeKind::Deleted;
}

// This checks that type of newly incoming node is compatible with one
// known in graph. The newly incoming type may be imprecise (generic) e.g.
// it may be EMNT_Module for all modules etc.
inline bool IsTypeCompatibleWith(EMakeNodeType newType, EMakeNodeType knownType) {
    return GetNodeKind(newType) == GetNodeKind(knownType);
}

struct TDGIterAddable;
typedef TVector<TDGIterAddable> TAddIterStack;
typedef TUniqVector<ui32> TOwnEntries;
