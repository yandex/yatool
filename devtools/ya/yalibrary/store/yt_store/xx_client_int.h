#pragma once

#ifndef XX_CLIENT_INT_H_
#error "Inclusion of this file by user code is not allowed"
#endif
#undef XX_CLIENT_INT_H_

#include "xx_client.hpp"

#include <yt/cpp/mapreduce/interface/client.h>


namespace NYa {
    // Allow to change an implementation in tests
    struct IYtClusterConnector : TThrRefBase {
        virtual NYT::IClientPtr operator() (const TString& proxy, const NYT::TCreateClientOptions& options) = 0;
        virtual ~IYtClusterConnector() = default;
    };
    using IYtClusterConnectorPtr = TIntrusivePtr<IYtClusterConnector>;
    extern IYtClusterConnectorPtr YtClusterConnectorPtr;

    struct TYtStore2::TInternalState {
        struct TReplica {
            TString Proxy;
            NYT::TYPath DataDir;
            TDuration Lag;

            friend bool operator== (const TYtStore2::TInternalState::TReplica&, const TYtStore2::TInternalState::TReplica&) = default;
        };

        NYT::EAtomicity Atomicity;
        int Version{};
        TVector<TReplica> GoodReplicas;
        TReplica PreparedReplica;
        friend bool operator== (const TYtStore2::TInternalState&, const TYtStore2::TInternalState&) = default;
    };
 }
