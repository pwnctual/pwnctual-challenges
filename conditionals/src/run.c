#define _GNU_SOURCE
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Each level hands out conditionals.c and a task. The checker doesn't run the program once:
   it changes the numbers at the top of main (like int age = 15;) and runs it again and again,
   so the conditions have to be right for every value, edge cases included. */
#define LEVELS 7
#define FILE_C "conditionals.c"
#define TESTS 10

struct test {
    int value[2];       /* the values the variables get */
    const char *output; /* what the program must print then ("" for nothing) */
};

struct level {
    const char *task;     /* shown to the learner */
    const char *source;   /* the conditionals.c they start from */
    const char *vars[2];  /* the variables the checker changes */
    struct test tests[TESTS];
    const char *rule;     /* shown when a check below fails */
    const char *needs[3]; /* text conditionals.c must have */
    const char *forbid[3]; /* text conditionals.c must not have */
};

static const struct level levels[LEVELS] = {
    {
        "This program always says the door is open, even when door is 0.\n"
        "Fix it, so it says the door is open only when door is 1.",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    int door = 0;\n"
        "\n"
        "    if (door = 1) {\n"
        "        printf(\"The door is open\\n\");\n"
        "    }\n"
        "    else {\n"
        "        printf(\"The door is closed\\n\");\n"
        "    }\n"
        "}\n",
        {"door", NULL},
        {
            {{0}, "The door is closed"},
            {{1}, "The door is open"},
            {{0}, "The door is closed"},
        },
        NULL, {NULL}, {NULL},
    },
    {
        "Print: You can vote         if age is 18 or more\n"
        "Print: You can't vote yet   if it isn't",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    int age = 15;\n"
        "\n"
        "}\n",
        {"age", NULL},
        {
            {{15}, "You can't vote yet"},
            {{0}, "You can't vote yet"},
            {{17}, "You can't vote yet"},
            {{18}, "You can vote"},
            {{19}, "You can vote"},
            {{40}, "You can vote"},
        },
        NULL, {NULL}, {NULL},
    },
    {
        "This grader should print:\n"
        "  Excellent   if score is 90 or more\n"
        "  Pass        if score is 50 or more\n"
        "  Fail        if it's less than that\n"
        "But it prints Pass for a 95. Fix it.",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    int score = 95;\n"
        "\n"
        "    if (score >= 50) {\n"
        "        printf(\"Pass\\n\");\n"
        "    }\n"
        "    else if (score >= 90) {\n"
        "        printf(\"Excellent\\n\");\n"
        "    }\n"
        "    else {\n"
        "        printf(\"Fail\\n\");\n"
        "    }\n"
        "}\n",
        {"score", NULL},
        {
            {{95}, "Excellent"},
            {{0}, "Fail"},
            {{49}, "Fail"},
            {{50}, "Pass"},
            {{75}, "Pass"},
            {{89}, "Pass"},
            {{90}, "Excellent"},
            {{100}, "Excellent"},
        },
        NULL, {NULL}, {NULL},
    },
    {
        "A shop is open from 9, up to (but not including) 17.\n"
        "Print: Open     if hour is when the shop is open\n"
        "Print: Closed   if it isn't",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    int hour = 10;\n"
        "\n"
        "}\n",
        {"hour", NULL},
        {
            {{10}, "Open"},
            {{0}, "Closed"},
            {{8}, "Closed"},
            {{9}, "Open"},
            {{12}, "Open"},
            {{16}, "Open"},
            {{17}, "Closed"},
            {{23}, "Closed"},
        },
        NULL, {NULL}, {NULL},
    },
    {
        "Days go from 1 (Monday) to 7 (Sunday).\n"
        "Print: Weekend   if day is 6 or 7\n"
        "Print: Weekday   if it isn't",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    int day = 6;\n"
        "\n"
        "}\n",
        {"day", NULL},
        {
            {{6}, "Weekend"},
            {{1}, "Weekday"},
            {{3}, "Weekday"},
            {{5}, "Weekday"},
            {{7}, "Weekend"},
        },
        NULL, {NULL}, {NULL},
    },
    {
        "This if does nothing, and all the work is in the else.\n"
        "Rewrite it with just one if and no else, using ! (NOT).\n"
        "It should still print: Leave the umbrella   only when it is not raining",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    int is_raining = 0;\n"
        "\n"
        "    if (is_raining) {\n"
        "    }\n"
        "    else {\n"
        "        printf(\"Leave the umbrella\\n\");\n"
        "    }\n"
        "}\n",
        {"is_raining", NULL},
        {
            {{0}, "Leave the umbrella"},
            {{1}, ""},
            {{0}, "Leave the umbrella"},
            {{7}, ""},
        },
        "Use just one if, with ! in its condition, and no else.",
        {"!", NULL},
        {"else", NULL},
    },
    {
        "The vault.\n"
        "Print: Access granted   if pin is 1337\n"
        "Print: Wrong pin        if it isn't\n"
        "But if tries is 3 or more, print: Locked   even if the pin is right.",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    int pin = 1337;\n"
        "    int tries = 0;\n"
        "\n"
        "}\n",
        {"pin", "tries"},
        {
            {{1337, 0}, "Access granted"},
            {{1234, 0}, "Wrong pin"},
            {{1337, 2}, "Access granted"},
            {{1234, 2}, "Wrong pin"},
            {{1337, 3}, "Locked"},
            {{1234, 3}, "Locked"},
            {{1337, 5}, "Locked"},
            {{0, 9}, "Locked"},
            {{1336, 1}, "Wrong pin"},
        },
        NULL, {NULL}, {NULL},
    },
};

#define STATE "/var/lib/pwnctual/level"
#define BUILT "/tmp/.conditionals"

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
    puts("Open it with:  nano conditionals.c");
    puts("Try it with:   make conditionals && ./conditionals");
    puts("Check it with: /challenge/run");
}

/* Reads conditionals.c as the learner, through a pipe */
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

/* Compiles conditionals.c as it is, showing the compiler's messages */
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
static const char *run_program(void)
{
    static char out[256];
    int fds[2];
    if (pipe(fds)) {
        return "";
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
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }
    if (len && out[len - 1] == '\n') {
        out[len - 1] = '\0';
    }
    return out;
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

static void say(const char *what, const char *text)
{
    if (*text) {
        printf("%s: %s\n", what, text);
    } else {
        printf("%s nothing\n", what);
    }
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
        printf("Level %d: conditionals.c is missing.\n", level);
        return 1;
    }
    if (!builds()) {
        printf("\nLevel %d: conditionals.c still has a mistake.\n", level);
        return 1;
    }

    /* Where the checker puts its own numbers */
    size_t start[2], end[2];
    int nvars = 0;
    for (; nvars < 2 && l->vars[nvars]; nvars++) {
        if (!find_value(src, l->vars[nvars], &start[nvars], &end[nvars])) {
            printf("Level %d: keep the line int %s = <a number>; in conditionals.c.\n", level, l->vars[nvars]);
            printf("The checker changes that number to test your program.\n");
            return 1;
        }
    }

    for (int t = 0; t < TESTS && l->tests[t].output; t++) {
        const struct test *test = &l->tests[t];
        /* Swap in this test's values, last one first so the positions stay right */
        static char text[17000];
        strcpy(text, src);
        int later = nvars == 2 && start[1] > start[0];
        for (int k = 0; k < nvars; k++) {
            int v = k == 0 ? later : !later;
            char num[16];
            snprintf(num, sizeof num, "%d", test->value[v]);
            memmove(text + start[v] + strlen(num), text + end[v], strlen(text + end[v]) + 1);
            memcpy(text + start[v], num, strlen(num));
        }
        if (!builds_text(text)) {
            printf("Level %d: conditionals.c doesn't compile when the numbers change.\n", level);
            return 1;
        }
        const char *got = run_program();
        if (strcmp(got, test->output) != 0) {
            printf("Level %d: when %s is %d", level, l->vars[0], test->value[0]);
            if (nvars == 2) {
                printf(" and %s is %d", l->vars[1], test->value[1]);
            }
            puts(",");
            say("it should print", test->output);
            say("but it printed", got);
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
