/*
 * Magnolia ELF applet: gohello (TinyGo).
 * Prints "Hello world!" via write(1, ...).
 */

extern int gohello_go_main(void);

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return gohello_go_main();
}
