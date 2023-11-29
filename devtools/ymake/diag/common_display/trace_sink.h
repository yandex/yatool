#pragma once

#include <util/generic/string.h>

#include <type_traits>

namespace NYMake {

    struct ITraceSink {
        virtual void Trace(const TString& ev) = 0;
        virtual ~ITraceSink() noexcept = default;
    };

    class TTraceSinkScope {
    public:
        static ITraceSink* CurrentSink() noexcept {return Current_;}
    protected:
        static ITraceSink* Current_;
    };

    template<typename T>
        requires std::derived_from<T, ITraceSink>
    class TScopedTraceSink: public T, private TTraceSinkScope {
    public:
        template<typename... Args>
        TScopedTraceSink(Args&&... args): T{std::forward<Args>(args)...}, Old_{TTraceSinkScope::Current_} {
            TTraceSinkScope::Current_ = this;
        }
        ~TScopedTraceSink() {TTraceSinkScope::Current_ = Old_;}

    private:
        ITraceSink* Old_;
    };

}
