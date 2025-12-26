//go:build tinygo

package magnolia

/*
typedef unsigned int size_t;
typedef int ssize_t;
typedef long off_t;

extern ssize_t write(int fd, const void *buf, size_t count);
extern ssize_t read(int fd, void *buf, size_t count);
extern int open(const char *path, int flags, int mode);
extern int close(int fd);
extern int unlink(const char *path);
extern int mkdir(const char *path, int mode);
extern int chdir(const char *path);
extern char *getcwd(char *buf, size_t size);

extern int *__errno(void);
extern char *strerror(int errnum);
*/
import "C"

import (
	"errors"
	"unsafe"
)

type FD int32

const (
	Stdin  FD = 0
	Stdout FD = 1
	Stderr FD = 2
)

// POSIX-ish open(2) flags (newlib-style; used by Magnolia's exported `open`).
const (
	O_RDONLY int32 = 0
	O_WRONLY int32 = 1
	O_RDWR   int32 = 2

	O_APPEND int32 = 0x0008
	O_CREAT  int32 = 0x0200
	O_TRUNC  int32 = 0x0400
	O_EXCL   int32 = 0x0800
)

type Errno int32

var ErrShortWrite = errors.New("short write")

func (e Errno) Error() string {
	if e == 0 {
		return "errno=0"
	}
	msg := C.strerror(C.int(e))
	if msg == nil {
		return "errno=unknown"
	}
	return cStringToString((*byte)(unsafe.Pointer(msg)))
}

func errno() Errno {
	p := C.__errno()
	if p == nil {
		return 0
	}
	return Errno(*p)
}

func Write(fd FD, p []byte) (int, error) {
	if len(p) == 0 {
		return 0, nil
	}
	n := C.write(C.int(fd), unsafe.Pointer(&p[0]), C.size_t(len(p)))
	if n < 0 {
		return int(n), errno()
	}
	return int(n), nil
}

func WriteString(fd FD, s string) (int, error) {
	return Write(fd, []byte(s))
}

func WriteAll(fd FD, p []byte) error {
	for len(p) > 0 {
		n, err := Write(fd, p)
		if err != nil {
			return err
		}
		if n <= 0 {
			return ErrShortWrite
		}
		p = p[n:]
	}
	return nil
}

func Read(fd FD, p []byte) (int, error) {
	if len(p) == 0 {
		return 0, nil
	}
	n := C.read(C.int(fd), unsafe.Pointer(&p[0]), C.size_t(len(p)))
	if n < 0 {
		return int(n), errno()
	}
	return int(n), nil
}

func Open(path string, flags int32, mode uint32) (FD, error) {
	cpath := cString(path)
	rc := C.open((*C.char)(unsafe.Pointer(&cpath[0])), C.int(flags), C.int(mode))
	if rc < 0 {
		return -1, errno()
	}
	return FD(rc), nil
}

func Close(fd FD) error {
	rc := C.close(C.int(fd))
	if rc < 0 {
		return errno()
	}
	return nil
}

func Unlink(path string) error {
	cpath := cString(path)
	rc := C.unlink((*C.char)(unsafe.Pointer(&cpath[0])))
	if rc < 0 {
		return errno()
	}
	return nil
}

func Mkdir(path string, mode uint32) error {
	cpath := cString(path)
	rc := C.mkdir((*C.char)(unsafe.Pointer(&cpath[0])), C.int(mode))
	if rc < 0 {
		return errno()
	}
	return nil
}

func Chdir(path string) error {
	cpath := cString(path)
	rc := C.chdir((*C.char)(unsafe.Pointer(&cpath[0])))
	if rc < 0 {
		return errno()
	}
	return nil
}

func Getcwd(buf []byte) (string, error) {
	if len(buf) == 0 {
		return "", Errno(0)
	}
	p := C.getcwd((*C.char)(unsafe.Pointer(&buf[0])), C.size_t(len(buf)))
	if p == nil {
		return "", errno()
	}
	return cStringToString((*byte)(unsafe.Pointer(p))), nil
}

func Cwd() (string, error) {
	var scratch [256]byte
	return Getcwd(scratch[:])
}

func Args(argc int32, argv unsafe.Pointer) []string {
	n := int(argc)
	if n <= 0 || argv == nil {
		return nil
	}

	args := make([]string, 0, n)
	base := uintptr(argv)
	ptrSize := unsafe.Sizeof(uintptr(0))
	for i := 0; i < n; i++ {
		p := *(*uintptr)(unsafe.Pointer(base + uintptr(i)*ptrSize))
		if p == 0 {
			args = append(args, "")
			continue
		}
		args = append(args, cStringToString((*byte)(unsafe.Pointer(p))))
	}
	return args
}

func cString(s string) []byte {
	b := make([]byte, len(s)+1)
	copy(b, s)
	b[len(b)-1] = 0
	return b
}

func cStringToString(p *byte) string {
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
