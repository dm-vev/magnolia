package main

import "C"

import (
	"magnolia/tinygo"
	"strconv"
	"unsafe"
)

func eprintf(msg string) {
	_, _ = magnolia.WriteString(magnolia.Stderr, msg)
}

func usage() {
	eprintf("usage: sleep seconds[s|m|h|d]...\n")
}

func parseDuration(arg string) (float64, bool) {
	if len(arg) == 0 {
		return 0, false
	}
	unit := arg[len(arg)-1]
	mult := 1.0
	num := arg
	switch unit {
	case 's':
		mult = 1.0
		num = arg[:len(arg)-1]
	case 'm':
		mult = 60.0
		num = arg[:len(arg)-1]
	case 'h':
		mult = 3600.0
		num = arg[:len(arg)-1]
	case 'd':
		mult = 86400.0
		num = arg[:len(arg)-1]
	default:
		if unit < '0' || unit > '9' {
			return 0, false
		}
	}
	if len(num) == 0 {
		return 0, false
	}
	v, err := strconv.ParseFloat(num, 64)
	if err != nil || v < 0 {
		return 0, false
	}
	return v * mult, true
}

func sleepSeconds(total float64) {
	if total <= 0 {
		return
	}
	for total >= 1.0 {
		maxChunk := float64(^uint32(0))
		var chunk uint32
		if total > maxChunk {
			chunk = ^uint32(0)
		} else {
			chunk = uint32(total)
		}
		if chunk == 0 {
			break
		}
		magnolia.Sleep(chunk)
		total -= float64(chunk)
	}
	if total > 0 {
		usec := uint32(total*1_000_000.0 + 0.5)
		if usec > 0 {
			_ = magnolia.Usleep(usec)
		}
	}
}

//export app_main
func app_main(argc C.int, argv **C.char) C.int {
	args := magnolia.Args(int32(argc), unsafe.Pointer(argv))
	if len(args) < 2 {
		usage()
		return 1
	}
	total := 0.0
	for i := 1; i < len(args); i++ {
		arg := args[i]
		if arg == "--" {
			continue
		}
		v, ok := parseDuration(arg)
		if !ok {
			usage()
			return 1
		}
		total += v
	}
	sleepSeconds(total)
	return 0
}

func main() {}
