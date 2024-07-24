import library.Genre as Genre
import library.Book as Book
import library.page.Page as Page
import flatbuffers


def main():
    builder = flatbuffers.Builder(0)

    content1 = builder.CreateString("Content of page 1")
    Page.Start(builder)
    Page.AddContent(builder, content1)
    Page.AddNumber(builder, 1)
    page1 = Page.End(builder)

    content2 = builder.CreateString("Content of page 2")
    Page.Start(builder)
    Page.AddContent(builder, content2)
    Page.AddNumber(builder, 2)
    page2 = Page.End(builder)

    Book.StartPagesVector(builder, 2)
    builder.PrependUOffsetTRelative(page2)
    builder.PrependUOffsetTRelative(page1)
    pages = builder.EndVector()

    title = builder.CreateString("Title")
    Book.Start(builder)
    Book.AddTitle(builder, title)
    Book.AddGenre(builder, Genre.Genre().adventure)
    Book.AddPages(builder, pages)
    book = Book.End(builder)

    builder.Finish(book)

    buf = builder.Output()

    book = Book.Book().GetRootAsBook(buf, 0)
    print(book.Title().decode('utf-8'))
    print(book.Genre())
    for i in range(book.PagesLength()):
        print('- [{}] {}'.format(book.Pages(i).Number(), book.Pages(i).Content().decode('utf-8')))


if __name__ == '__main__':
    main()
