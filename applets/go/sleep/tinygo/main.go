package main

/*
#include <time.h>
*/
import "C"

import (
	"magnolia/tinygo"
	"math"
	"strconv"
	"unsafe"
)

func eprintf(msg string) {
	_, _ = magnolia.WriteString(magnolia.Stderr, msg)
}

func usage() {
	eprintf("usage: sleep seconds\n")
}

func illegalInterval(arg string) {
	eprintf("sleep: illegal time interval -- " + arg + "\n")
	usage()
}

// Match the platform time_t range to keep behavior aligned with mainline.
func timeTMaxSeconds() float64 {
	var t C.time_t
	bits := uint(unsafe.Sizeof(t) * 8)
	if C.time_t(-1) > 0 {
		if bits >= 64 {
			return float64(^uint64(0))
		}
		return float64((uint64(1) << bits) - 1)
	}
	if bits >= 64 {
		return float64(^uint64(0) >> 1)
	}
	return float64((uint64(1) << (bits - 1)) - 1)
}

func parseSeconds(arg string, maxSeconds float64) (float64, bool) {
	if len(arg) == 0 {
		return 0, false
	}
	v, err := strconv.ParseFloat(arg, 64)
	if err != nil || math.IsInf(v, 0) || math.IsNaN(v) || v < 0 {
		return 0, false
	}
	if v > maxSeconds {
		return 0, false
	}
	return v, true
}

func sleepSeconds(total float64) {
	if total <= 0 {
		return
	}
	secPart, frac := math.Modf(total)
	for secPart >= 1.0 {
		maxChunk := float64(^uint32(0))
		var chunk uint32
		if secPart > maxChunk {
			chunk = ^uint32(0)
		} else {
			chunk = uint32(secPart)
		}
		if chunk == 0 {
			break
		}
		magnolia.Sleep(chunk)
		secPart -= float64(chunk)
	}
	usec := uint32(frac * 1_000_000.0)
	if usec >= 1_000_000 {
		magnolia.Sleep(1)
		usec = 0
	}
	if usec > 0 {
		_ = magnolia.Usleep(usec)
	}
}

//export app_main
func app_main(argc C.int, argv **C.char) C.int {
	args := magnolia.Args(int32(argc), unsafe.Pointer(argv))
	if len(args) < 2 {
		usage()
		return 1
	}
	maxSeconds := timeTMaxSeconds()
	total, ok := parseSeconds(args[1], maxSeconds)
	if !ok {
		illegalInterval(args[1])
		return 1
	}
	if len(args) > 2 {
		illegalInterval(args[2])
		return 1
	}
	sleepSeconds(total)
	return 0
}

func main() {}
