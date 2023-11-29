#include "loaders.h"

namespace NYa::NEdl {
    namespace {
        template <class T>
        inline void RejectValue(TStringBuf type, const T& val)  {
            ythrow TLoaderError() <<  type << " value ('" << val << "') is not expected";
        }
    }

    void TBaseLoader::SetValue(nullptr_t) {
        ythrow TLoaderError() <<  "value 'null' is not expected";
    }

    void TBaseLoader::SetValue(bool val) {
        RejectValue("boolean", val ? "true" : "false");
    }

    void TBaseLoader::SetValue(long long val) {
        RejectValue("integer", val);
    }

    void TBaseLoader::SetValue(unsigned long long val) {
        RejectValue("uinteger", val);
    }

    void TBaseLoader::SetValue(double val) {
        RejectValue("float", val);
    }

    void TBaseLoader::SetValue(const TStringBuf val) {
        RejectValue("string", val);
    }

    void TBaseLoader::EnsureMap() {
        ythrow TLoaderError() << "map is not expected";
    }

    TLoaderPtr TBaseLoader::AddMapValue(TStringBuf) {
        Y_UNREACHABLE(); // StartMap should protect this function from calling
    }

    void TBaseLoader::EnsureArray() {
        ythrow TLoaderError() << "array is not expected";
    }

    TLoaderPtr TBaseLoader::AddArrayValue() {
        Y_UNREACHABLE(); // StartArray should protect this function from calling
    }

    void TBaseLoader::Finish() {
    }

    void TBlackHoleLoader::SetValue(nullptr_t) {
    }

    void TBlackHoleLoader::SetValue(bool) {
    }

    void TBlackHoleLoader::SetValue(long long) {
    }

    void TBlackHoleLoader::SetValue(unsigned long long) {
    }

    void TBlackHoleLoader::SetValue(double) {
    }

    void TBlackHoleLoader::SetValue(const TStringBuf) {
    }

    void TBlackHoleLoader::EnsureMap() {
    }

    TLoaderPtr TBlackHoleLoader::AddMapValue(TStringBuf) {
        return MakeHolder<TBlackHoleLoader>();
    }

    void TBlackHoleLoader::EnsureArray() {
    }

    TLoaderPtr TBlackHoleLoader::AddArrayValue() {
        return MakeHolder<TBlackHoleLoader>();
    }

    void TBlackHoleLoader::Finish() {
    }
}
