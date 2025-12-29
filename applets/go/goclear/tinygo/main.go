package main

/*
#include <stdlib.h>
*/
import "C"

import (
	"magnolia/tinygo"
	"unsafe"
)

const (
	clearSeq       = "\x1b[H\x1b[2J"
	clearScroll    = "\x1b[3J"
	clearVersion   = "Magnolia coreutils 0.1"
	clearUsageLine = "usage: clear [-T term] [-V] [-x]\n"
)

func writeString(s string) bool {
	return magnolia.WriteAll(magnolia.Stdout, []byte(s)) == nil
}

func eprintf(msg string) {
	_, _ = magnolia.WriteString(magnolia.Stderr, msg)
}

func usage() {
	eprintf(clearUsageLine)
}

func cString(s string) []byte {
	b := make([]byte, len(s)+1)
	copy(b, s)
	b[len(b)-1] = 0
	return b
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

func getenv(key string) string {
	cKey := cString(key)
	value := C.getenv((*C.char)(unsafe.Pointer(&cKey[0])))
	if value == nil {
		return ""
	}
	return cStringToString(value)
}

func termHasScrollback(term string) (bool, bool) {
	switch term {
	case "xterm", "xterm-256color", "xterm-color", "screen", "screen-256color", "tmux", "tmux-256color":
		return true, true
	case "vt100", "ansi", "linux":
		return true, false
	default:
		return false, false
	}
}

//export app_main
func app_main(argc C.int, argv **C.char) C.int {
	magnolia.InitRuntime()
	args := magnolia.Args(int32(argc), unsafe.Pointer(argv))
	termOverride := ""
	noScroll := false
	showVersion := false

	i := 1
	for i < len(args) {
		a := args[i]
		if a == "--" {
			i++
			break
		}
		if len(a) >= 2 && a[0] == '-' {
			if a == "-V" {
				showVersion = true
				i++
				continue
			}
			if a == "-x" {
				noScroll = true
				i++
				continue
			}
			if len(a) >= 2 && a[1] == 'T' {
				if len(a) > 2 {
					termOverride = a[2:]
					i++
					continue
				}
				i++
				if i >= len(args) || args[i] == "" {
					eprintf("clear: option requires an argument -- T\n")
					usage()
					return 1
				}
				termOverride = args[i]
				i++
				continue
			}
			opt := byte('?')
			if len(a) > 1 {
				opt = a[1]
			}
			eprintf("clear: illegal option -- " + string(opt) + "\n")
			usage()
			return 1
		}
		usage()
		return 1
	}

	if i < len(args) {
		usage()
		return 1
	}

	if showVersion {
		writeString("clear (" + clearVersion + ")\n")
		return 0
	}

	term := termOverride
	if term == "" {
		term = getenv("TERM")
	}
	if term == "" {
		eprintf("clear: TERM environment variable not set.\n")
		return 1
	}

	ok, scrollback := termHasScrollback(term)
	if !ok {
		eprintf("clear: unknown terminal type " + term + "\n")
		return 1
	}

	if err := magnolia.WriteAll(magnolia.Stdout, []byte(clearSeq)); err != nil {
		eprintf("clear: stdout: " + err.Error() + "\n")
		return 1
	}
	if scrollback && !noScroll {
		if err := magnolia.WriteAll(magnolia.Stdout, []byte(clearScroll)); err != nil {
			eprintf("clear: stdout: " + err.Error() + "\n")
			return 1
		}
	}
	return 0
}

func main() {}
