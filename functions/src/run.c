#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Each level hands out hello.c and a task. The learner edits hello.c until it prints
   the right thing, using functions the way the Functions lesson teaches. */
#define LEVELS 5

struct level {
    const char *task;   /* shown to the learner */
    const char *source; /* the hello.c they start from */
    const char *output; /* what the program must print */
    int one_printf;     /* 1: hello.c may only have one printf, so main can't just print it */
    const char *needs[3]; /* text hello.c must still have */
};

static const struct level levels[LEVELS] = {
    {
        "main calls hello(), but hello doesn't exist yet.\n"
        "Write the hello function so the program prints: hello",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    hello();\n"
        "}\n",
        "hello", 0, {"hello();", NULL},
    },
    {
        "The hello function is written, but nobody calls it.\n"
        "Make the program print: hello",
        "#include <stdio.h>\n"
        "\n"
        "int hello() {\n"
        "    printf(\"hello\");\n"
        "}\n"
        "\n"
        "int main() {\n"
        "}\n",
        "hello", 1, {"hello();", "printf(\"hello\")", NULL},
    },
    {
        "This program has no main.\n"
        "Make it print: hello",
        "#include <stdio.h>\n"
        "\n"
        "int hello() {\n"
        "    printf(\"hello\");\n"
        "}\n",
        "hello", 1, {"hello();", "printf(\"hello\")", NULL},
    },
    {
        "main calls pwn() and ctual(), but neither of them exists yet.\n"
        "Write both functions so the program prints: pwnctual",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    pwn();\n"
        "    ctual();\n"
        "}\n",
        "pwnctual", 0, {"pwn();", "ctual();", NULL},
    },
    {
        "Make the program print: hellohello\n"
        "But you can't add another printf.",
        "#include <stdio.h>\n"
        "\n"
        "int hello() {\n"
        "    printf(\"hello\");\n"
        "}\n"
        "\n"
        "int main() {\n"
        "    hello();\n"
        "}\n",
        "hellohello", 1, {"hello();", "printf(\"hello\")", NULL},
    },
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
        if (!f || fputs(levels[level - 1].source, f) == EOF || fclose(f)) {
            _exit(1);
        }
        _exit(0);
    }
    if (pid < 0 || !wait_ok(pid)) {
        exit(1);
    }
    printf("Level %d of %d\n\n%s\n\n", level, LEVELS, levels[level - 1].task);
    puts("Open it with:  nano hello.c");
    puts("Try it with:   make hello && ./hello");
    puts("Check it with: /challenge/run");
}

/* Reads hello.c as the learner, through a pipe */
static char *read_source(void)
{
    static char src[16384];
    int fds[2];
    if (pipe(fds)) {
        return NULL;
    }
    pid_t pid = fork();
    if (pid == 0) {
        drop();
        close(fds[0]);
        if (chdir(home)) {
            _exit(1);
        }
        FILE *f = fopen("hello.c", "r");
        if (!f) {
            _exit(1);
        }
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
            if (write(fds[1], buf, n) < 0) {
                _exit(1);
            }
        }
        _exit(0);
    }
    close(fds[1]);
    if (pid < 0) {
        close(fds[0]);
        return NULL;
    }
    size_t len = 0;
    ssize_t n;
    while (len < sizeof src - 1 && (n = read(fds[0], src + len, sizeof src - 1 - len)) > 0) {
        len += (size_t)n;
    }
    src[len] = '\0';
    close(fds[0]);
    return wait_ok(pid) ? src : NULL;
}

static int count(const char *text, const char *word)
{
    int n = 0;
    for (const char *p = text; (p = strstr(p, word)); p += strlen(word)) {
        n++;
    }
    return n;
}

/* Copies text without spaces, tabs or newlines, so int age=25; and int age = 25; look the same */
static void squash(const char *text, char *out, size_t size)
{
    size_t n = 0;
    for (; *text && n < size - 1; text++) {
        if (*text != ' ' && *text != '\t' && *text != '\n' && *text != '\r') {
            out[n++] = *text;
        }
    }
    out[n] = '\0';
}

/* Whether the source has this text, however it's spaced out */
static int has(const char *src, const char *text)
{
    static char s[16384];
    char t[256];
    squash(src, s, sizeof s);
    squash(text, t, sizeof t);
    return strstr(s, t) != NULL;
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

/* Runs the program as the learner and checks what it prints */
static int prints(const char *want)
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
    if (len && out[len - 1] == '\n') {
        out[len - 1] = '\0';
    }
    return strcmp(out, want) == 0;
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
    const struct level *l = &levels[level - 1];

    char *src = read_source();
    if (!src) {
        printf("Level %d: hello.c is missing.\n", level);
        return 1;
    }
    if (!builds()) {
        printf("\nLevel %d: hello.c still has a mistake.\n", level);
        return 1;
    }
    if (!prints(l->output)) {
        printf("Level %d: hello.c runs, but it doesn't print %s\n", level, l->output);
        return 1;
    }
    if (l->one_printf && count(src, "printf") != 1) {
        printf("Level %d: it prints %s, but hello.c should only have one printf.\n", level, l->output);
        printf("Let the hello function do the printing.\n");
        return 1;
    }
    for (int i = 0; l->needs[i]; i++) {
        if (!has(src, l->needs[i])) {
            printf("Level %d: it prints %s, but hello.c should still have %s\n", level, l->output, l->needs[i]);
            return 1;
        }
    }

    printf("Level %d done!\n\n", level);
    save_level(level + 1);
    if (level == LEVELS) {
        print_flag();
    } else {
        hand_out(level + 1);
    }
    return 0;
}
