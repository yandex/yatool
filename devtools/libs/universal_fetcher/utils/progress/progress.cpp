#include "progress.h"

namespace NUniversalFetcher {

    THolder<IOutputStream> MakeProgressOutputStreamIfNeeded(const TMaybe<TFetchParams::TProgressReporting>& params, ui64 totalSize, THolder<IOutputStream> stream) {
        if (!params) {
            return stream;
        }
        THolder<TProgressOutputStream> progressStream = MakeHolder<TProgressOutputStream>([&params, totalSize](ui64 downloaded) {
            params->Callback(downloaded, totalSize);
        }, params->MinInterval, std::move(stream));
        return progressStream;
    }

}

