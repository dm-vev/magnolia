package main

/*
#include <stdlib.h>
*/
import "C"

import (
	"magnolia/tinygo"
	"runtime"
	"unsafe"
)

func eprintf(msg string) {
	_, _ = magnolia.WriteString(magnolia.Stderr, msg)
}

type unameOpts struct {
	sysname   bool
	nodename  bool
	release   bool
	version   bool
	machine   bool
	processor bool
}

func selectAll(o *unameOpts) {
	o.sysname = true
	o.nodename = true
	o.release = true
	o.version = true
	o.machine = true
}

func anySelected(o *unameOpts) bool {
	return o.sysname || o.nodename || o.release || o.version || o.machine || o.processor
}

func unameSysname() string {
	return "Magnolia"
}

func cStringToString(p *C.char) string {
	if p == nil {
		return ""
	}
	n := 0
	for {
		if *(*byte)(unsafe.Pointer(uintptr(unsafe.Pointer(p)) + uintptr(n))) == 0 {
			break
		}
		n++
	}
	if n == 0 {
		return ""
	}
	out := make([]byte, n)
	for i := 0; i < n; i++ {
		out[i] = *(*byte)(unsafe.Pointer(uintptr(unsafe.Pointer(p)) + uintptr(i)))
	}
	return string(out)
}

func cString(s string) []byte {
	b := make([]byte, len(s)+1)
	copy(b, s)
	b[len(b)-1] = 0
	return b
}

func getenvNonEmpty(key string) string {
	cKey := cString(key)
	value := C.getenv((*C.char)(unsafe.Pointer(&cKey[0])))
	if value == nil {
		return ""
	}
	return cStringToString(value)
}

func unameNodename() string {
	// Match the mainline fallback behavior when no hostname API is available.
	if v := getenvNonEmpty("HOSTNAME"); v != "" {
		return v
	}
	if v := getenvNonEmpty("HOST"); v != "" {
		return v
	}
	return "magnolia"
}

func unameRelease() string {
	return "0.1"
}

func unameVersion() string {
	return "unknown"
}

func unameArch() string {
	switch runtime.GOARCH {
	case "riscv32", "riscv64":
		return "riscv"
	case "":
		return "unknown"
	default:
		return runtime.GOARCH
	}
}

func unameMachine() string {
	return unameArch()
}

func unameProcessor() string {
	return unameArch()
}

func unamePrint(o *unameOpts) {
	fields := make([]string, 0, 8)
	if o.sysname {
		fields = append(fields, unameSysname())
	}
	if o.nodename {
		fields = append(fields, unameNodename())
	}
	if o.release {
		fields = append(fields, unameRelease())
	}
	if o.version {
		fields = append(fields, unameVersion())
	}
	if o.machine {
		fields = append(fields, unameMachine())
	}
	if o.processor {
		fields = append(fields, unameProcessor())
	}
	for i, f := range fields {
		if i > 0 {
			_, _ = magnolia.WriteString(magnolia.Stdout, " ")
		}
		_, _ = magnolia.WriteString(magnolia.Stdout, f)
	}
	_, _ = magnolia.WriteString(magnolia.Stdout, "\n")
}

//export app_main
func app_main(argc C.int, argv **C.char) C.int {
	magnolia.InitRuntime()
	args := magnolia.Args(int32(argc), unsafe.Pointer(argv))

	opts := unameOpts{}
	i := 1
	for i < len(args) {
		a := args[i]
		if a == "--" {
			i++
			break
		}
		if len(a) < 2 || a[0] != '-' || a == "-" {
			break
		}
		for j := 1; j < len(a); j++ {
			switch a[j] {
			case 'a':
				selectAll(&opts)
			case 's':
				opts.sysname = true
			case 'n':
				opts.nodename = true
			case 'r':
				opts.release = true
			case 'v':
				opts.version = true
			case 'm':
				opts.machine = true
			case 'p':
				opts.processor = true
			default:
				eprintf("uname: illegal option -- " + string(a[j]) + "\n")
				eprintf("usage: uname [-amnprsv]\n")
				return 1
			}
		}
		i++
	}

	if i < len(args) {
		eprintf("uname: extra operand: " + args[i] + "\n")
		eprintf("usage: uname [-amnprsv]\n")
		return 1
	}

	if !anySelected(&opts) {
		opts.sysname = true
	}
	unamePrint(&opts)
	return 0
}

func main() {}
