#include <stdio.h>
#include <unistd.h>

int m_elf_run_file(const char *path, int argc, char *argv[], int *out_rc);

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char *login_argv[] = { (char *)"login", NULL };

    while (1) {
        int rc = 0;
        int ret = m_elf_run_file("/bin/login", 1, login_argv, &rc);
        if (ret != 0) {
            printf("init: m_elf_run_file(/bin/login) failed ret=%d\n", ret);
            sleep(1);
            continue;
        }
        (void)rc;
        sleep(1);
    }
}

