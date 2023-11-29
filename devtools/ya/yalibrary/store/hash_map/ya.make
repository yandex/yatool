PY23_LIBRARY()

SRCS(open_hash_map.cpp)

PY_SRCS(TOP_LEVEL open_hash_map.pyx)

END()

RECURSE(ut)
