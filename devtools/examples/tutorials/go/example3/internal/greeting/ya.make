GO_LIBRARY()

SRCS(greeting.go)

GO_TEST_SRCS(greeting_test.go)

END()

RECURSE(gotest)
