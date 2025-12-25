/*
 * Minimal symbols expected by TinyGo ESP32 baremetal runtime when building an
 * object file. We provide a small BSS-backed heap and basic BSS boundaries so
 * the TinyGo object can be linked into an ELF applet.
 */

    .section .bss.gohello_tinygo_heap,"aw",@nobits
    .align 4

    .global _sbss
_sbss:

    .global _globals_start
_globals_start:

    .global _heap_start
_heap_start:
    .space 4096

    .global _heap_end
_heap_end:

    .global _globals_end
_globals_end:

    .global _ebss
_ebss:

    .global _stack_top
_stack_top:

    .section .note.GNU-stack,"",@progbits
