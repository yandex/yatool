#include "confreader_cache.h"

#include <devtools/ymake/common/md5sig.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/diag/display.h>
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/conf.h>
#include <devtools/ymake/config/config.h>
#include <devtools/ymake/lang/confreader.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/folder/path.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/stream/file.h>
#include <util/stream/fwd.h>
#include <util/system/fs.h>
#include <util/system/fstat.h>
#include <util/system/getpid.h>
#include <util/ysaveload.h>

#include <ctime>

namespace {
    using namespace NConfReader;

    static constexpr const ui32 MAGIC = 0x5afec0de;
    static constexpr const ui32 VERSION = 0xA;

    bool IsActualFile(const TString& fileName,
                      const TString& md5,
                      time_t modificationTime,
                      TString* ignoredHashContent) {
        TFsPath filePath(fileName);
        if (!filePath.Exists()) {
            return false;
        }

        TFileStat fs;
        filePath.Stat(fs);
        if (fs.MTime == modificationTime) {
            // Such optimization may bring problems in the future, since MTime is rather unreliable.
            //
            // return true;
        }

        TFileInput ufi(filePath);
        const TString buffer = ufi.ReadAll();
        TString curMd5 = NConfReader::CalculateConfMd5(buffer, ignoredHashContent);
        return md5 == curMd5;
    }

    bool CheckImportedFiles(const TVector<TImportedFileDescription>& importedFiles,
                            const TBuildConfiguration& conf,
                            TString* ignoredHashContent) {
        // The cache actuality criteria is as follows:
        //  - all listed files:
        //    - exist
        //    - has actual TS and if the TS is newer then fixed,
        //    - stored Hashsum is the same with file.
        TString fileName;
        for (const auto& [name, hash, modTime] : importedFiles) {
            fileName.clear();
            if (name == "-") {
                fileName = conf.YmakeConf;
            } else if (NPath::IsTypedPath(name)) {
                if (NPath::IsType(name, NPath::Source)) {
                    fileName = conf.SourceRoot / NPath::CutType(name);
                } else if (NPath::IsType(name, NPath::Build)) {
                    fileName = conf.BuildRoot / NPath::CutType(name);
                }
            }
            if (fileName.empty()) {
                YDebug() << "Unexpected name for imported conf file: [" << name << "]...";
                return false;
            }
            if (!IsActualFile(fileName, hash, modTime, ignoredHashContent)) {
                return false;
            }
        }

        return true;
    }

    ELoadStatus LoadCache(TBuildConfiguration& conf, TMd5Sig& confMd5) {
        const TFsPath& cacheFile = conf.YmakeConfCache;
        if (!cacheFile.Exists()) {
            return ELoadStatus::DoesNotExist;
        }

        TFileInput cacheStream(cacheFile);
        ui32 magic;
        ::Load(&cacheStream, magic);
        if (magic != MAGIC) {
            return ELoadStatus::UnknownFormat;
        }
        ui32 version;
        ::Load(&cacheStream, version);
        if (VERSION != version) {
            return ELoadStatus::VersionMismatch;
        }
        TVector<TImportedFileDescription> importedFiles;
        ::Load(&cacheStream, importedFiles);
        TString ignoredHashContent;
        if (!CheckImportedFiles(importedFiles, conf, &ignoredHashContent)) {
            return ELoadStatus::ConfigurationChanged;
        }
        ::Load(&cacheStream, conf);
        ::Load(&cacheStream, confMd5);
        if (!ignoredHashContent.empty()) {
            // Update the values of those vars which may depend on system and were
            // not taken into account for calcualtion of configuration hash
            conf.LoadConfigFromContext(ignoredHashContent);
        }

        conf.SetFromCache(true);

        return ELoadStatus::Success;
    }

    ESaveStatus SaveCache(TBuildConfiguration& conf, const TMd5Sig& confMd5) {
        const TFsPath& cachePathTemp = TString::Join(conf.YmakeConfCache.GetPath(), "."sv, ToString(GetPID()));
        TFile tempFile{cachePathTemp, CreateAlways | WrOnly};
        {
            TFileOutput cacheStream(tempFile);
            ::Save(&cacheStream, MAGIC);
            ::Save(&cacheStream, VERSION);
            ::Save(&cacheStream, conf.ImportedFiles);
            ::Save(&cacheStream, conf);
            ::Save(&cacheStream, confMd5);
        }
        tempFile.Close();
        cachePathTemp.RenameTo(conf.YmakeConfCache);
        return ESaveStatus::Success;
    }

}

namespace NConfReader {
    ELoadStatus LoadCache(TBuildConfiguration& conf, TMd5Sig& confMd5) {
        NYMake::TTraceStage stage("Load configuration from cache");
        TString ignoredHashContent;
        auto status = ELoadStatus::Success;
        try {
            status = ::LoadCache(conf, confMd5);
        } catch (...) {
            status = ELoadStatus::UnhandledException;
        }
        if (status == ELoadStatus::Success) {
            if (!ignoredHashContent.empty()) {
                // Update the values of those vars which may depend on system and were
                // not taken into account for calcualtion of configuration hash
                conf.LoadConfigFromContext(ignoredHashContent);
            }
            YDebug() << "Conf cache has been loaded..." << Endl;
        } else {
            conf.ClearYmakeConfig();
            YDebug() << "Conf cache has not been loaded (reason: " << status << ")..." << Endl;
        }
        return status;
    }

    ESaveStatus SaveCache(TBuildConfiguration& conf, const TMd5Sig& confMd5) {
        NYMake::TTraceStage stage("Save configuration to cache");
        auto status = ESaveStatus::Success;
        try {
            status = ::SaveCache(conf, confMd5);
        } catch (...) {
            status = ESaveStatus::UnhandledException;
        }
        if (status == ESaveStatus::Success) {
            YDebug() << "Conf cache has been saved..." << Endl;
        } else {
            YDebug() << "Conf cache has not been saved (reason: " << status << ")..." << Endl;
        }
        return status;
    }
}
