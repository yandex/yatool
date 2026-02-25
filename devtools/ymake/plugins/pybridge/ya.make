LIBRARY()

SRCS(
    raii.cpp
    signature_conversion.cpp
)
PEERDIR(devtools/ymake/lang)
USE_PYTHON3()

END()

RECURSE(ut)
