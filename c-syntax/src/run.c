#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Each level hands out hello.c with one mistake in it. */
#define LEVELS 6
static const char *const broken[LEVELS] = {
    /* 1: a missing ; */
    "#include <stdio.h>\n"
    "\n"
    "int main() {\n"
    "    printf(\"Hello, World!\")\n"
    "    return 0;\n"
    "}\n",
    /* 2: Printf instead of printf */
    "#include <stdio.h>\n"
    "\n"
    "int main() {\n"
    "    Printf(\"Hello, World!\");\n"
    "    return 0;\n"
    "}\n",
    /* 3: main without () */
    "#include <stdio.h>\n"
    "\n"
    "int main {\n"
    "    printf(\"Hello, World!\");\n"
    "    return 0;\n"
    "}\n",
    /* 4: a missing } */
    "#include <stdio.h>\n"
    "\n"
    "int main() {\n"
    "    printf(\"Hello, World!\");\n"
    "    return 0;\n",
    /* 5: printf outside main's curly braces */
    "#include <stdio.h>\n"
    "\n"
    "int main() {\n"
    "    return 0;\n"
    "}\n"
    "printf(\"Hello, World!\");\n",
    /* 6: #Include instead of #include */
    "#Include <stdio.h>\n"
    "\n"
    "int main() {\n"
    "    printf(\"Hello, World!\");\n"
    "    return 0;\n"
    "}\n",
};

#define STATE "/var/lib/pwnctual/level"
#define BUILT "/tmp/.hello"

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

static int read_level(void)
{
    int level = 0;
    FILE *f = fopen(STATE, "r");
    if (f) {
        if (fscanf(f, "%d", &level) != 1) {
            level = 0;
        }
        fclose(f);
    }
    return level;
}

static void save_level(int level)
{
    FILE *f = fopen(STATE, "w");
    if (!f) {
        exit(1);
    }
    fprintf(f, "%d\n", level);
    fclose(f);
}

static int wait_ok(pid_t pid)
{
    int status;
    return waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void hand_out(int level)
{
    pid_t pid = fork();
    if (pid == 0) {
        drop();
        if (chdir(home)) {
            _exit(1);
        }
        FILE *f = fopen("hello.c", "w");
        if (!f || fputs(broken[level - 1], f) == EOF || fclose(f)) {
            _exit(1);
        }
        _exit(0);
    }
    if (pid < 0 || !wait_ok(pid)) {
        exit(1);
    }
    printf("Level %d of %d: fix hello.c so it prints Hello, World!\n", level, LEVELS);
    puts("Open it with:  nano hello.c");
    puts("Check it with: /challenge/run");
}

static int builds(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        drop();
        if (chdir(home)) {
            _exit(1);
        }
        unlink(BUILT);
        execlp("gcc", "gcc", "hello.c", "-o", BUILT, (char *)NULL);
        _exit(1);
    }
    return pid > 0 && wait_ok(pid);
}

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
        alarm(3);
        execl(BUILT, BUILT, (char *)NULL);
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

    int level = read_level();
    if (level < 1) {
        save_level(1);
        hand_out(1);
        return 0;
    }
    if (level > LEVELS) {
        print_flag();
        return 0;
    }

    if (!builds()) {
        printf("\nLevel %d: hello.c still has a mistake.\n", level);
        return 1;
    }
    if (!prints_hello()) {
        printf("Level %d: hello.c runs, but it doesn't print Hello, World!\n", level);
        return 1;
    }

    printf("Level %d fixed!\n\n", level);
    save_level(level + 1);
    if (level == LEVELS) {
        print_flag();
    } else {
        hand_out(level + 1);
    }
    return 0;
}
