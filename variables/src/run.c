#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Each level hands out variables.c and a task. The learner edits variables.c until it prints
   the right thing, using variable types and printf specifiers the way the Variables lesson teaches. */
#define LEVELS 6
#define FILE_C "variables.c"

struct level {
    const char *task;     /* shown to the learner */
    const char *source;   /* the variables.c they start from */
    const char *output;   /* what the program must print */
    const char *rule;     /* shown when a check below fails */
    const char *needs[5]; /* text variables.c must have */
    const char *forbid[3]; /* text variables.c must not have */
};

static const struct level levels[LEVELS] = {
    {
        "main prints x, but x doesn't exist yet.\n"
        "Make x an int that holds 1337, so the program prints: 1337",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    printf(\"%i\\n\", x);\n"
        "}\n",
        "1337",
        "Keep printf(\"%i\\n\", x); and make x hold 1337.",
        {"printf(\"%i\\n\", x);", "int x", NULL},
        {NULL},
    },
    {
        "grade holds 'A', but the program prints 65.\n"
        "Make it print: A",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    char grade = 'A';\n"
        "    printf(\"%i\\n\", grade);\n"
        "}\n",
        "A",
        "Keep char grade = 'A'; and print grade with the right specifier.",
        {"char grade = 'A';", "%c", "\\n\", grade);", NULL},
        {"%i", "%d", NULL},
    },
    {
        "pi is a float, but the program prints the wrong thing.\n"
        "Make it print: 3.140000",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    float pi = 3.14;\n"
        "    printf(\"%d\\n\", pi);\n"
        "}\n",
        "3.140000",
        "Keep float pi = 3.14; and print pi with the right specifier.",
        {"float pi = 3.14;", "%f", "\\n\", pi);", NULL},
        {"3.140000", NULL},
    },
    {
        "Store 25 in an int called age, then print age.\n"
        "The program should print: 25",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "}\n",
        "25",
        "Make an int called age that holds 25, and print age with printf. Don't type 25 into the printf.",
        {"int age = 25;", ", age);", NULL},
        {"\"25", NULL},
    },
    {
        "This program should print: 3.14159265358979\n"
        "But it prints 3.14159274101257, because pi can't hold that many digits.\n"
        "Fix it without changing the number or the printf.\n"
        "(%.14f just means: show 14 digits after the dot.)",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    float pi = 3.14159265358979;\n"
        "    printf(\"%.14f\\n\", pi);\n"
        "}\n",
        "3.14159265358979",
        "Keep pi = 3.14159265358979; and printf(\"%.14f\\n\", pi); and change the type of pi.",
        {"pi = 3.14159265358979;", "printf(\"%.14f\\n\", pi);", NULL},
        {"float", NULL},
    },
    {
        "letter holds 'A', and the program prints A.\n"
        "Without changing letter, make it print: 65",
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "    char letter = 'A';\n"
        "    printf(\"%c\\n\", letter);\n"
        "}\n",
        "65",
        "Keep char letter = 'A'; and print letter. Don't type 65 anywhere.",
        {"char letter = 'A';", "\\n\", letter);", NULL},
        {"65", NULL},
    },
};

#define STATE "/var/lib/pwnctual/level"
#define BUILT "/tmp/.variables"

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
    puts("Open it with:  nano variables.c");
    puts("Try it with:   make variables && ./variables");
    puts("Check it with: /challenge/run");
}

/* Reads variables.c as the learner, through a pipe */
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
        printf("Level %d: variables.c is missing.\n", level);
        return 1;
    }
    if (!builds()) {
        printf("\nLevel %d: variables.c still has a mistake.\n", level);
        return 1;
    }
    if (!prints(l->output)) {
        printf("Level %d: variables.c runs, but it doesn't print %s\n", level, l->output);
        return 1;
    }
    for (int i = 0; l->needs[i]; i++) {
        if (!strstr(src, l->needs[i])) {
            printf("Level %d: it prints %s, but not the way this level asks.\n%s\n", level, l->output, l->rule);
            return 1;
        }
    }
    for (int i = 0; l->forbid[i]; i++) {
        if (strstr(src, l->forbid[i])) {
            printf("Level %d: it prints %s, but not the way this level asks.\n%s\n", level, l->output, l->rule);
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
