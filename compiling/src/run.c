#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Hands out flag.c: a program that prints the flag once it's compiled with make and run.
   The flag is stored scrambled, so reading flag.c doesn't give it away. */
#define KEY 42

int main(void)
{
    const char *home = getenv("HOME");
    if (!home || !*home) {
        home = "/home/hacker";
    }

    /* Read the flag as root */
    char flag[128] = {0};
    FILE *f = fopen("/flag", "r");
    if (!f || !fgets(flag, sizeof flag, f)) {
        return 1;
    }
    fclose(f);
    flag[strcspn(flag, "\n")] = '\0';

    /* Write flag.c as the learner */
    pid_t pid = fork();
    if (pid == 0) {
        gid_t g = getgid();
        uid_t u = getuid();
        if (setresgid(g, g, g) || setresuid(u, u, u) || chdir(home)) {
            _exit(1);
        }
        FILE *out = fopen("flag.c", "w");
        if (!out) {
            _exit(1);
        }
        fputs("#include <stdio.h>\n\nint main() {\n    unsigned char secret[] = {", out);
        for (size_t i = 0; flag[i]; i++) {
            fprintf(out, "%s%d", i ? ", " : "", (unsigned char)flag[i] ^ KEY);
        }
        fprintf(out, "};\n"
                     "    for (int i = 0; i < sizeof(secret); i++) {\n"
                     "        putchar(secret[i] ^ %d);\n"
                     "    }\n"
                     "    printf(\"\\n\");\n"
                     "    return 0;\n"
                     "}\n", KEY);
        _exit(fclose(out) ? 1 : 0);
    }
    int status;
    if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status)) {
        return 1;
    }

    puts("You got flag.c!");
    puts("Compile it with:  make flag");
    puts("Then run it with: ./flag");
    return 0;
}
