#include "devtools/examples/tutorials/protobuf/example3/book/book.pb.h"
#include "devtools/examples/tutorials/protobuf/example3/page/page.pb.h"

#include "util/generic/string.h"

#include <iostream>


int main() {
    book::Book book;

    page::Page* page = book.add_pages();
    page->set_text("This is the first page");

    TString str;
    Y_PROTOBUF_SUPPRESS_NODISCARD book.SerializeToString(&str);

    book::Book book2;
    Y_PROTOBUF_SUPPRESS_NODISCARD book2.ParseFromString(str);

    std::cout << book2.pages(0).text() << std::endl;

    return 0;
}
