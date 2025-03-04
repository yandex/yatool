#pragma once

#include "throughput_throttler.h"

#include "yt/yt/core/ytree/yson_struct.h"

namespace NYT::NConcurrency {

////////////////////////////////////////////////////////////////////////////////

struct TFairThrottlerConfig
    : public NYTree::TYsonStruct
{
    i64 TotalLimit;

    TDuration DistributionPeriod;

    int BucketAccumulationTicks;

    int GlobalAccumulationTicks;

    std::optional<TString> IpcPath;

    REGISTER_YSON_STRUCT(TFairThrottlerConfig);

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TFairThrottlerConfig)

////////////////////////////////////////////////////////////////////////////////

struct TFairThrottlerBucketConfig
    : public NYTree::TYsonStruct
{
    double Weight;

    std::optional<i64> Limit;
    std::optional<double> RelativeLimit;
    std::optional<i64> GetLimit(i64 totalLimit);

    std::optional<i64> Guarantee;
    std::optional<double> RelativeGuarantee;
    std::optional<i64> GetGuarantee(i64 totalLimit);

    REGISTER_YSON_STRUCT(TFairThrottlerBucketConfig);

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TFairThrottlerBucketConfig)

////////////////////////////////////////////////////////////////////////////////

struct IIpcBucket
    : public TRefCounted
{
    // NB: This struct is shared between processes. All changes must be backward compatible.
    struct TState
    {
        std::atomic<double> Weight;
        std::atomic<i64> Limit;
        std::atomic<i64> Demand;
        std::atomic<i64> InFlow;
        std::atomic<i64> OutFlow;
        std::atomic<i64> GuaranteedQuota;
    };

    virtual TState* GetState() = 0;
};

DEFINE_REFCOUNTED_TYPE(IIpcBucket)

////////////////////////////////////////////////////////////////////////////////

struct IThrottlerIpc
    : public TRefCounted
{
    // NB: This struct is shared between processes. All changes must be backward compatible.
    struct TSharedBucket
    {
        std::atomic<i64> Value = 0;
    };

    virtual bool TryLock() = 0;
    virtual TSharedBucket* State() = 0;
    virtual std::vector<IIpcBucketPtr> ListBuckets() = 0;
    virtual IIpcBucketPtr AddBucket() = 0;
};

IThrottlerIpcPtr CreateFileThrottlerIpc(const TString& path);

DEFINE_REFCOUNTED_TYPE(IThrottlerIpc)

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_STRUCT(TSharedBucket)

//! TFairThrottler manages a group of throttlers, distributing traffic according to fair share policy.
/*!
 *  TFairThrottler distributes TotalLimit * DistributionPeriod bytes every DistributionPeriod.
 *
 *  At the beginning of the period, TFairThrottler distributes new quota between buckets. Buckets
 *  accumulate quota for N ticks. After N ticks overflown quota is transferred into shared bucket.
 *  Shared bucket accumulates quota for M ticks. Overflown quota from shared bucket is discarded.
 *
 *  Throttled requests may consume quota from both local bucket and shared bucket.
 */
class TFairThrottler
    : public TRefCounted
{
public:
    TFairThrottler(
        TFairThrottlerConfigPtr config,
        NLogging::TLogger logger,
        NProfiling::TProfiler profiler);

    IThroughputThrottlerPtr CreateBucketThrottler(
        const std::string& name,
        TFairThrottlerBucketConfigPtr config);

    void Reconfigure(
        TFairThrottlerConfigPtr config,
        const THashMap<std::string, TFairThrottlerBucketConfigPtr>& bucketConfigs);

    static std::vector<i64> ComputeFairDistribution(
        i64 totalLimit,
        const std::vector<double>& weights,
        const std::vector<i64>& demands,
        const std::vector<std::optional<i64>>& limits);

private:
    const NLogging::TLogger Logger;
    const NProfiling::TProfiler Profiler_;

    TSharedBucketPtr SharedBucket_;
    std::atomic<bool> IsLeader_ = false;

    struct TBucket
    {
        TFairThrottlerBucketConfigPtr Config;
        TBucketThrottlerPtr Throttler;
        IIpcBucketPtr Ipc;
    };

    // Protects all Config_ and Buckets_.
    YT_DECLARE_SPIN_LOCK(NThreading::TSpinLock, Lock_);
    TFairThrottlerConfigPtr Config_;
    THashMap<std::string, TBucket> Buckets_;

    IThrottlerIpcPtr Ipc_;

    void DoUpdateLeader();
    void DoUpdateFollower();
    void RefillFromSharedBucket();
    void UpdateLimits(TInstant at);
    void ScheduleLimitUpdate(TInstant at);
};

DEFINE_REFCOUNTED_TYPE(TFairThrottler)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NConcurrency
