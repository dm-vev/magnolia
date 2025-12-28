package main

import "C"

import (
	"magnolia/tinygo"
	"strconv"
	"strings"
	"unsafe"
)

func eprintf(msg string) {
	_, _ = magnolia.WriteString(magnolia.Stderr, msg)
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

func writeAll(fd magnolia.FD, buf []byte) error {
	return magnolia.WriteAll(fd, buf)
}

func skipInput(fd magnolia.FD, blocks uint64, ibs int) bool {
	if blocks == 0 {
		return true
	}
	buf := make([]byte, ibs)
	left := blocks
	for left > 0 {
		n, err := magnolia.Read(fd, buf)
		if err != nil || n == 0 {
			return false
		}
		left--
	}
	return true
}

func seekOutput(fd magnolia.FD, blocks uint64, obs int) bool {
	if blocks == 0 {
		return true
	}
	zeros := make([]byte, obs)
	left := blocks
	for left > 0 {
		if writeAll(fd, zeros) != nil {
			return false
		}
		left--
	}
	return true
}

//export app_main
func app_main(argc C.int, argv **C.char) C.int {
	args := magnolia.Args(int32(argc), unsafe.Pointer(argv))
	ifile := ""
	ofile := ""
	ibs := uint64(512)
	obs := uint64(512)
	bs := uint64(0)
	count := uint64(0)
	skip := uint64(0)
	seek := uint64(0)
	useCount := false
	noerror := false
	sync := false
	notrunc := false
	statusNone := false

	for i, a := range args {
		if i == 0 {
			continue
		}
		eq := strings.IndexByte(a, '=')
		if eq < 0 {
			eprintf("dd: invalid argument '" + a + "'\n")
			return 1
		}
		key := a[:eq]
		val := a[eq+1:]
		switch key {
		case "if":
			ifile = val
		case "of":
			ofile = val
		case "ibs":
			if v, ok := parseSize(val); ok {
				ibs = v
			} else {
				eprintf("dd: invalid ibs '" + val + "'\n")
				return 1
			}
		case "obs":
			if v, ok := parseSize(val); ok {
				obs = v
			} else {
				eprintf("dd: invalid obs '" + val + "'\n")
				return 1
			}
		case "bs":
			if v, ok := parseSize(val); ok {
				bs = v
			} else {
				eprintf("dd: invalid bs '" + val + "'\n")
				return 1
			}
		case "count":
			if v, ok := parseSize(val); ok {
				count = v
				useCount = true
			} else {
				eprintf("dd: invalid count '" + val + "'\n")
				return 1
			}
		case "skip":
			if v, ok := parseSize(val); ok {
				skip = v
			} else {
				eprintf("dd: invalid skip '" + val + "'\n")
				return 1
			}
		case "seek":
			if v, ok := parseSize(val); ok {
				seek = v
			} else {
				eprintf("dd: invalid seek '" + val + "'\n")
				return 1
			}
		case "conv":
			parts := strings.Split(val, ",")
			for _, p := range parts {
				switch p {
				case "noerror":
					noerror = true
				case "sync":
					sync = true
				case "notrunc":
					notrunc = true
				case "":
				default:
					eprintf("dd: unsupported conv '" + p + "'\n")
					return 1
				}
			}
		case "status":
			if val == "none" {
				statusNone = true
			} else {
				eprintf("dd: unsupported status '" + val + "'\n")
				return 1
			}
		default:
			eprintf("dd: invalid argument '" + a + "'\n")
			return 1
		}
	}

	if bs > 0 {
		ibs = bs
		obs = bs
	}
	if ibs == 0 || obs == 0 {
		eprintf("dd: block size cannot be zero\n")
		return 1
	}

	infd := magnolia.Stdin
	outfd := magnolia.Stdout
	if ifile != "" {
		fd, err := magnolia.Open(ifile, magnolia.O_RDONLY, 0)
		if err != nil {
			eprintf("dd: " + ifile + ": " + err.Error() + "\n")
			return 1
		}
		infd = fd
		defer magnolia.Close(fd)
	}
	if ofile != "" {
		flags := magnolia.O_WRONLY | magnolia.O_CREAT
		if !notrunc {
			flags |= magnolia.O_TRUNC
		}
		fd, err := magnolia.Open(ofile, flags, 0o666)
		if err != nil {
			eprintf("dd: " + ofile + ": " + err.Error() + "\n")
			return 1
		}
		outfd = fd
		defer magnolia.Close(fd)
	}

	if !skipInput(infd, skip, int(ibs)) {
		eprintf("dd: skip failed\n")
		return 1
	}
	if !seekOutput(outfd, seek, int(obs)) {
		eprintf("dd: seek failed\n")
		return 1
	}

	ibuf := make([]byte, int(ibs))
	obuf := make([]byte, int(obs))
	obufLen := 0
	inFull := uint64(0)
	inPart := uint64(0)
	outFull := uint64(0)
	outPart := uint64(0)
	blocks := uint64(0)

	for !useCount || blocks < count {
		n, err := magnolia.Read(infd, ibuf)
		if err != nil {
			if noerror {
				eprintf("dd: read error\n")
				continue
			}
			eprintf("dd: read error\n")
			break
		}
		if n == 0 {
			break
		}
		if n == int(ibs) {
			inFull++
		} else {
			inPart++
		}
		chunkLen := n
		if sync && chunkLen < int(ibs) {
			for i := chunkLen; i < int(ibs); i++ {
				ibuf[i] = 0
			}
			chunkLen = int(ibs)
		}
		if obs == ibs {
			if writeAll(outfd, ibuf[:chunkLen]) != nil {
				eprintf("dd: write error\n")
				break
			}
			if chunkLen == int(obs) {
				outFull++
			} else {
				outPart++
			}
		} else {
			off := 0
			for off < chunkLen {
				space := int(obs) - obufLen
				take := chunkLen - off
				if take > space {
					take = space
				}
				copy(obuf[obufLen:obufLen+take], ibuf[off:off+take])
				obufLen += take
				off += take
				if obufLen == int(obs) {
					if writeAll(outfd, obuf) != nil {
						eprintf("dd: write error\n")
						return 1
					}
					outFull++
					obufLen = 0
				}
			}
		}
		blocks++
	}

	if obufLen > 0 {
		if writeAll(outfd, obuf[:obufLen]) != nil {
			eprintf("dd: write error\n")
		} else {
			outPart++
		}
	}

	if !statusNone {
		eprintf(strconv.FormatUint(inFull, 10) + "+" + strconv.FormatUint(inPart, 10) + " records in\n")
		eprintf(strconv.FormatUint(outFull, 10) + "+" + strconv.FormatUint(outPart, 10) + " records out\n")
	}

	return 0
}

func main() {}
