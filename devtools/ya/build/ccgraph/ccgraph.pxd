cdef extern from "devtools/ya/cpp/graph/graph.h" namespace "NYa::NGraph":
    cdef cppclass TGraphPtr:
        pass


cdef class Graph:
    cdef TGraphPtr graph
