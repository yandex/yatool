LIBRARY()

SRCS(
    ffi_macro.cpp
    raii.cpp
    signature_conversion.cpp
)
PEERDIR(devtools/ymake/lang)
USE_PYTHON3()

END()

RECURSE(ut)
