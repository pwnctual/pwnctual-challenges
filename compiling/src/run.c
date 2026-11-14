#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* The learner gets hello.c, compiles it with make, and this checks the result */
static const char *const source =
    "#include <stdio.h>\n"
    "\n"
    "int main() {\n"
    "    printf(\"Hello, World!\");\n"
    "    return 0;\n"
    "}\n";

static const char *home;

/* Everything that touches the learner's files runs as the learner, not as root */
static void drop(void)
{
    gid_t g = getgid();
    uid_t u = getuid();
    if (setresgid(g, g, g) || setresuid(u, u, u)) {
        _exit(1);
    }
}

static int wait_ok(pid_t pid)
{
    int status;
    return waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* Runs as the learner: exit 0 if the file exists */
static int exists(const char *name)
{
    pid_t pid = fork();
    if (pid == 0) {
        drop();
        struct stat st;
        _exit(chdir(home) == 0 && lstat(name, &st) == 0 ? 0 : 1);
    }
    return pid > 0 && wait_ok(pid);
}

static void hand_out(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        drop();
        if (chdir(home)) {
            _exit(1);
        }
        FILE *f = fopen("hello.c", "w");
        if (!f || fputs(source, f) == EOF || fclose(f)) {
            _exit(1);
        }
        _exit(0);
    }
    if (pid < 0 || !wait_ok(pid)) {
        exit(1);
    }
}

/* Runs ./hello as the learner and checks it prints Hello, World! */
static int prints_hello(void)
{
    int fds[2];
    if (pipe(fds)) {
        return 0;
    }
    pid_t pid = fork();
    if (pid == 0) {
        drop();
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]);
        close(fds[1]);
        if (chdir(home)) {
            _exit(1);
        }
        /* Only a real compiled program counts, not a script */
        FILE *f = fopen("hello", "rb");
        unsigned char magic[4] = {0};
        if (!f || fread(magic, 1, 4, f) != 4 || memcmp(magic, "\x7f" "ELF", 4) != 0) {
            _exit(2);
        }
        fclose(f);
        alarm(3);
        execl("./hello", "./hello", (char *)NULL);
        _exit(1);
    }
    close(fds[1]);
    if (pid < 0) {
        close(fds[0]);
        return 0;
    }
    char out[256];
    size_t len = 0;
    ssize_t n;
    while (len < sizeof out - 1 && (n = read(fds[0], out + len, sizeof out - 1 - len)) > 0) {
        len += (size_t)n;
    }
    out[len] = '\0';
    close(fds[0]);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    return strcmp(out, "Hello, World!") == 0 || strcmp(out, "Hello, World!\n") == 0;
}

static void print_flag(void)
{
    FILE *flag = fopen("/flag", "r");
    if (!flag) {
        exit(1);
    }
    char buf[128];
    if (fgets(buf, sizeof buf, flag)) {
        buf[strcspn(buf, "\n")] = '\0';
        puts(buf);
    }
    fclose(flag);
}

int main(void)
{
    home = getenv("HOME");
    if (!home || !*home) {
        home = "/home/hacker";
    }
    setvbuf(stdout, NULL, _IONBF, 0);

    if (!exists("hello")) {
        if (!exists("hello.c")) {
            hand_out();
        }
        puts("Compile hello.c with:  make hello");
        puts("Then check it with:    /challenge/run");
        return 0;
    }

    if (!prints_hello()) {
        puts("hello doesn't print Hello, World!");
        puts("Compile hello.c with:  make hello");
        return 1;
    }

    puts("You compiled hello.c with make!\n");
    print_flag();
    return 0;
}
