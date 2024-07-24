package main

import (
	"a.yandex-team.ru/devtools/examples/tutorials/flatbuf/example3/library"
	"a.yandex-team.ru/devtools/examples/tutorials/flatbuf/example3/page"

	flatbuffers "github.com/google/flatbuffers/go"

	"fmt"
)

func main() {
	builder := flatbuffers.NewBuilder(0)

	content1 := builder.CreateString("Content of page 1")
	page.PageStart(builder)
	page.PageAddContent(builder, content1)
	page.PageAddNumber(builder, 1)
	page1 := page.PageEnd(builder)

	content2 := builder.CreateString("Content of page 2")
	page.PageStart(builder)
	page.PageAddContent(builder, content2)
	page.PageAddNumber(builder, 2)
	page2 := page.PageEnd(builder)

	library.BookStartPagesVector(builder, 2)
	builder.PrependUOffsetT(page2)
	builder.PrependUOffsetT(page1)
	pages := builder.EndVector(2)

	title := builder.CreateString("Title")
	library.BookStart(builder)
	library.BookAddTitle(builder, title)
	library.BookAddGenre(builder, library.Genreadventure)
	library.BookAddPages(builder, pages)
	book := library.BookEnd(builder)

	builder.Finish(book)

	book2 := library.GetRootAsBook(builder.Bytes, builder.Head())

	fmt.Println(string(book2.Title()))
	fmt.Println(book2.Genre())
	length := book2.PagesLength()
	for i := 0; i < length; i++ {
		var p page.Page
		if book2.Pages(&p, i) {
			fmt.Printf("- [%v] %s\n", p.Number(), p.Content())
		}
	}
}
