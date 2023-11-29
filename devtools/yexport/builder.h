#pragma once

#include <span>


template <typename TSubdirsTableElem, typename TBuilderTarget>
class TGeneratorBuilder {
public:
    class TTargetHolder;

    const TBuilderTarget* CurrentTarget() const noexcept {return CurTarget;}

    const TSubdirsTableElem* CurrentList() const noexcept {return CurList;}

protected:
    TSubdirsTableElem* CurList = nullptr;
    TBuilderTarget* CurTarget = nullptr;
};

template <typename TSubdirsTableElem, typename TBuilderTarget>
class TGeneratorBuilder<TSubdirsTableElem, TBuilderTarget>::TTargetHolder {
public:
    TTargetHolder() noexcept = default;

    TTargetHolder(const TTargetHolder&) = delete;
    TTargetHolder& operator=(const TTargetHolder&) = delete;

    TTargetHolder(TTargetHolder&& other) noexcept
        : Builder{std::exchange(other.Builder, nullptr)}
        , PrevList{other.PrevList}
        , PrevTarget(other.PrevTarget)
    {}
    TTargetHolder& operator=(TTargetHolder&& other) noexcept {
        if (Builder) {
            Builder->CurTarget = PrevTarget;
            Builder->CurList = PrevList;
        }
        Builder = std::exchange(other.Builder, nullptr);
        PrevList = other.PrevList;
        PrevTarget = other.PrevTarget;
        return *this;
    }

    TTargetHolder(TGeneratorBuilder& builder, TSubdirsTableElem& list, TBuilderTarget& target) noexcept: Builder{&builder} {
        PrevList = std::exchange(builder.CurList, &list);
        PrevTarget = std::exchange(builder.CurTarget, &target);
    }

    ~TTargetHolder() noexcept {
        if (Builder) {
            Builder->CurTarget = PrevTarget;
            Builder->CurList = PrevList;
        }
    }

private:
    TGeneratorBuilder* Builder = nullptr;
    TSubdirsTableElem* PrevList = nullptr;
    TBuilderTarget* PrevTarget = nullptr;
};
