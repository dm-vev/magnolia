package main

/*
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
*/
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

func pwdMatchesCurrent(pwd string) bool {
	// Only trust $PWD if it really refers to the current directory.
	if len(pwd) == 0 || pwd[0] != '/' {
		return false
	}
	cPwd := cString(pwd)
	var pwdStat C.struct_stat
	var dotStat C.struct_stat
	if C.stat((*C.char)(unsafe.Pointer(&cPwd[0])), &pwdStat) != 0 {
		return false
	}
	dot := []byte{'.', 0}
	if C.stat((*C.char)(unsafe.Pointer(&dot[0])), &dotStat) != 0 {
		return false
	}
	return pwdStat.st_dev == dotStat.st_dev && pwdStat.st_ino == dotStat.st_ino
}

func getcwdAlloc() (string, error) {
	// Grow the buffer to handle long paths without PATH_MAX.
	maxInt := int(^uint(0) >> 1)
	size := 256
	for {
		buf := make([]byte, size)
		cwd, err := magnolia.Getcwd(buf)
		if err == nil {
			return cwd, nil
		}
		errno, ok := err.(magnolia.Errno)
		if !ok || errno != magnolia.Errno(C.ERANGE) {
			return "", err
		}
		if size > maxInt/2 {
			return "", magnolia.Errno(C.ERANGE)
		}
		size *= 2
	}
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
		eprintf("pwd: too many arguments\n")
		return 1
	}

	if opt != 'P' {
		pwd := getenv("PWD")
		if pwdMatchesCurrent(pwd) {
			_, _ = magnolia.WriteString(magnolia.Stdout, pwd)
			_, _ = magnolia.WriteString(magnolia.Stdout, "\n")
			return 0
		}
	}
	cwd, err := getcwdAlloc()
	if err != nil {
		eprintf("pwd: " + err.Error() + "\n")
		return 1
	}
	_, _ = magnolia.WriteString(magnolia.Stdout, cwd)
	_, _ = magnolia.WriteString(magnolia.Stdout, "\n")
	return 0
}

func main() {}
