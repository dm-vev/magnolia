package main

import "C"

import "magnolia/tinygo"
import "unsafe"

func uitoa(v uint) string {
	if v == 0 {
		return "0"
	}
	var buf [20]byte
	i := len(buf)
	for v > 0 {
		i--
		buf[i] = byte('0' + (v % 10))
		v /= 10
	}
	return string(buf[i:])
}

//export app_main
func app_main(argc C.int, argv **C.char) C.int {
	args := magnolia.Args(int32(argc), unsafe.Pointer(argv))
	_, _ = magnolia.WriteString(magnolia.Stdout, "goargs: argv\n")
	for i, a := range args {
		_, _ = magnolia.WriteString(magnolia.Stdout, uitoa(uint(i)))
		_, _ = magnolia.WriteString(magnolia.Stdout, ": ")
		_, _ = magnolia.WriteString(magnolia.Stdout, a)
		_, _ = magnolia.WriteString(magnolia.Stdout, "\n")
	}
	return 0
}

func main() {}
