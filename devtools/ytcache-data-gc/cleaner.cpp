#include "cleaner.h"
#include "logger.h"

#include <yt/cpp/mapreduce/interface/client.h>
#include <yt/cpp/mapreduce/interface/operation.h>
#include <yt/cpp/mapreduce/util/ypath_join.h>

#include <util/generic/guid.h>
#include <util/generic/hash_set.h>
#include <util/generic/ptr.h>
#include <util/generic/scope.h>
#include <util/generic/size_literals.h>
#include <util/generic/xrange.h>
#include <util/generic/utility.h>

#include <ctime>

const NYT::TYPath TMP_DIR = "//tmp";
constexpr i64 MIN_DATA_AGE_SECONDS = 30 * 60; // Remove orphan data record only if it is older than MIN_DATA_AGE_SECONDS
constexpr i64 DATA_SIZE_PER_JOB = 3_GB;
constexpr i64 DATA_SIZE_PER_KEY_RANGE = 1_TB;
constexpr i64 MAX_CHUNK_SIZE = 5'000;

using TKnownHashesSet = THashSet<TString>;

void YtJobEntry(int argc, const char* argv[]) {
    NYT::Initialize(argc, argv);
}

class TOrphanHashesExtractor : public NYT::IMapper<NYT::TTableReader<NYT::TNode>, NYT::TTableWriter<NYT::TNode>> {
private:
    ui64 TimeThreshold_;
    TKnownHashesSet KnownHashes_;
public:
    TOrphanHashesExtractor()
    {
    }

    TOrphanHashesExtractor(ui64 timeThreshold, const TKnownHashesSet& knownHashes)
        : TimeThreshold_{timeThreshold}
        , KnownHashes_{knownHashes}
    {
    }

    void Do(NYT::TTableReader<NYT::TNode>* input, NYT::TTableWriter<NYT::TNode>* output) override {
        for (; input->IsValid(); input->Next()) {
            auto row = input->GetRow();
            const NYT::TNode& createTimeNode = row["create_time"];
            if (createTimeNode.IsNull()) {
                continue;
            }
            TString hash = row["hash"].AsString();
            ui64 chunk_i = row["chunk_i"].AsUint64();
            ui64 createTime = createTimeNode.AsUint64();
            if (!KnownHashes_.contains(hash) && createTime < TimeThreshold_) {
                output->AddRow(NYT::TNode()("hash", hash)("chunk_i", chunk_i));
            }
        }
    }

    Y_SAVELOAD_JOB(TimeThreshold_, KnownHashes_);
};
REGISTER_MAPPER(TOrphanHashesExtractor);

struct TKeyRange {
    ui64 Begin;
    ui64 End;

    TString AsString() const {
        return ToString(Begin) + "-" + ToString(End);
    }

    NYT::TReadRange AsReadRange() const {
        return NYT::TReadRange::FromKeys(NYT::TNode(Begin), NYT::TNode(End));
    }
};

TVector<TKeyRange> GetDataTableKeyRanges(NYT::IClientPtr ytc, const NYT::TYPath& dataTablePath) {
    TVector<TKeyRange> keyRanges;
    NYT::TNode::TListType pivotKeys = ytc->Get(dataTablePath + "/@pivot_keys").AsList();
    // Estimate max pivot key value.
    Y_ASSERT(pivotKeys.size() > 1);
    ui64 lastPivotBegin = pivotKeys.back()[0].AsUint64();
    ui64 maxKeyValue = lastPivotBegin + lastPivotBegin / (pivotKeys.size() - 1);

    i64 dataSize = ytc->Get(dataTablePath + "/@uncompressed_data_size").AsInt64();
    i64 rangeCount = Max(dataSize / DATA_SIZE_PER_KEY_RANGE, 1l);
    ui64 step = maxKeyValue / rangeCount;
    ui64 rem = maxKeyValue % rangeCount;
    ui64 begin = 0;
    for (int i = 1; i < rangeCount; i++) {
        ui64 end = step * i + rem * i / rangeCount;
        keyRanges.push_back({begin, end});
        begin = end;
    }
    keyRanges.push_back({begin, Max<ui64>()});
    return keyRanges;
}

TKnownHashesSet ReadKnownHashes(NYT::IClientPtr ytc, const NYT::TYPath& metadataTablePath) {
    LDEBUG("Start getting known hashes");

    i64 selectRowsLimit = ytc->Get(metadataTablePath + "/@chunk_row_count").AsInt64() * 3 / 2;
    LDEBUG("Select rows limit: %d", selectRowsLimit);

    TKnownHashesSet hashes{};
    auto selectRowsOptions = NYT::TSelectRowsOptions()
        .InputRowLimit(selectRowsLimit)
        .OutputRowLimit(selectRowsLimit);
    auto rows = ytc->SelectRows("hash from [" + metadataTablePath + "]", selectRowsOptions);
    hashes.reserve(rows.size());
    for (const auto& row : rows) {
        hashes.insert(row["hash"].AsString());
    }
    LDEBUG("Finish getting known hashes");
    return hashes;
}

NYT::TYPath FindOrphanRows(NYT::IClientBasePtr tx, NYT::TYPath dataTablePath, const TKnownHashesSet &knownHashes, const TKeyRange& keyRange, const i64 jobMemoryLimit, ui64 timeThreshold) {
    NYT::TRichYPath richDataPath = NYT::TRichYPath(dataTablePath)
        .Columns({"hash", "chunk_i", "create_time"})
        .AddRange(keyRange.AsReadRange());
    NYT::TYPath destTable = NYT::JoinYPaths(TMP_DIR, "ytcache-data-gc-" + CreateGuidAsString());
    auto spec = NYT::TMapOperationSpec()
        .AddInput<NYT::TNode>(richDataPath)
        .AddOutput<NYT::TNode>(destTable)
        // YT overestimate required job count because of large 'data' column size
        .DataSizePerJob(DATA_SIZE_PER_JOB);

    auto operationSpec = NYT::TNode()("mapper", NYT::TNode()("memory_limit", jobMemoryLimit));

    const TString rangeName = keyRange.AsString();
    LDEBUG("Start map operation for range %s", rangeName.c_str());
    auto mapper = MakeIntrusive<TOrphanHashesExtractor>(timeThreshold, knownHashes);
    tx->Map(spec, mapper, NYT::TOperationOptions().Spec(operationSpec));
    LDEBUG("Finish map operation for range %s", rangeName.c_str());
    return destTable;
}

// Dynamic table operations can not run in cypress transaction, so pass both clients
void DeleteOrphanRows(NYT::IClientPtr ytc, NYT::IClientBasePtr tx, const NYT::TYPath& dataTablePath, const NYT::TYPath& orphanHashesTable) {
    auto reader = tx->CreateTableReader<NYT::TNode>(orphanHashesTable);
    while (reader->IsValid()) {
        NYT::TNode::TListType keys;

        for (; reader->IsValid() && keys.size() < MAX_CHUNK_SIZE; reader->Next()) {
            keys.push_back(std::move(reader->GetRow()));
        }

        if (keys.empty())
            break;

        ytc->DeleteRows(dataTablePath, keys, NYT::TDeleteRowsOptions().Atomicity(NYT::EAtomicity::None));
    }
}

i64 CleanDataRange(NYT::IClientPtr ytc, const NYT::TYPath& dataTablePath, const bool dryRun, const TKnownHashesSet& knownHashes, const TKeyRange& keyRange, const i64 jobMemoryLimit, ui64 timeThreshold) {
    auto tx = ytc->StartTransaction();
    NYT::TYPath orphanHashesTable = FindOrphanRows(tx, dataTablePath, knownHashes, keyRange, jobMemoryLimit, timeThreshold);
    i64 rowCount = tx->Get(orphanHashesTable + "/@row_count").AsInt64();
    if (rowCount) {
        if (!dryRun) {
            DeleteOrphanRows(ytc, tx, dataTablePath, orphanHashesTable);
            LDEBUG("Range:%s. %d orphan rows found and deleted", keyRange.AsString().c_str(), rowCount);
        } else {
            LDEBUG("Range:%s. %d orphan rows found", keyRange.AsString().c_str(), rowCount);
        }
    } else {
        LDEBUG("Range:%s. No orphan rows found", keyRange.AsString().c_str());
    }
    tx->Remove(orphanHashesTable, NYT::TRemoveOptions().Force(true));
    tx->Commit();

    return rowCount;
}

void DoCleanData(const TString& ytProxy, const TString& ytToken, const TString& ytDir, const TString& metadataTable, const TString& dataTable, const i64 jobMemoryLimit, const bool dryRun) {
    auto ytc = CreateClient(ytProxy, NYT::TCreateClientOptions().Token(ytToken));

    ui64 timeThreshold = (std::time(nullptr) - MIN_DATA_AGE_SECONDS) * 1'000'000;
    LDEBUG("Time threshold=%lu", timeThreshold);

    NYT::TYPath metadataTablePath = NYT::JoinYPaths(ytDir, metadataTable);
    TKnownHashesSet knownHashes = ReadKnownHashes(ytc, metadataTablePath);
    LINFO("Read %d hashes from %s", knownHashes.size(), metadataTablePath.c_str());

    i64 deletedRowCount = 0;
    NYT::TYPath dataTablePath = NYT::JoinYPaths(ytDir, dataTable);
    const TVector<TKeyRange> keyRanges = GetDataTableKeyRanges(ytc, dataTablePath);
    LDEBUG("Map operations count: %d", keyRanges.size());
    for (const auto& keyRange : keyRanges) {
        deletedRowCount += CleanDataRange(ytc, dataTablePath, dryRun, knownHashes, keyRange, jobMemoryLimit, timeThreshold);
    }
    if (deletedRowCount) {
        if (dryRun) {
            LINFO("Dry run (nothing actually changed). %d orphan rows found", deletedRowCount);
        } else {
            LINFO("%d orphan rows deleted", deletedRowCount);
        }
    } else {
        LDEBUG("No orphan rows found");
    }
}
