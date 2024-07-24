#include "say_hello.h"

#include <util/stream/str.h>

#include <malloc.h>
#include <string.h>


extern "C"
char* SayHello(const char* name) {
    TString str;
    TStringOutput output{str};
    output << "Hello, " << (name ? name : "") << "!";
    char* buff = (char*)malloc(str.size() + 1);
    return strcpy(buff, str.data());
}
