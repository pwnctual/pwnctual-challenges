#define _GNU_SOURCE
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Each level hands out loops.c and a task. Like the conditionals challenge, the checker
   changes the numbers at the top of main (like int n = 5;) and runs the program again and
   again. It also catches loops that never stop, and points at the first line that's wrong. */
#define LEVELS 6
#define FILE_C "loops.c"
#define TESTS 8
#define OUT 4096

struct level {
    const char *task;     /* shown to the learner */
    const char *source;   /* the loops.c they start from */
    const char *vars[2];  /* the variables the checker changes */
    int tests[TESTS][2];  /* the values they get, ends at a row of -1 */
    const char *rule;     /* shown when a check below fails */
    const char *needs[3]; /* text loops.c must have */
    const char *forbid[3]; /* text loops.c must not have */
};

static const struct level levels[LEVELS] = {
    {
        "This program should print the numbers from 1 to n, one on each line.\n"
        "But it's off by one. Fix it.",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    int n = 5;\n"
        "\n"
        "    for (int i = 0; i <= n; i++) {\n"
        "        printf(\"%d\\n\", i);\n"
        "    }\n"
        "}\n",
        {"n", NULL},
        {{5}, {1}, {3}, {10}, {0}, {-1}},
        NULL, {NULL}, {NULL},
    },
    {
        "This countdown should print n, n-1, ... down to 1, one on each line, then: Liftoff!\n"
        "But it never stops. Fix it.",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    int n = 5;\n"
        "\n"
        "    while (n > 0) {\n"
        "        printf(\"%d\\n\", n);\n"
        "    }\n"
        "    printf(\"Liftoff!\\n\");\n"
        "}\n",
        {"n", NULL},
        {{5}, {1}, {3}, {0}, {-1}},
        NULL, {NULL}, {NULL},
    },
    {
        "Scan every port from first to last (including last), one on each line, like:\n"
        "  Scanning port 20\n"
        "  Scanning port 21\n"
        "  ...",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    int first = 20;\n"
        "    int last = 25;\n"
        "\n"
        "}\n",
        {"first", "last"},
        {{20, 25}, {80, 83}, {1, 1}, {440, 445}, {5, 4}, {-1}},
        NULL, {NULL}, {NULL},
    },
    {
        "Every extra bit doubles how many different values you can store:\n"
        "1 bit can store 2 values, 2 bits can store 4, and a byte (8 bits) can store 256.\n"
        "Print how many different values bits bits can store.",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    int bits = 8;\n"
        "\n"
        "}\n",
        {"bits", NULL},
        {{8}, {1}, {2}, {4}, {10}, {16}, {0}, {-1}},
        NULL, {NULL}, {NULL},
    },
    {
        "Print the numbers from 1 to n, one on each line, but skip the number skip.\n"
        "Use continue to skip it.",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    int n = 10;\n"
        "    int skip = 4;\n"
        "\n"
        "}\n",
        {"n", "skip"},
        {{10, 4}, {5, 1}, {5, 5}, {3, 7}, {1, 1}, {-1}},
        "Skip the number with continue.",
        {"continue", NULL},
        {NULL},
    },
    {
        "Crack the pin! Try every number starting from 0 with a loop,\n"
        "and stop with break as soon as you find it. Then print: Cracked! The pin is <the pin>\n"
        "Print the number your loop found, not pin itself.",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    int pin = 4821;\n"
        "\n"
        "}\n",
        {"pin", NULL},
        {{4821}, {0}, {7}, {9999}, {1337}, {-1}},
        "Find the pin with a loop that tries 0, 1, 2, ... and stops with break.\n"
        "Print the number your loop found, not pin itself.",
        {"break", NULL},
        {",pin)", "=pin;", NULL},
    },
};

/* What the program must print for these values */
static void expect(int level, const int *v, char *out, size_t size)
{
    size_t n = 0;
    out[0] = '\0';
#define ADD(...) (n += (size_t)snprintf(out + n, n < size ? size - n : 0, __VA_ARGS__))
    switch (level) {
    case 1:
        for (int i = 1; i <= v[0]; i++) ADD("%d\n", i);
        break;
    case 2:
        for (int i = v[0]; i > 0; i--) ADD("%d\n", i);
        ADD("Liftoff!\n");
        break;
    case 3:
        for (int i = v[0]; i <= v[1]; i++) ADD("Scanning port %d\n", i);
        break;
    case 4:
        ADD("%d\n", 1 << v[0]);
        break;
    case 5:
        for (int i = 1; i <= v[0]; i++) {
            if (i != v[1]) ADD("%d\n", i);
        }
        break;
    case 6:
        ADD("Cracked! The pin is %d\n", v[0]);
        break;
    }
#undef ADD
}

#define STATE "/var/lib/pwnctual/level"
#define BUILT "/tmp/.loops"

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
        FILE *f = fopen(FILE_C, "w");
        if (!f || fputs(levels[level - 1].source, f) == EOF || fclose(f)) {
            _exit(1);
        }
        _exit(0);
    }
    if (pid < 0 || !wait_ok(pid)) {
        exit(1);
    }
    printf("Level %d of %d\n\n%s\n\n", level, LEVELS, levels[level - 1].task);
    if (levels[level - 1].vars[0]) {
        printf("The checker tries your program with lots of different values for %s", levels[level - 1].vars[0]);
        if (levels[level - 1].vars[1]) {
            printf(" and %s", levels[level - 1].vars[1]);
        }
        puts(", so change the numbers and test it yourself too!\n");
    }
    puts("Open it with:  nano loops.c");
    puts("Try it with:   make loops && ./loops");
    puts("Check it with: /challenge/run");
}

/* Reads loops.c as the learner, through a pipe */
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
        FILE *f = fopen(FILE_C, "r");
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

/* Copies text without spaces, tabs or newlines, so x>10 and x > 10 look the same */
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

static int ident(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static const char *spaces(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    return p;
}

/* Finds the number in "int <name> = <number>;" and returns where it starts and ends */
static int find_value(const char *src, const char *name, size_t *start, size_t *end)
{
    for (const char *p = src; (p = strstr(p, "int")); p += 3) {
        if (p > src && ident(p[-1])) {
            continue;
        }
        const char *q = p + 3;
        if (!isspace((unsigned char)*q)) {
            continue;
        }
        q = spaces(q);
        size_t n = strlen(name);
        if (strncmp(q, name, n) || ident(q[n])) {
            continue;
        }
        q = spaces(q + n);
        if (*q != '=') {
            continue;
        }
        q = spaces(q + 1);
        const char *num = q;
        if (*q == '-') {
            q++;
        }
        if (!isdigit((unsigned char)*q)) {
            continue;
        }
        while (isdigit((unsigned char)*q)) {
            q++;
        }
        if (*spaces(q) != ';') {
            continue;
        }
        *start = (size_t)(num - src);
        *end = (size_t)(q - src);
        return 1;
    }
    return 0;
}

/* Compiles loops.c as it is, showing the compiler's messages */
static int builds(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        drop();
        if (chdir(home)) {
            _exit(1);
        }
        unlink(BUILT);
        execlp("gcc", "gcc", FILE_C, "-o", BUILT, (char *)NULL);
        _exit(1);
    }
    return pid > 0 && wait_ok(pid);
}

/* Compiles this text quietly, as the learner */
static int builds_text(const char *text)
{
    int fds[2];
    if (pipe(fds)) {
        return 0;
    }
    pid_t pid = fork();
    if (pid == 0) {
        drop();
        dup2(fds[0], STDIN_FILENO);
        close(fds[0]);
        close(fds[1]);
        int null = open("/dev/null", O_WRONLY);
        if (null >= 0) {
            dup2(null, STDOUT_FILENO);
            dup2(null, STDERR_FILENO);
        }
        unlink(BUILT);
        execlp("gcc", "gcc", "-w", "-x", "c", "-", "-o", BUILT, (char *)NULL);
        _exit(1);
    }
    close(fds[0]);
    if (pid < 0) {
        close(fds[1]);
        return 0;
    }
    size_t len = strlen(text);
    signal(SIGPIPE, SIG_IGN);
    int ok = write(fds[1], text, len) == (ssize_t)len;
    close(fds[1]);
    return wait_ok(pid) && ok;
}

/* Runs the program as the learner and returns what it prints */

static int stuck; /* the last run never stopped, or printed far too much */

/* Runs the program as the learner and returns what it prints */
static const char *run_program(void)
{
    static char out[OUT];
    int fds[2];
    stuck = 0;
    if (pipe(fds)) {
        return "";
    }
    pid_t pid = fork();
    if (pid == 0) {
        drop();
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]);
        close(fds[1]);
        alarm(2);
        execl(BUILT, BUILT, (char *)NULL);
        _exit(1);
    }
    close(fds[1]);
    size_t len = 0;
    if (pid > 0) {
        ssize_t n;
        while (len < sizeof out - 1 && (n = read(fds[0], out + len, sizeof out - 1 - len)) > 0) {
            len += (size_t)n;
        }
    }
    out[len] = '\0';
    close(fds[0]);
    if (pid > 0) {
        if (len == sizeof out - 1) {
            stuck = 1;
        }
        kill(pid, SIGKILL);
        int status;
        if (waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM) {
            stuck = 1;
        }
    }
    return out;
}

/* Copies line number k (from 0) of text into line, without the newline. 0 if there's no such line. */
static int line_at(const char *text, int k, char *line, size_t size)
{
    for (; k > 0; k--) {
        text = strchr(text, '\n');
        if (!text) {
            return 0;
        }
        text++;
    }
    if (!*text) {
        return 0;
    }
    size_t n = strcspn(text, "\n");
    if (n >= size) {
        n = size - 1;
    }
    memcpy(line, text, n);
    line[n] = '\0';
    return 1;
}

/* Says where the output first goes wrong. 0 if it's all right. */
static int explain(const char *want, const char *got)
{
    char w[256], g[256];
    for (int k = 0;; k++) {
        int hw = line_at(want, k, w, sizeof w);
        int hg = line_at(got, k, g, sizeof g);
        if (!hw && !hg) {
            return 0;
        }
        if (hw && hg && strcmp(w, g) == 0) {
            continue;
        }
        if (!hg) {
            printf("it stopped after %d line%s, but line %d should be: %s\n", k, k == 1 ? "" : "s", k + 1, w);
        } else if (!hw) {
            if (k == 0) {
                printf("it should print nothing, but it printed: %s\n", g);
            } else {
                printf("it should stop after %d line%s, but then it also printed: %s\n", k, k == 1 ? "" : "s", g);
            }
        } else {
            printf("line %d should be: %s\n", k + 1, w);
            printf("but it was:       %s\n", g);
        }
        return 1;
    }
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
        printf("Level %d: loops.c is missing.\n", level);
        return 1;
    }
    if (!builds()) {
        printf("\nLevel %d: loops.c still has a mistake.\n", level);
        return 1;
    }

    /* Where the checker puts its own numbers */
    size_t start[2], end[2];
    int nvars = 0;
    for (; nvars < 2 && l->vars[nvars]; nvars++) {
        if (!find_value(src, l->vars[nvars], &start[nvars], &end[nvars])) {
            printf("Level %d: keep the line int %s = <a number>; in loops.c.\n", level, l->vars[nvars]);
            printf("The checker changes that number to test your program.\n");
            return 1;
        }
    }

    for (int t = 0; t < TESTS && l->tests[t][0] >= 0; t++) {
        const int *value = l->tests[t];
        /* Swap in this test's values, the later one first so the positions stay right */
        static char text[17000];
        strcpy(text, src);
        int later = nvars == 2 && start[1] > start[0];
        for (int k = 0; k < nvars; k++) {
            int v = k == 0 ? later : !later;
            char num[16];
            snprintf(num, sizeof num, "%d", value[v]);
            memmove(text + start[v] + strlen(num), text + end[v], strlen(text + end[v]) + 1);
            memcpy(text + start[v], num, strlen(num));
        }
        if (!builds_text(text)) {
            printf("Level %d: loops.c doesn't compile when the numbers change.\n", level);
            return 1;
        }
        char want[OUT];
        expect(level, value, want, sizeof want);
        char got[OUT];
        strcpy(got, run_program());
        /* The very last newline is optional */
        size_t gl = strlen(got), wl = strlen(want);
        if (gl && got[gl - 1] == '\n') got[gl - 1] = '\0';
        if (wl && want[wl - 1] == '\n') want[wl - 1] = '\0';
        if (stuck || strcmp(got, want) != 0) {
            printf("Level %d: when %s is %d", level, l->vars[0], value[0]);
            if (nvars == 2) {
                printf(" and %s is %d", l->vars[1], value[1]);
            }
            puts(",");
            if (stuck) {
                puts("it never stopped. Does something in your loop ever make the condition false?");
            } else if (!explain(want, got)) {
                puts("it prints the right lines, but something else is off. Check the spaces and newlines (\\n).");
            }
            return 1;
        }
    }

    for (int i = 0; l->needs[i]; i++) {
        if (!has(src, l->needs[i])) {
            printf("Level %d: it prints the right things, but not the way this level asks.\n%s\n", level, l->rule);
            return 1;
        }
    }
    for (int i = 0; l->forbid[i]; i++) {
        if (has(src, l->forbid[i])) {
            printf("Level %d: it prints the right things, but not the way this level asks.\n%s\n", level, l->rule);
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
