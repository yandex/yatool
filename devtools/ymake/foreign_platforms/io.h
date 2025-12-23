#pragma once

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_awaitable.hpp>
#include <util/generic/fwd.h>
#include <util/stream/input.h>

#include <devtools/ymake/symbols/file_store.h>

namespace NForeignTargetPipeline {

class TLineReader {
public:
    virtual asio::awaitable<std::optional<TString>> ReadLine() = 0;
    virtual ~TLineReader() noexcept = default;
};

class TStreamLineReader : public TLineReader {
public:
    explicit TStreamLineReader(IInputStream& input)
        : ReadPool_(1)
        , Input_(input)
    {}

    asio::awaitable<std::optional<TString>> ReadLine() override {
        return asio::co_spawn(ReadPool_.executor(), [this]() -> asio::awaitable<std::optional<TString>> {
            TString line;
            if (Input_.ReadLine(line)) {
                co_return std::make_optional(line);
            }
            co_return std::nullopt;
        }, asio::use_awaitable);
    }

private:
    asio::thread_pool ReadPool_;
    IInputStream& Input_;
};

class TLineWriter {
public:
    virtual void WriteLine(const TString& target) = 0;
    virtual void WriteLineUniq(const TFileView& fileview, const TString& target) = 0;
    virtual void WriteBypassLine(const TString& target) = 0;
    virtual ~TLineWriter() noexcept = default;
};

} // namespace NForeignTargetPipeline
