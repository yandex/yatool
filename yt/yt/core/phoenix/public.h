#pragma once

#include <library/cpp/yt/memory/ref_counted.h>

#include <library/cpp/yt/misc/strong_typedef.h>

namespace NYT::NPhoenix2 {

////////////////////////////////////////////////////////////////////////////////

class TFieldDescriptor;
class TTypeDescriptor;
class TUniverseDescriptor;

struct TPolymorphicBase;

////////////////////////////////////////////////////////////////////////////////

using TPolymorphicConstructor = TPolymorphicBase* (*)();
using TConcreteConstructor = void* (*)();

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_STRUCT(TFieldSchema);
DECLARE_REFCOUNTED_STRUCT(TTypeSchema);
DECLARE_REFCOUNTED_STRUCT(TUniverseSchema);

////////////////////////////////////////////////////////////////////////////////

YT_DEFINE_STRONG_TYPEDEF(TTypeTag, ui32);
YT_DEFINE_STRONG_TYPEDEF(TFieldTag, ui32);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NPhoenix2
