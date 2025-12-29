package main

import "C"

import (
	"magnolia/tinygo"
	"strconv"
	"unsafe"
)

const lineBytes = 16

type formatMode uint8

const (
	modeCanonical formatMode = iota
	modeByteOctal
	modeChar
	modeShortDec
	modeShortOct
	modeShortHex
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

func appendOctMinWidth(dst []byte, v uint64, width int) []byte {
	var tmp [22]byte
	i := len(tmp)
	if v == 0 {
		i--
		tmp[i] = '0'
	}
	for v > 0 {
		i--
		tmp[i] = byte('0' + (v % 8))
		v /= 8
	}
	n := len(tmp) - i
	for n < width {
		dst = append(dst, '0')
		n++
	}
	return append(dst, tmp[i:]...)
}

func appendDecMinWidth(dst []byte, v uint64, width int) []byte {
	var tmp [22]byte
	i := len(tmp)
	if v == 0 {
		i--
		tmp[i] = '0'
	}
	for v > 0 {
		i--
		tmp[i] = byte('0' + (v % 10))
		v /= 10
	}
	n := len(tmp) - i
	for n < width {
		dst = append(dst, '0')
		n++
	}
	return append(dst, tmp[i:]...)
}

func renderChar(b byte) (byte, byte) {
	switch b {
	case 0:
		return '\\', '0'
	case '\n':
		return '\\', 'n'
	case '\r':
		return '\\', 'r'
	case '\t':
		return '\\', 't'
	case '\b':
		return '\\', 'b'
	case '\f':
		return '\\', 'f'
	case '\v':
		return '\\', 'v'
	case '\\':
		return '\\', '\\'
	default:
		if b >= 0x20 && b <= 0x7e {
			return ' ', b
		}
		return '.', ' '
	}
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

func printCanonical(offset uint64, buf []byte, length int) {
	line := make([]byte, 0, 256)
	line = appendHexMinWidth(line, offset, 8, false)
	line = append(line, ' ', ' ')
	for i := 0; i < lineBytes; i++ {
		if i < length {
			line = appendHexMinWidth(line, uint64(buf[i]), 2, false)
			line = append(line, ' ')
		} else {
			line = appendSpaces(line, 3)
		}
		if i == 7 {
			line = append(line, ' ')
		}
	}
	line = append(line, ' ', '|')
	for i := 0; i < lineBytes; i++ {
		if i < length {
			b := buf[i]
			if b >= 0x20 && b <= 0x7e {
				line = append(line, b)
			} else {
				line = append(line, '.')
			}
		} else {
			line = append(line, ' ')
		}
	}
	line = append(line, '|', '\n')
	_ = magnolia.WriteAll(magnolia.Stdout, line)
}

func printByteOctal(offset uint64, buf []byte, length int) {
	line := make([]byte, 0, 256)
	line = appendHexMinWidth(line, offset, 8, false)
	line = append(line, ' ')
	for i := 0; i < lineBytes; i++ {
		if i < length {
			line = append(line, ' ')
			line = appendOctMinWidth(line, uint64(buf[i]), 3)
		} else {
			line = appendSpaces(line, 4)
		}
	}
	line = append(line, '\n')
	_ = magnolia.WriteAll(magnolia.Stdout, line)
}

func printChar(offset uint64, buf []byte, length int) {
	line := make([]byte, 0, 256)
	line = appendHexMinWidth(line, offset, 8, false)
	line = append(line, ' ')
	for i := 0; i < lineBytes; i++ {
		if i < length {
			a, b := renderChar(buf[i])
			line = append(line, ' ', a, b)
		} else {
			line = appendSpaces(line, 3)
		}
	}
	line = append(line, '\n')
	_ = magnolia.WriteAll(magnolia.Stdout, line)
}

func printShort(offset uint64, buf []byte, length int, mode formatMode) {
	line := make([]byte, 0, 256)
	line = appendHexMinWidth(line, offset, 8, false)
	line = append(line, ' ')
	for i := 0; i < lineBytes; i += 2 {
		if i+1 < length {
			word := uint64(buf[i]) | (uint64(buf[i+1]) << 8)
			line = append(line, ' ')
			switch mode {
			case modeShortDec:
				line = appendDecMinWidth(line, word, 5)
			case modeShortOct:
				line = appendOctMinWidth(line, word, 6)
			default:
				line = appendHexMinWidth(line, word, 4, false)
			}
		} else {
			switch mode {
			case modeShortDec:
				line = appendSpaces(line, 6)
			case modeShortOct:
				line = appendSpaces(line, 7)
			default:
				line = appendSpaces(line, 5)
			}
		}
	}
	line = append(line, '\n')
	_ = magnolia.WriteAll(magnolia.Stdout, line)
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

func equalBuf(a []byte, b []byte, length int) bool {
	for i := 0; i < length; i++ {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

func hexdumpFD(fd magnolia.FD, name string, mode formatMode, verbose bool,
	offset *uint64, remaining *uint64, skipLeft *uint64,
) bool {
	prev := make([]byte, lineBytes)
	prevLen := 0
	suppressed := false
	buf := make([]byte, lineBytes)

	for {
		if *skipLeft > 0 {
			if !skipBytes(fd, skipLeft) {
				eprintf("hexdump: " + name + ": skip failed\n")
				return false
			}
		}
		want := lineBytes
		if remaining != nil && *remaining < uint64(want) {
			want = int(*remaining)
		}
		n, err := magnolia.Read(fd, buf[:want])
		if err != nil {
			eprintf("hexdump: " + name + ": " + err.Error() + "\n")
			return false
		}
		if n == 0 {
			break
		}
		if remaining != nil {
			*remaining -= uint64(n)
		}
		same := !verbose && prevLen == n && equalBuf(prev, buf, n)
		if same {
			if !suppressed {
				_, _ = magnolia.WriteString(magnolia.Stdout, "*\n")
				suppressed = true
			}
		} else {
			suppressed = false
			switch mode {
			case modeCanonical:
				printCanonical(*offset, buf, n)
			case modeByteOctal:
				printByteOctal(*offset, buf, n)
			case modeChar:
				printChar(*offset, buf, n)
			default:
				printShort(*offset, buf, n, mode)
			}
			copy(prev, buf[:n])
			prevLen = n
		}
		*offset += uint64(n)
		if remaining != nil && *remaining == 0 {
			break
		}
	}
	return true
}

func usage() {
	eprintf("usage: hexdump [-bcdoxC] [-n length] [-s offset] [-v] [file ...]\n")
}

//export app_main
func app_main(argc C.int, argv **C.char) C.int {
	magnolia.InitRuntime()
	args := magnolia.Args(int32(argc), unsafe.Pointer(argv))
	mode := modeCanonical
	verbose := false
	length := uint64(0)
	skip := uint64(0)
	useLength := false

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
			case 'b':
				mode = modeByteOctal
			case 'c':
				mode = modeChar
			case 'C':
				mode = modeCanonical
			case 'd':
				mode = modeShortDec
			case 'o':
				mode = modeShortOct
			case 'x':
				mode = modeShortHex
			case 'v':
				verbose = true
			case 'n', 's':
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
				parsed, ok := parseSize(val)
				if !ok {
					if opt == 'n' {
						eprintf("hexdump: invalid length '" + val + "'\n")
					} else {
						eprintf("hexdump: invalid skip '" + val + "'\n")
					}
					return 1
				}
				if opt == 'n' {
					length = parsed
					useLength = true
				} else {
					skip = parsed
				}
				j = len(a)
			default:
				usage()
				return 1
			}
			j++
		}
		i++
	}

	files := []string{}
	if i < len(args) {
		files = args[i:]
	}

	offset := uint64(0)
	remaining := length
	skipLeft := skip
	var remPtr *uint64
	if useLength {
		remPtr = &remaining
	}
	rc := 0

	if len(files) == 0 {
		if !hexdumpFD(magnolia.Stdin, "-", mode, verbose, &offset, remPtr, &skipLeft) {
			rc = 1
		}
	} else {
		for _, path := range files {
			fd, err := magnolia.Open(path, magnolia.O_RDONLY, 0)
			if err != nil {
				eprintf("hexdump: " + path + ": " + err.Error() + "\n")
				rc = 1
				continue
			}
			if !hexdumpFD(fd, path, mode, verbose, &offset, remPtr, &skipLeft) {
				rc = 1
			}
			_ = magnolia.Close(fd)
			if useLength && remaining == 0 {
				break
			}
		}
	}

	out := make([]byte, 0, 32)
	out = appendHexMinWidth(out, offset, 8, false)
	out = append(out, '\n')
	_ = magnolia.WriteAll(magnolia.Stdout, out)
	return C.int(rc)
}

func main() {}
