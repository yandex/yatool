package greeting

import "fmt"

func SayHello(name string) string {
	return fmt.Sprintf("Hello, %s!", name)
}
