package main

import (
	"fmt"
	"os"

	book "a.yandex-team.ru/devtools/examples/tutorials/protobuf/example3/book"
	page "a.yandex-team.ru/devtools/examples/tutorials/protobuf/example3/page"
	"github.com/golang/protobuf/proto"
)

func main() {
	book1 := book.Book{
		Pages: []*page.Page{
			{Text: proto.String("This is the first page")},
		},
	}

	data, err := proto.Marshal(&book1)
	if err != nil {
		fmt.Printf("Marshalling failed: [%v]\n", err)
		os.Exit(1)
	}

	book2 := &book.Book{}
	if err = proto.Unmarshal(data, book2); err != nil {
		fmt.Printf("Unmarshalling failed: [%v]\n", err)
		os.Exit(1)
	}

	fmt.Println(book2)
}
