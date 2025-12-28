package main

import "C"

import (
	"magnolia/tinygo"
	"unsafe"
)

func writeString(s string) bool {
	return magnolia.WriteAll(magnolia.Stdout, []byte(s)) == nil
}

//export app_main
func app_main(argc C.int, argv **C.char) C.int {
	args := magnolia.Args(int32(argc), unsafe.Pointer(argv))
	if len(args) == 2 {
		s := args[1]
		if s == "--help" {
			writeString("usage: clear [--help] [--version]\n")
			return 0
		}
		if s == "--version" {
			writeString("clear (Magnolia coreutils 0.1)\n")
			return 0
		}
	}
	if !writeString("\x1b[2J\x1b[H") {
		return 1
	}
	return 0
}

func main() {}
