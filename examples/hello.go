// Ejemplo mínimo para outline / LSP gopls en tgdb.
// Compilar: go build -gcflags="all=-N -l" -o hello hello.go

package main

import "fmt"

func greet(name string) {
	fmt.Printf("hello, %s\n", name)
}

func main() {
	greet("tide")
}
