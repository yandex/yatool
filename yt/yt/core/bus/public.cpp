#include "public.h"

namespace NYT::NBus {

////////////////////////////////////////////////////////////////////////////////

const TString DefaultNetworkName("default");
const TString LocalNetworkName("local");

////////////////////////////////////////////////////////////////////////////////

EMultiplexingBand GetDefaultValue(EMultiplexingBand)
{
    return EMultiplexingBand::Default;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NBus