package main

import "C"

import (
	"magnolia/tinygo"
	"unsafe"
)

func eprintf(msg string) {
	_, _ = magnolia.WriteString(magnolia.Stderr, msg)
}

func usage() {
	eprintf("usage: pwd [-L|-P]\n")
}

//export app_main
func app_main(argc C.int, argv **C.char) C.int {
	args := magnolia.Args(int32(argc), unsafe.Pointer(argv))
	opt := byte(0)
	i := 1
	for i < len(args) {
		a := args[i]
		if a == "--" {
			i++
			break
		}
		if len(a) < 2 || a[0] != '-' {
			break
		}
		for j := 1; j < len(a); j++ {
			switch a[j] {
			case 'L', 'P':
				opt = a[j]
			default:
				usage()
				return 1
			}
		}
		i++
	}
	if opt == 0 {
		opt = 'L'
	}
	if i < len(args) {
		usage()
		return 1
	}

	_ = opt
	cwd, err := magnolia.Cwd()
	if err != nil {
		eprintf("pwd: " + err.Error() + "\n")
		return 1
	}
	_, _ = magnolia.WriteString(magnolia.Stdout, cwd)
	_, _ = magnolia.WriteString(magnolia.Stdout, "\n")
	return 0
}

func main() {}
