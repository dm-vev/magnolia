package main

import "C"

import "magnolia/tinygo"

//export gohello_go_main
func gohello_go_main() C.int {
	_, _ = magnolia.WriteString(magnolia.Stdout, "Hello world!\n")
	return 0
}

func main() {}
