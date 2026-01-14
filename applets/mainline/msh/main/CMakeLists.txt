idf_component_register(
    SRCS
        "../setbrk.c"
        "../blok.c"
        "../stak.c"
        "../cmd.c"
        "../fault.c"
        "../main.c"
        "../word.c"
        "../string.c"
        "../name.c"
        "../args.c"
        "../xec.c"
        "../service.c"
        "../error.c"
        "../io.c"
        "../print.c"
        "../macro.c"
        "../expand.c"
        "../ctype.c"
        "../msg.c"
        "../test.c"
        "../defs.c"
        "../echo.c"
        "../hash.c"
        "../hashserv.c"
        "../pwd.c"
        "../func.c"
    INCLUDE_DIRS ".."
)

# Magnolia ELF applets are linked as shared objects; ensure PIC on RISC-V.
target_compile_options(${COMPONENT_LIB} PRIVATE -fPIC)
