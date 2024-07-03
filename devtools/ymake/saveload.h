#pragma once

#include <devtools/ymake/common/md5sig.h>

#include <library/cpp/on_disk/multi_blob/multiblob_builder.h>

#include <util/folder/path.h>
#include <util/memory/blob.h>

#include <functional>

class TBuildConfiguration;
class IBlobSaverBase;

TFsPath MakeTempFilename(const TString& basePath);

TMd5Sig DefaultConfHash(const TBuildConfiguration& conf);
TMd5Sig ExtraConfHash(const TBuildConfiguration& conf);

using TConfHash = std::function<TMd5Sig(const TBuildConfiguration&)>;
class TCacheFileReader {
private:
    const TBuildConfiguration& Conf;
    const bool ForceLoad;
    const bool UseExtraConf;
    TConfHash Hash;
    TConfHash ExtraHash;

    THolder<TSubBlobs> SubBlobs;
    TSubBlobs::iterator SubBlobsIt;

public:
    enum class EReadResult {
        Success,
        Exception,
        IncompatibleFormat,
        UpdatedBinary,
        ChangedConfig,
        ChangedExtraConfig
    };
    using enum EReadResult;

    TCacheFileReader(const TBuildConfiguration& conf, bool forceLoad, bool useExtraConf, TConfHash confHash = DefaultConfHash, TConfHash extraConf = ExtraConfHash);

    EReadResult Read(const TFsPath& file);

    bool HasNextBlob() const;
    TBlob& GetNextBlob();

private:
    EReadResult CheckVersionInfo();
};

class TCacheFileWriter {
private:
    const TBuildConfiguration& Conf;
    TFsPath Path;
    TConfHash Hash;
    TConfHash ExtraHash;

    TMultiBlobBuilder Builder;

public:
    TCacheFileWriter(const TBuildConfiguration& conf, const TFsPath& path, TConfHash confHash = DefaultConfHash, TConfHash extraConf = ExtraConfHash);

    TFsPath Flush(bool delayed);

    void AddBlob(IBlobSaverBase* blob);
    TMultiBlobBuilder& GetBuilder();

private:
    void SaveVersionInfo();
};

class TDepGraph;

class TUidsCachable {
public:
    virtual void SaveCache(IOutputStream* output, const TDepGraph& graph) = 0;
    virtual void LoadCache(IInputStream* input, const TDepGraph& graph) = 0;
};
