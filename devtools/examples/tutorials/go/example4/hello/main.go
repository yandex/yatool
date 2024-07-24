package main

/*
#include "say_hello.h"
#include <stdlib.h>
*/
import "C"

import (
	"fmt"
	"unsafe"
)

func main() {
	name := C.CString("World")
	defer C.free(unsafe.Pointer(name))
	greeting := C.SayHello(name)
	defer C.free(unsafe.Pointer(greeting))
	fmt.Println(C.GoString(greeting))
}
