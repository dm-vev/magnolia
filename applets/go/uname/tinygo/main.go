package main

import "C"

import (
	"magnolia/tinygo"
	"runtime"
	"unsafe"
)

const coreutilsVersion = "Magnolia coreutils 0.1"

func eprintf(msg string) {
	_, _ = magnolia.WriteString(magnolia.Stderr, msg)
}

func printHelp() {
	_, _ = magnolia.WriteString(magnolia.Stdout, "usage: uname [OPTION]...\n")
	_, _ = magnolia.WriteString(magnolia.Stdout, "  -a  print all information\n")
	_, _ = magnolia.WriteString(magnolia.Stdout, "  -s  print the kernel name\n")
	_, _ = magnolia.WriteString(magnolia.Stdout, "  -n  print the network node hostname\n")
	_, _ = magnolia.WriteString(magnolia.Stdout, "  -r  print the kernel release\n")
	_, _ = magnolia.WriteString(magnolia.Stdout, "  -v  print the kernel version\n")
	_, _ = magnolia.WriteString(magnolia.Stdout, "  -m  print the machine hardware name\n")
	_, _ = magnolia.WriteString(magnolia.Stdout, "  -p  print the processor type\n")
	_, _ = magnolia.WriteString(magnolia.Stdout, "  -i  print the hardware platform\n")
	_, _ = magnolia.WriteString(magnolia.Stdout, "  -o  print the operating system\n")
	_, _ = magnolia.WriteString(magnolia.Stdout, "      --help     display this help and exit\n")
	_, _ = magnolia.WriteString(magnolia.Stdout, "      --version  output version information and exit\n")
}

func printVersion() {
	_, _ = magnolia.WriteString(magnolia.Stdout, "uname ("+coreutilsVersion+")\n")
}

type unameOpts struct {
	sysname         bool
	nodename        bool
	release         bool
	version         bool
	machine         bool
	processor       bool
	hwPlatform      bool
	operatingSystem bool
}

func selectAll(o *unameOpts) {
	o.sysname = true
	o.nodename = true
	o.release = true
	o.version = true
	o.machine = true
	o.processor = true
	o.hwPlatform = true
	o.operatingSystem = true
}

func anySelected(o *unameOpts) bool {
	return o.sysname || o.nodename || o.release || o.version || o.machine || o.processor || o.hwPlatform || o.operatingSystem
}

func unameSysname() string {
	return "Magnolia"
}

func unameNodename() string {
	return "magnolia"
}

func unameRelease() string {
	return "0.1"
}

func unameVersion() string {
	return "unknown"
}

func unameMachine() string {
	return runtime.GOARCH
}

func unameProcessor() string {
	return runtime.GOARCH
}

func unameHwPlatform() string {
	return unameMachine()
}

func unameOperatingSystem() string {
	return "Magnolia"
}

func unamePrint(o *unameOpts) {
	fields := make([]string, 0, 8)
	if o.sysname {
		fields = append(fields, unameSysname())
	}
	if o.nodename {
		fields = append(fields, unameNodename())
	}
	if o.release {
		fields = append(fields, unameRelease())
	}
	if o.version {
		fields = append(fields, unameVersion())
	}
	if o.machine {
		fields = append(fields, unameMachine())
	}
	if o.processor {
		fields = append(fields, unameProcessor())
	}
	if o.hwPlatform {
		fields = append(fields, unameHwPlatform())
	}
	if o.operatingSystem {
		fields = append(fields, unameOperatingSystem())
	}
	for i, f := range fields {
		if i > 0 {
			_, _ = magnolia.WriteString(magnolia.Stdout, " ")
		}
		_, _ = magnolia.WriteString(magnolia.Stdout, f)
	}
	_, _ = magnolia.WriteString(magnolia.Stdout, "\n")
}

//export app_main
func app_main(argc C.int, argv **C.char) C.int {
	args := magnolia.Args(int32(argc), unsafe.Pointer(argv))
	for i := 1; i < len(args); i++ {
		if args[i] == "--help" {
			printHelp()
			return 0
		}
		if args[i] == "--version" {
			printVersion()
			return 0
		}
	}

	opts := unameOpts{}
	i := 1
	for i < len(args) {
		a := args[i]
		if len(a) < 2 || a[0] != '-' || a == "-" {
			break
		}
		for j := 1; j < len(a); j++ {
			switch a[j] {
			case 'a':
				selectAll(&opts)
			case 's':
				opts.sysname = true
			case 'n':
				opts.nodename = true
			case 'r':
				opts.release = true
			case 'v':
				opts.version = true
			case 'm':
				opts.machine = true
			case 'p':
				opts.processor = true
			case 'i':
				opts.hwPlatform = true
			case 'o':
				opts.operatingSystem = true
			default:
				eprintf("usage: uname [-asnrvmpio] [-a]\n")
				return 1
			}
		}
		i++
	}

	if i < len(args) {
		eprintf("uname: extra operand: " + args[i] + "\n")
		return 1
	}

	if !anySelected(&opts) {
		opts.sysname = true
	}
	unamePrint(&opts)
	return 0
}

func main() {}
