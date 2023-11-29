#pragma once

template<typename T>
class TRestoreGuard {
public:
    TRestoreGuard(T& valueRef)
        : ValueRef_(valueRef)
        , SavedValue_(valueRef)
    {}

    ~TRestoreGuard() {
        ValueRef_ = SavedValue_;
    }

private:
    T& ValueRef_;
    const T SavedValue_;
};
