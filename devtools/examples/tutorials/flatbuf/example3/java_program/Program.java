import com.google.flatbuffers.FlatBufferBuilder;
import library.Book;
import library.Genre;
import library.page.Page;

public class Program {
    public static void main(String[] args) {
        FlatBufferBuilder fbb = new FlatBufferBuilder();

        final int[] pages = new int[] {
            Page.createPage(fbb, fbb.createString("Content of page 1"), 1),
            Page.createPage(fbb, fbb.createString("Content of page 2"), 2),
        };

        Book.finishBookBuffer(
            fbb,
            Book.createBook(
                fbb,
                fbb.createString("Title"),
                Genre.adventure,
                Book.createPagesVector(fbb, pages)
            )
        );

        Book book2 = Book.getRootAsBook(fbb.dataBuffer());

        System.out.println(book2.title());
        System.out.println(Genre.name(book2.genre()));
        for (int i = 0; i < book2.pagesLength(); i++) {
            System.out.printf("- [%d] %s\n", book2.pages(i).number(), book2.pages(i).content());
        }
    }
}

