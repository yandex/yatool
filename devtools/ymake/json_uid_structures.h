#pragma once

#include "md5.h"

#include <devtools/ymake/compact_graph/iter.h>

enum class EJsonHashField {
    Structure,
    StructureForConsumer
};

class TJsonMD5NewUID {
private:
    TMd5Value StructureMd5;
    TMd5Value IncludeStructureMd5;
    TMd5Value ContentMd5;
    TMd5Value IncludeContentMd5;

public:
    TJsonMD5NewUID(TNodeDebugOnly nodeDebug);

    void StructureMd5Update(const TMd5UpdateValue& value, TStringBuf reason) {
        StructureMd5.Update(value, reason);
    }

    void IncludeStructureMd5Update(const TMd5UpdateValue& value, TStringBuf reason) {
        IncludeStructureMd5.Update(value, reason);
    }

    void ContentMd5Update(const TMd5UpdateValue& value, TStringBuf reason) {
        ContentMd5.Update(value, reason);
    }

    void IncludeContentMd5Update(const TMd5UpdateValue& value, TStringBuf reason) {
        IncludeContentMd5.Update(value, reason);
    }

    const TMd5Value& GetStructureMd5() const {
        return StructureMd5;
    }

    const TMd5Value& GetIncludeStructureMd5() const {
        return IncludeStructureMd5;
    }

    const TMd5Value& GetContentMd5() const {
        return ContentMd5;
    }

    const TMd5Value& GetIncludeContentMd5() const {
        return IncludeContentMd5;
    }
};

struct TJSONEntryStatsNewUID : public TEntryStats, public TNodeDebugOnly {
    TJSONEntryStatsNewUID(TNodeDebugOnly nodeDebug, bool inStack, bool isFile);

    void SetStructureUid(const TMd5SigValue& md5);
    void SetStructureUid(const TMd5Value& oldMd5);

    void SetIncludeStructureUid(const TMd5SigValue& md5);
    void SetIncludeStructureUid(const TMd5Value& oldMd5);

    void SetContentUid(const TMd5SigValue& md5);
    void SetContentUid(const TMd5Value& oldMd5);

    void SetIncludeContentUid(const TMd5SigValue& md5);
    void SetIncludeContentUid(const TMd5Value& oldMd5);

    void SetFullUid(const TMd5Value& oldMd5);
    void SetSelfUid(const TMd5Value& oldMd5);

    const TMd5SigValue& GetStructureUid() const {
        Y_ASSERT(Finished);
        return StructureUID;
    }

    const TMd5SigValue& GetIncludeStructureUid() const {
        Y_ASSERT(Finished);
        return IncludeStructureUID;
    }

    const TMd5SigValue& GetContentUid() const {
        Y_ASSERT(Finished);
        return ContentUID;
    }

    const TMd5SigValue& GetIncludeContentUid() const {
        Y_ASSERT(Finished);
        return IncludeContentUID;
    }

    const TMd5SigValue& GetFullUid() const {
        return FullUID;
    }

    const TMd5SigValue& GetSelfUid() const {
        return SelfUID;
    }

    TString GetFullNodeUid() const {
        return FullUID.ToBase64();
    }

    TString GetSelfNodeUid() const {
        return SelfUID.ToBase64();
    }

    bool IsFullUidCompleted() const {
        return IsFullUIDCompleted;
    }

    bool IsSelfUidCompleted() const {
        return IsSelfUIDCompleted;
    }

    size_t EnterDepth = 0;
    bool Finished = false;
    bool Stored = false;

protected:
    TMd5SigValue StructureUID;
    TMd5SigValue IncludeStructureUID;
    TMd5SigValue ContentUID;
    TMd5SigValue IncludeContentUID;

    TMd5SigValue FullUID;
    TMd5SigValue SelfUID;

    bool IsFullUIDCompleted;
    bool IsSelfUIDCompleted;
};
