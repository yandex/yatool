import com.google.protobuf.InvalidProtocolBufferException;

import ru.yandex.sample.BookClass.Book;
import ru.yandex.sample.PageClass.Page;


public final class Main {
    public static void main(String[] args) {
        Page page = Page.newBuilder().setText("This is the first page").build();
        Book book = Book.newBuilder().addPages(page).build();

        byte[] bytes = book.toByteArray();

        Book book2;
        try {
            book2 = Book.parseFrom(bytes);
        } catch (InvalidProtocolBufferException e) {
            throw new RuntimeException(e);
        }

        System.out.println(book2.getPages(0).getText());
    }
}
