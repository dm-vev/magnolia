package main

import "unsafe"

/*
typedef unsigned int size_t;
typedef int ssize_t;
extern ssize_t write(int fd, const void *buf, size_t count);
*/
import "C"

var msg = [...]byte{'H', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd', '!', '\n'}

//export gohello_go_main
func gohello_go_main() C.int {
	C.write(1, unsafe.Pointer(&msg[0]), C.size_t(len(msg)))
	return 0
}

func main() {}
