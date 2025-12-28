#include <stdio.h>
#include <unistd.h>

int m_elf_run_file(const char *path, int argc, char *argv[], int *out_rc);

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    static char login_argv0[] = "login";
    static char login_path[] = "/bin/login";
    char *login_argv[] = { login_argv0, NULL };

    while (1) {
        int rc = 0;
        int ret = m_elf_run_file(login_path, 1, login_argv, &rc);
        if (ret != 0) {
            printf("init: m_elf_run_file(%s) failed ret=%d\n", login_path, ret);
            sleep(1);
            continue;
        }
        (void)rc;
        sleep(1);
    }
}
