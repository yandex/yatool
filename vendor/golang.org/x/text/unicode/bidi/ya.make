GO_LIBRARY()

LICENSE(BSD-3-Clause)

VERSION(v0.26.0)

SRCS(
    bidi.go
    bracket.go
    core.go
    prop.go
    tables15.0.0.go
    trieval.go
)

GO_TEST_SRCS(
    bidi_test.go
    core_test.go
    ranges_test.go
    tables_test.go
)

END()

RECURSE(
    gotest
)
