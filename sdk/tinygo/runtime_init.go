//go:build tinygo

package magnolia

import _ "unsafe"

//go:linkname runtimeInitHeap runtime.initHeap
func runtimeInitHeap()

//go:linkname runtimeInitRand runtime.initRand
func runtimeInitRand()

//go:linkname runtimeInitAll runtime.initAll
func runtimeInitAll()

var runtimeInitialized bool

// InitRuntime prepares the TinyGo runtime for applet execution.
func InitRuntime() {
	if runtimeInitialized {
		return
	}
	runtimeInitialized = true
	runtimeInitHeap()
	runtimeInitRand()
	runtimeInitAll()
}
