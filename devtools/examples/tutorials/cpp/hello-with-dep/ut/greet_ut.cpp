#include <devtools/examples/tutorials/cpp/hello-with-dep/greet.h>

#include <util/stream/str.h>

#include <library/cpp/testing/unittest/registar.h>

Y_UNIT_TEST_SUITE(GreetTest) {
    Y_UNIT_TEST(PrintsToProvidedStream) {
        TStringStream out;
        greet(out, "World");
        UNIT_ASSERT_EQUAL(out.Str(), "Hello World\n");
    }

    Y_UNIT_TEST(GreetsProvidedName) {
        TStringStream out;
        greet(out, "Vasja");
        UNIT_ASSERT_EQUAL(out.Str(), "Hello Vasja\n");
    }
}
