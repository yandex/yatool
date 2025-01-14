#pragma once

#include <util/generic/maybe.h>
#include <util/generic/ptr.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/va_args.h>


// Mostly inspired by (and copy-pasted from, of course) https://stackoverflow.com/a/11744832/18250613

#define Y_EDL_REM(...) __VA_ARGS__
#define Y_EDL_EAT(...)
// Strip off the type
#define Y_EDL_STRIP(x) Y_EDL_EAT x
// Show the type without parenthesis
#define Y_EDL_PAIR(x) Y_EDL_REM x

namespace NYa::NEdl {
    enum class EMemberExportPolicy {
        NOT_EMPTY,
        ALWAYS
    };

    inline TString ToSnake(TStringBuf camel) {
        TString snake;
        bool inWord = false;
        for (char c : camel) {
            if (isupper(c)) {
                if (inWord) {
                    snake += '_';
                }
                inWord = false;
                snake += tolower(c);
            } else {
                snake += c;
                inWord = true;
            }
        }
        return snake;
    }

    template <class T>
    concept CHasEdlMemberInfo = requires {
        T::EdlMemberCount;
    };

    template <class T>
    concept CHasEdlDefaultMember = requires(T t) {
        T::template GetDefaultMemberRef<T>(t);
    };

}

#define Y_EDL_MEMBER_IMPL(n, member, name, exportPolicy) \
    Y_EDL_PAIR(member); \
    \
    template <> \
    /* n is a sequence N..1. Subtract n from member count to get 0..(N-1) */ \
    struct EdlMemberInfo<EdlMemberCount - n> { \
        template <class T> \
        inline static decltype(Y_EDL_STRIP(member))& GetRef(T& self) noexcept { \
            return self.Y_EDL_STRIP(member); \
        } \
        template <class T> \
        inline static const decltype(Y_EDL_STRIP(member))& GetRef(const T& self) noexcept { \
            return self.Y_EDL_STRIP(member); \
        } \
        [[maybe_unused]] static constexpr TStringBuf ExportName = name; \
        [[maybe_unused]] static constexpr TStringBuf Name = Y_STRINGIZE(Y_EDL_STRIP(member)); \
        \
        [[maybe_unused]] static constexpr ::NYa::NEdl::EMemberExportPolicy ExportPolicy = exportPolicy; \
        \
        inline static TString GetExportName() { \
            static const TString exportName = ExportName ? TString{ExportName} : ::NYa::NEdl::ToSnake(Name); \
            return exportName; \
        } \
    };

#define Y_EDL_MEMBER1(n, member) Y_EDL_MEMBER_IMPL(n, member, "", (::NYa::NEdl::EMemberExportPolicy::NOT_EMPTY))
#define Y_EDL_MEMBER2(n, member, name) Y_EDL_MEMBER_IMPL(n, member, name, (::NYa::NEdl::EMemberExportPolicy::NOT_EMPTY))
#define Y_EDL_MEMBER3(n, member, name, exportPolicy) Y_EDL_MEMBER_IMPL(n, member, name, exportPolicy)
#define Y_EDL_MEMBERX(n, args...) Y_PASS_VA_ARGS(Y_MACRO_IMPL_DISPATCHER_3(args, Y_EDL_MEMBER3, Y_EDL_MEMBER2, Y_EDL_MEMBER1)(n, args))
#define Y_EDL_MEMBER(n, member) Y_EDL_MEMBERX(n, Y_EDL_PAIR(member))

/*
 *    // Define struct:
 *
 *    struct TMyData {
 *        Y_EDL_MEMBERS(
 *            ((int) MemberN1),             // ExportName = member_n1
 *            ((int) MemberN2, "member_2"), // ExportName = member_2
 *            ((int) MemberN3, "", ::NYa::NEdl::EMemberExportPolicy::ALWAYS) // Export zero/empty value
 *        )
 *    };
 *
 *    // Get member count:
 *
 *    Cerr << TMyData::EdlMemberCount;
 *
 *    // To get a member reference and names:
 *
 *    TMyData data;
 *    TMyData::template EdlMemberInfo<0>::GetRef(data) // Get ref to the first data member
 *    TMyData::template EdlMemberInfo<0>::ExportName // Raw export name (as defined by user). May be empty
 *    TMyData::template EdlMemberInfo<0>::GetExportName() // Real export name
 *    TMyData::template EdlMemberInfo<0>::Name // Get field name as is
 *    TMyData::template EdlMemberInfo<0>::ExportPolicy // Get export policy
 */

#define Y_EDL_MEMBERS(members...) \
    static constexpr size_t EdlMemberCount = Y_COUNT_ARGS(members); \
    \
    template <int I> \
    struct EdlMemberInfo; \
    \
    Y_MAP_ARGS_N(Y_EDL_MEMBER, members)

/*
 * During loading all unknown member names are passed to the default member loader.
 * The default member fields are exported as subfields of parent struct.
 *
 * Example 1:
 *
 *        struct A {
 *            Y_EDL_MEMBERS(
 *                ((int) FieldA)
 *            )
 *        };
 *
 *        struct B {
 *            Y_EDL_MEMBERS(
 *                ((int) FieldB)
 *            )
 *
 *            Y_EDL_DEFAULT_MEMBER((A) Inner)
 *        };
 *
 *    If we try to load the following json into B(), '2' is loaded into B().FieldB and '1' is loaded into B().Inner.FieldA.
 *    "inner" field will produce 'Unexpected map key' error.
 *    {
 *        "filed_a": 1,
 *        "filed_b": 2
 *        // "inner": ... - not possible
 *    }
 *    Exporting to json produces the same value as above.
 *
 * Example 2:
 *
 *    Y_EDL_DEFAULT_MEMBER((THash<TString, NJson::TJsonValue>) Unknown)
 *
 * Keep in 'Unknown' all fields which we don't process but must preserve between load and export.
 *
 * Notes:
 *    - Calling of OpenMap() and CloseMap() methods from default member exporter is ignored because default member items don't create new dictionary but extend existing one.
 */

#define Y_EDL_DEFAULT_MEMBER(member) \
    Y_EDL_PAIR(member); \
    \
    template <class T> \
    inline static decltype(Y_EDL_STRIP(member))& GetDefaultMemberRef(T& self) noexcept { \
        return self.Y_EDL_STRIP(member); \
    } \
    \
    template <class T> \
    inline static const decltype(Y_EDL_STRIP(member))& GetDefaultMemberRef(const T& self) noexcept { \
        return self.Y_EDL_STRIP(member); \
    } \
