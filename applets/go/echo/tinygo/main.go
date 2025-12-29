package main

import "C"

import (
	"magnolia/tinygo"
	"unsafe"
)

func writeByte(b byte) bool {
	var buf [1]byte
	buf[0] = b
	return magnolia.WriteAll(magnolia.Stdout, buf[:]) == nil
}

func writeEscaped(s string) (stop bool, ok bool) {
	for i := 0; i < len(s); i++ {
		b := s[i]
		if b != '\\' {
			if !writeByte(b) {
				return false, false
			}
			continue
		}
		if i+1 >= len(s) {
			break
		}
		i++
		switch s[i] {
		case '\\':
			b = '\\'
		case 'a':
			b = '\a'
		case 'b':
			b = '\b'
		case 'f':
			b = '\f'
		case 'n':
			b = '\n'
		case 'r':
			b = '\r'
		case 't':
			b = '\t'
		case 'v':
			b = '\v'
		case 'c':
			return true, true
		case '0':
			val := byte(0)
			digits := 0
			for digits < 3 && i+1 < len(s) {
				d := s[i+1]
				if d < '0' || d > '7' {
					break
				}
				i++
				val = (val << 3) | byte(d-'0')
				digits++
			}
			b = val
		default:
			if !writeByte('\\') {
				return false, false
			}
			b = s[i]
		}
		if !writeByte(b) {
			return false, false
		}
	}
	return false, true
}

//export app_main
func app_main(argc C.int, argv **C.char) C.int {
	magnolia.InitRuntime()
	args := magnolia.Args(int32(argc), unsafe.Pointer(argv))
	newline := true

	i := 1
	// BSD echo only recognizes a single leading -n.
	if i < len(args) && args[i] == "-n" {
		newline = false
		i++
	}

	first := true
	for ; i < len(args); i++ {
		if !first {
			if !writeByte(' ') {
				return 1
			}
		}
		first = false
		stop, ok := writeEscaped(args[i])
		if !ok {
			return 1
		}
		if stop {
			newline = false
			return 0
		}
	}

	if newline {
		if !writeByte('\n') {
			return 1
		}
	}
	return 0
}

func main() {}
