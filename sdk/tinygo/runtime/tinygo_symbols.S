/*
 * Minimal symbols expected by the TinyGo ESP32 baremetal runtime when linking
 * a TinyGo object into a Magnolia ELF applet.
 *
 * We provide a small BSS-backed heap and basic BSS boundaries. The heap is
 * intentionally small to stay within Magnolia's job allocator constraints.
 */

#ifndef MAGNOLIA_TINYGO_HEAP_SIZE
#define MAGNOLIA_TINYGO_HEAP_SIZE 4096
#endif

    .section .bss.magnolia_tinygo_heap,"aw",@nobits
    .align 4

    .global _sbss
_sbss:

    .global _globals_start
_globals_start:

    .global _heap_start
_heap_start:
    .space MAGNOLIA_TINYGO_HEAP_SIZE

    .global _heap_end
_heap_end:

    .global _globals_end
_globals_end:

    .global _ebss
_ebss:

    .global _stack_top
_stack_top:

    .section .note.GNU-stack,"",@progbits

