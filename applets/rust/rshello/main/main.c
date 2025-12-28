#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

void rshello_fill(uint8_t *buf, size_t len);

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    uint8_t msg[13];
    rshello_fill(msg, sizeof(msg));
    (void)write(1, msg, sizeof(msg));
    return 0;
}

