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

func appendSpaces(dst []byte, count int) []byte {
	for i := 0; i < count; i++ {
		dst = append(dst, ' ')
	}
	return dst
}

func appendHexMinWidth(dst []byte, v uint64, width int, upper bool) []byte {
	digits := "0123456789abcdef"
	if upper {
		digits = "0123456789ABCDEF"
	}
	var tmp [16]byte
	i := len(tmp)
	if v == 0 {
		i--
		tmp[i] = '0'
	}
	for v > 0 {
		i--
		tmp[i] = digits[int(v&0xf)]
		v >>= 4
	}
	n := len(tmp) - i
	for n < width {
		dst = append(dst, '0')
		n++
	}
	return append(dst, tmp[i:]...)
}

func parseToken(s string) (uint64, string, bool) {
	if len(s) == 0 {
		return 0, s, false
	}
	radix := 10
	start := 0
	if len(s) >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') {
		radix = 16
		start = 2
	}
	idx := start
	for idx < len(s) {
		c := s[idx]
		ok := false
		if radix == 16 {
			ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')
		} else {
			ok = (c >= '0' && c <= '9')
		}
		if !ok {
			break
		}
		idx++
	}
	if idx == start {
		return 0, s, false
	}
	v, err := strconv.ParseUint(s[start:idx], radix, 64)
	if err != nil {
		return 0, s, false
	}
	rest := s[idx:]
	if len(rest) > 0 {
		mult := uint64(1)
		switch rest[0] {
		case 'b':
			mult = 512
		case 'k', 'K':
			mult = 1024
		case 'm', 'M':
			mult = 1024 * 1024
		case 'g', 'G':
			mult = 1024 * 1024 * 1024
		}
		if mult != 1 {
			v *= mult
			rest = rest[1:]
		}
	}
	return v, rest, true
}

func parseSize(s string) (uint64, bool) {
	rest := s
	total := uint64(1)
	for {
		v, next, ok := parseToken(rest)
		if !ok {
			return 0, false
		}
		total *= v
		if len(next) == 0 {
			return total, true
		}
		if next[0] == 'x' || next[0] == '*' {
			rest = next[1:]
			continue
		}
		return 0, false
	}
}

func skipBytes(fd magnolia.FD, skipLeft *uint64) bool {
	if *skipLeft == 0 {
		return true
	}
	buf := make([]byte, 256)
	for *skipLeft > 0 {
		chunk := len(buf)
		if *skipLeft < uint64(chunk) {
			chunk = int(*skipLeft)
		}
		n, err := magnolia.Read(fd, buf[:chunk])
		if err != nil || n <= 0 {
			return false
		}
		*skipLeft -= uint64(n)
	}
	return true
}

func xxdForward(fd magnolia.FD, skip uint64, length uint64, useLength bool, columns int, group int, plain bool, upper bool) bool {
	skipLeft := skip
	if !skipBytes(fd, &skipLeft) {
		eprintf("xxd: skip failed\n")
		return false
	}
	offset := uint64(0)
	buf := make([]byte, columns)
	for {
		want := columns
		if useLength && length < uint64(want) {
			want = int(length)
		}
		n, err := magnolia.Read(fd, buf[:want])
		if err != nil {
			eprintf("xxd: read error: " + err.Error() + "\n")
			return false
		}
		if n == 0 {
			break
		}
		if plain {
			line := make([]byte, 0, n*2+2)
			for i := 0; i < n; i++ {
				line = appendHexMinWidth(line, uint64(buf[i]), 2, upper)
				if i+1 == n || ((i+1)%columns) == 0 {
					line = append(line, '\n')
				}
			}
			if err := magnolia.WriteAll(magnolia.Stdout, line); err != nil {
				eprintf("xxd: write error: " + err.Error() + "\n")
				return false
			}
		} else {
			line := make([]byte, 0, 512)
			line = appendHexMinWidth(line, offset, 8, false)
			line = append(line, ':', ' ')
			for i := 0; i < columns; i++ {
				if i < n {
					line = appendHexMinWidth(line, uint64(buf[i]), 2, upper)
				} else {
					line = appendSpaces(line, 2)
				}
				if group > 0 && ((i+1)%group) == 0 {
					line = append(line, ' ')
				}
			}
			line = append(line, ' ')
			for i := 0; i < n; i++ {
				b := buf[i]
				if b >= 0x20 && b <= 0x7e {
					line = append(line, b)
				} else {
					line = append(line, '.')
				}
			}
			line = append(line, '\n')
			if err := magnolia.WriteAll(magnolia.Stdout, line); err != nil {
				eprintf("xxd: write error: " + err.Error() + "\n")
				return false
			}
		}
		offset += uint64(n)
		if useLength {
			length -= uint64(n)
			if length == 0 {
				break
			}
		}
	}
	return true
}

func hexValue(c byte) int {
	if c >= '0' && c <= '9' {
		return int(c - '0')
	}
	if c >= 'a' && c <= 'f' {
		return int(c-'a') + 10
	}
	if c >= 'A' && c <= 'F' {
		return int(c-'A') + 10
	}
	return -1
}

func reverseStream(fd magnolia.FD) bool {
	var line [256]byte
	lineLen := 0
	half := -1
	out := make([]byte, 0, 256)

	buf := make([]byte, 128)
	for {
		n, err := magnolia.Read(fd, buf)
		if err != nil {
			eprintf("xxd: read error: " + err.Error() + "\n")
			return false
		}
		if n == 0 {
			break
		}
		for i := 0; i < n; i++ {
			c := buf[i]
			if c == '\n' || c == '\r' {
				if lineLen > 0 {
					start := 0
					for idx := 0; idx < lineLen && idx <= 8; idx++ {
						if line[idx] == ':' {
							start = idx + 1
							break
						}
					}
					for j := start; j < lineLen; j++ {
						v := hexValue(line[j])
						if v < 0 {
							continue
						}
						if half < 0 {
							half = v
						} else {
							out = append(out, byte((half<<4)|v))
							if len(out) == cap(out) {
								if err := magnolia.WriteAll(magnolia.Stdout, out); err != nil {
									eprintf("xxd: write error: " + err.Error() + "\n")
									return false
								}
								out = out[:0]
							}
							half = -1
						}
					}
				}
				lineLen = 0
				continue
			}
			if lineLen+1 < len(line) {
				line[lineLen] = c
				lineLen++
			}
		}
	}

	if lineLen > 0 {
		start := 0
		for idx := 0; idx < lineLen && idx <= 8; idx++ {
			if line[idx] == ':' {
				start = idx + 1
				break
			}
		}
		for j := start; j < lineLen; j++ {
			v := hexValue(line[j])
			if v < 0 {
				continue
			}
			if half < 0 {
				half = v
			} else {
				out = append(out, byte((half<<4)|v))
				if len(out) == cap(out) {
					if err := magnolia.WriteAll(magnolia.Stdout, out); err != nil {
						eprintf("xxd: write error: " + err.Error() + "\n")
						return false
					}
					out = out[:0]
				}
				half = -1
			}
		}
	}

	if len(out) > 0 {
		if err := magnolia.WriteAll(magnolia.Stdout, out); err != nil {
			eprintf("xxd: write error: " + err.Error() + "\n")
			return false
		}
	}
	return true
}

func usage() {
	eprintf("usage: xxd [-g n] [-c n] [-l len] [-s offset] [-p] [-r] [-u] [file]\n")
}

//export app_main
func app_main(argc C.int, argv **C.char) C.int {
	magnolia.InitRuntime()
	args := magnolia.Args(int32(argc), unsafe.Pointer(argv))
	columns := 16
	group := 2
	length := uint64(0)
	skip := uint64(0)
	useLength := false
	plain := false
	reverse := false
	upper := false

	i := 1
	for i < len(args) {
		a := args[i]
		if a == "--" {
			i++
			break
		}
		if len(a) == 0 || a[0] != '-' || a == "-" {
			break
		}
		j := 1
		for j < len(a) {
			switch a[j] {
			case 'g', 'c', 'l', 's':
				opt := a[j]
				var val string
				if j+1 < len(a) {
					val = a[j+1:]
					j = len(a)
				} else {
					if i+1 >= len(args) {
						usage()
						return 1
					}
					i++
					val = args[i]
				}
				switch opt {
				case 'g':
					v, err := strconv.Atoi(val)
					if err != nil {
						eprintf("xxd: invalid group '" + val + "'\n")
						return 1
					}
					group = v
					if group < 0 {
						group = 0
					}
				case 'c':
					v, err := strconv.Atoi(val)
					if err != nil || v <= 0 || v > 256 {
						columns = 16
					} else {
						columns = v
					}
				case 'l':
					v, ok := parseSize(val)
					if !ok {
						eprintf("xxd: invalid length '" + val + "'\n")
						return 1
					}
					length = v
					useLength = true
				case 's':
					v, ok := parseSize(val)
					if !ok {
						eprintf("xxd: invalid offset '" + val + "'\n")
						return 1
					}
					skip = v
				}
				j = len(a)
			case 'p':
				plain = true
			case 'r':
				reverse = true
			case 'u':
				upper = true
			case 'h':
				usage()
				return 1
			default:
				usage()
				return 1
			}
			j++
		}
		i++
	}

	if plain && columns == 16 {
		columns = 30
	}

	path := ""
	if i < len(args) {
		path = args[i]
	}

	fd := magnolia.Stdin
	if path != "" {
		opened, err := magnolia.Open(path, magnolia.O_RDONLY, 0)
		if err != nil {
			eprintf("xxd: " + path + ": " + err.Error() + "\n")
			return 1
		}
		fd = opened
		defer magnolia.Close(opened)
	}

	ok := true
	if reverse {
		ok = reverseStream(fd)
	} else {
		ok = xxdForward(fd, skip, length, useLength, columns, group, plain, upper)
	}
	if !ok {
		return 1
	}
	return 0
}

func main() {}
