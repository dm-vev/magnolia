//go:build !tinygo

package magnolia

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

func (e Errno) Error() string { return "magnolia/tinygo: unsupported (host build)" }

var errHostUnsupported = errors.New("magnolia/tinygo: unsupported (host build)")

func Write(fd FD, p []byte) (int, error)                { return 0, errHostUnsupported }
func WriteString(fd FD, s string) (int, error)          { return 0, errHostUnsupported }
func WriteAll(fd FD, p []byte) error                    { return errHostUnsupported }
func Read(fd FD, p []byte) (int, error)                 { return 0, errHostUnsupported }
func Open(path string, flags int32, mode uint32) (FD, error) { return -1, errHostUnsupported }
func Close(fd FD) error                                 { return errHostUnsupported }
func Unlink(path string) error                          { return errHostUnsupported }
func Mkdir(path string, mode uint32) error              { return errHostUnsupported }
func Chdir(path string) error                           { return errHostUnsupported }
func Getcwd(buf []byte) (string, error)                 { return "", errHostUnsupported }
func Cwd() (string, error)                              { return "", errHostUnsupported }
func Args(argc int32, argv unsafe.Pointer) []string      { return nil }
