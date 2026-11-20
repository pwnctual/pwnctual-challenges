#define _GNU_SOURCE
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Each level hands out math.c and a real problem to solve with math. Like the conditionals and
   loops challenges, the checker changes the values at the top of main (like int seconds = 3725;
   or char letter = 'x';) and runs the program again and again, so only a correct formula passes. */
#define LEVELS 6
#define FILE_C "math.c"
#define TESTS 10
#define OUT 4096

struct level {
    const char *task;     /* shown to the learner */
    const char *source;   /* the math.c they start from */
    const char *vars[3];  /* the variables the checker changes: "i:name" for an int, "c:name" for a char */
    int tests[TESTS][3];  /* the values they get, ends at a row of -1 */
};

static const struct level levels[LEVELS] = {
    {
        "This program should print the average of a, b and c (rounded down, like int division does).\n"
        "But for 70, 80 and 90 it prints 180. Fix it.",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    int a = 70;\n"
        "    int b = 80;\n"
        "    int c = 90;\n"
        "\n"
        "    int avg = a + b + c / 3;\n"
        "    printf(\"%d\\n\", avg);\n"
        "}\n",
        {"i:a", "i:b", "i:c"},
        {{70, 80, 90}, {0, 0, 3}, {10, 20, 31}, {100, 100, 100}, {1, 2, 3}, {5, 5, 6}, {0, 0, 0}, {-1}},
    },
    {
        "This program should print the exact average of a and b.\n"
        "But it says the average of 2 and 3 is 2.000000. Make it print 2.500000.",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    int a = 2;\n"
        "    int b = 3;\n"
        "\n"
        "    float avg = (a + b) / 2;\n"
        "    printf(\"%f\\n\", avg);\n"
        "}\n",
        {"i:a", "i:b"},
        {{2, 3}, {4, 4}, {1, 0}, {7, 10}, {0, 0}, {99, 100}, {-1}},
    },
    {
        "Turn seconds into hours, minutes and seconds.\n"
        "For 3725 seconds, print: 1h 2m 5s\n"
        "(Hint: there are 3600 seconds in an hour, and you can use printf more than once.)",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    int seconds = 3725;\n"
        "\n"
        "}\n",
        {"i:seconds"},
        {{3725}, {0}, {59}, {60}, {3600}, {86399}, {45296}, {-1}},
    },
    {
        "Reverse the digits of a 3-digit number, using only math.\n"
        "For 472, print: 274   (and for 100, print: 1)\n"
        "(Hint: n % 10 gives you the last digit, and n / 10 throws the last digit away.)",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    int n = 472;\n"
        "\n"
        "}\n",
        {"i:n"},
        {{472}, {100}, {999}, {120}, {305}, {101}, {768}, {-1}},
    },
    {
        "A byte can only hold 0 to 255. Add more than that, and it wraps around back to 0,\n"
        "like a car's odometer. This is called an integer overflow, and it is one of the\n"
        "classic ways real programs get hacked.\n"
        "A byte holds value. Add amount to it, and print what the byte actually ends up holding.\n"
        "For 250 + 10, print: 4",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    int value = 250;\n"
        "    int amount = 10;\n"
        "\n"
        "}\n",
        {"i:value", "i:amount"},
        {{250, 10}, {0, 0}, {255, 1}, {100, 100}, {200, 300}, {255, 255}, {1, 254}, {-1}},
    },
    {
        "The Caesar cipher: Julius Caesar hid his messages by shifting every letter\n"
        "a few places down the alphabet, wrapping from z back to a.\n"
        "Shift letter (a lowercase letter) by key places and print the new letter.\n"
        "For 'x' with key 3, print: a\n"
        "(Hint: a char is just a number, its ASCII value. 'a' is a number too,\n"
        "so letter - 'a' tells you how far letter is from a: 0 for a, 1 for b, and so on.)",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    char letter = 'x';\n"
        "    int key = 3;\n"
        "\n"
        "}\n",
        {"c:letter", "i:key"},
        {{'x', 3}, {'a', 1}, {'z', 1}, {'h', 13}, {'m', 0}, {'c', 25}, {'p', 26}, {'a', 51}, {-1}},
    },
};

/* What the program must print for these values */
static void expect(int level, const int *v, char *out, size_t size)
{
    switch (level) {
    case 1: snprintf(out, size, "%d\n", (v[0] + v[1] + v[2]) / 3); break;
    case 2: snprintf(out, size, "%f\n", (float)(v[0] + v[1]) / 2); break;
    case 3: snprintf(out, size, "%dh %dm %ds\n", v[0] / 3600, v[0] % 3600 / 60, v[0] % 60); break;
    case 4: snprintf(out, size, "%d\n", v[0] % 10 * 100 + v[0] / 10 % 10 * 10 + v[0] / 100); break;
    case 5: snprintf(out, size, "%d\n", (v[0] + v[1]) % 256); break;
    case 6: snprintf(out, size, "%c\n", (v[0] - 'a' + v[1]) % 26 + 'a'); break;
    default: out[0] = '\0';
    }
}

#define STATE "/var/lib/pwnctual/level"
#define BUILT "/tmp/.math"

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
    printf("The checker tries your program with lots of different values for ");
    for (int i = 0; i < 3 && levels[level - 1].vars[i]; i++) {
        printf("%s%s", i ? (i + 1 < 3 && levels[level - 1].vars[i + 1] ? ", " : " and ") : "", levels[level - 1].vars[i] + 2);
    }
    puts(", so change them and test it yourself too!\n");
    puts("Open it with:  nano math.c");
    puts("Try it with:   make math && ./math");
    puts("Check it with: /challenge/run");
}

/* Reads math.c as the learner, through a pipe */
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
/* Finds the value in "int <name> = <number>;" (or "char <name> = '<letter>';"),
   and returns where the number (or the letter) starts and ends */
static int find_value(const char *src, const char *var, size_t *start, size_t *end)
{
    int is_char = var[0] == 'c';
    const char *name = var + 2;
    const char *kw = is_char ? "char" : "int";
    size_t kl = strlen(kw);
    for (const char *p = src; (p = strstr(p, kw)); p += kl) {
        if (p > src && ident(p[-1])) {
            continue;
        }
        const char *q = p + kl;
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
        const char *val = q;
        if (is_char) {
            if (q[0] != '\'' || !q[1] || q[1] == '\\' || q[2] != '\'') {
                continue;
            }
            val = q + 1;
            q += 3;
        } else {
            if (*q == '-') {
                q++;
            }
            if (!isdigit((unsigned char)*q)) {
                continue;
            }
            while (isdigit((unsigned char)*q)) {
                q++;
            }
        }
        if (*spaces(q) != ';') {
            continue;
        }
        *start = (size_t)(val - src);
        *end = is_char ? (size_t)(val - src) + 1 : (size_t)(q - src);
        return 1;
    }
    return 0;
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


/* "a is 5" or "letter is 'x'" */
static void say_value(const char *var, int v)
{
    if (var[0] == 'c') {
        printf("%s is '%c'", var + 2, v);
    } else {
        printf("%s is %d", var + 2, v);
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
        printf("Level %d: math.c is missing.\n", level);
        return 1;
    }
    if (!builds()) {
        printf("\nLevel %d: math.c still has a mistake.\n", level);
        return 1;
    }

    /* Where the checker puts its own values */
    size_t start[3], end[3];
    int nvars = 0;
    for (; nvars < 3 && l->vars[nvars]; nvars++) {
        if (!find_value(src, l->vars[nvars], &start[nvars], &end[nvars])) {
            if (l->vars[nvars][0] == 'c') {
                printf("Level %d: keep the line char %s = '<a letter>'; in math.c.\n", level, l->vars[nvars] + 2);
            } else {
                printf("Level %d: keep the line int %s = <a number>; in math.c.\n", level, l->vars[nvars] + 2);
            }
            printf("The checker changes that value to test your program.\n");
            return 1;
        }
    }
    /* Swap values in from the last one in the file to the first, so the positions stay right */
    int order[3] = {0, 1, 2};
    for (int i = 0; i < nvars; i++) {
        for (int j = i + 1; j < nvars; j++) {
            if (start[order[j]] > start[order[i]]) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }

    for (int t = 0; t < TESTS && l->tests[t][0] >= 0; t++) {
        const int *value = l->tests[t];
        static char text[17000];
        strcpy(text, src);
        for (int k = 0; k < nvars; k++) {
            int v = order[k];
            char put[16];
            if (l->vars[v][0] == 'c') {
                put[0] = (char)value[v];
                put[1] = '\0';
            } else {
                snprintf(put, sizeof put, "%d", value[v]);
            }
            memmove(text + start[v] + strlen(put), text + end[v], strlen(text + end[v]) + 1);
            memcpy(text + start[v], put, strlen(put));
        }
        if (!builds_text(text)) {
            printf("Level %d: math.c doesn't compile when the values change.\n", level);
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
            printf("Level %d: when ", level);
            for (int k = 0; k < nvars; k++) {
                if (k) {
                    printf(k + 1 == nvars ? " and " : ", ");
                }
                say_value(l->vars[k], value[k]);
            }
            puts(",");
            if (stuck) {
                puts("it never stopped.");
            } else if (!strchr(want, '\n') && !strchr(got, '\n')) {
                printf("it should print: %s\n", want);
                printf("but it printed:  %s\n", *got ? got : "nothing");
            } else if (!explain(want, got)) {
                puts("it prints the right thing, but something else is off. Check the spaces and newlines (\\n).");
            }
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
