package main

import "C"

import "magnolia/tinygo"

//export app_main
func app_main(argc C.int, argv **C.char) C.int {
	magnolia.InitRuntime()
	_ = argc
	_ = argv
	_, _ = magnolia.WriteString(magnolia.Stdout, "Hello world!\n")
	return 0
}

func main() {}
