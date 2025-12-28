package main

import "C"

import "magnolia/tinygo"
import "unsafe"

func fail(msg string) C.int {
	_, _ = magnolia.WriteString(magnolia.Stderr, "gotest: ")
	_, _ = magnolia.WriteString(magnolia.Stderr, msg)
	_, _ = magnolia.WriteString(magnolia.Stderr, "\n")
	return 1
}

//export app_main
func app_main(argc C.int, argv **C.char) C.int {
	args := magnolia.Args(int32(argc), unsafe.Pointer(argv))
	_, _ = magnolia.WriteString(magnolia.Stdout, "gotest: start\n")

	if len(args) == 0 {
		return fail("empty argv")
	}

	cwd, err := magnolia.Cwd()
	if err == nil {
		_, _ = magnolia.WriteString(magnolia.Stdout, "cwd: ")
		_, _ = magnolia.WriteString(magnolia.Stdout, cwd)
		_, _ = magnolia.WriteString(magnolia.Stdout, "\n")
	}

	path := "/flash/tinygo_test.txt"
	_ = magnolia.Unlink(path)

	fd, err := magnolia.Open(path, magnolia.O_WRONLY|magnolia.O_CREAT|magnolia.O_TRUNC, 0644)
	if err != nil {
		return fail("open(O_WRONLY|O_CREAT|O_TRUNC): " + err.Error())
	}
	if _, err := magnolia.WriteString(fd, "hello from gotest\n"); err != nil {
		_ = magnolia.Close(fd)
		return fail("write: " + err.Error())
	}
	if err := magnolia.Close(fd); err != nil {
		return fail("close(write fd): " + err.Error())
	}

	fd, err = magnolia.Open(path, magnolia.O_RDONLY, 0)
	if err != nil {
		return fail("open(O_RDONLY): " + err.Error())
	}
	buf := make([]byte, 64)
	n, err := magnolia.Read(fd, buf)
	_ = magnolia.Close(fd)
	if err != nil {
		return fail("read: " + err.Error())
	}
	got := string(buf[:n])
	if got != "hello from gotest\n" {
		return fail("content mismatch")
	}

	_, _ = magnolia.WriteString(magnolia.Stdout, "gotest: OK\n")
	return 0
}

func main() {}
