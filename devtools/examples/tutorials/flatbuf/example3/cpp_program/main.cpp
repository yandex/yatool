#include <devtools/examples/tutorials/flatbuf/example3/page/page.fbs.h>
#include <devtools/examples/tutorials/flatbuf/example3/library/book.fbs.h>
#include <devtools/examples/tutorials/flatbuf/example3/library/genre.fbs.h>
#include <contrib/libs/flatbuffers/include/flatbuffers/flatbuffers.h>
#include <util/stream/output.h>

int main() {
    flatbuffers::FlatBufferBuilder builder;

    std::vector<flatbuffers::Offset<library::page::Page>> pages;
    pages.push_back(library::page::CreatePage(builder, builder.CreateString("Content of page 1"), 1));
    pages.push_back(library::page::CreatePage(builder, builder.CreateString("Content of page 2"), 2));
    auto book = library::CreateBook(
        builder,
        builder.CreateString("Title"),
        library::Genre_adventure,
        builder.CreateVector(pages));
    library::FinishBookBuffer(builder, book);

    flatbuffers::Verifier verifier(builder.GetBufferPointer(), builder.GetSize());
    Cout << (library::VerifyBookBuffer(verifier) ? "SUCCESS" : "FAIL") << Endl;

    auto b = library::GetBook(builder.GetBufferPointer());
    Cout << b->title()->str() << Endl;
    Cout << library::EnumNameGenre(b->genre()) << Endl;
    for (const auto p : *b->pages()) {
        Cout << "- [" << p->number() << "] " << p->content()->str() << Endl;
    }

    return 0;
}
