import devtools.examples.tutorials.protobuf.example3.book.book_pb2 as book_pb2


def main():
    book = book_pb2.Book()
    page = book.pages.add()
    page.text = "This is the first page"

    data = book.SerializeToString()
    new_book = book_pb2.Book()
    new_book.ParseFromString(data)

    print(new_book.pages[0].text)
