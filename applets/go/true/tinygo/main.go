package main

import "C"

//export app_main
func app_main(argc C.int, argv **C.char) C.int {
	_ = argc
	_ = argv
	return 0
}

func main() {}
