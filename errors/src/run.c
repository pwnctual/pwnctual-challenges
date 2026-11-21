#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

/* Errors and debugging: reading what the compiler tells you. Level 1 is a question about a
   real error message. Every other level hands out broken.c with one problem, and each one
   practices a different thing from the lesson. broken.c must end up printing Hello, World!
   and compiling with no warnings at all. */
#define LEVELS 6
#define FILE_C "broken.c"

static const char *const task[LEVELS] = {
    NULL,
    "Lots of errors, but only one mistake. Read the first error, fix it, and the rest disappear.",
    "The compiler complains about lines that look perfect. The real mistake is a bit before them.",
    "It compiles and runs just fine, but the compiler warns you about something.\n"
    "Make it compile with no warnings at all.",
    "A new kind of error, from a different part of the compiler. Read it, then fix it.",
    "No errors, no warnings, but it prints Hello, instead of Hello, World!\n"
    "Find out why.",
};

static const char *const broken[LEVELS] = {
    NULL,
    "#include <stdio.h>\n"
    "\n"
    "int main() {\n"
    "    printf(Hello, World!\");\n"
    "    return 0;\n"
    "}\n",

    "#include <stdio.h>\n"
    "\n"
    "int main()\n"
    "    printf(\"Hello, \");\n"
    "    printf(\"World!\");\n"
    "    return 0;\n"
    "}\n",

    "int main() {\n"
    "    printf(\"Hello, World!\");\n"
    "    return 0;\n"
    "}\n",

    "#include <stdio.h>\n"
    "\n"
    "int main() {\n"
    "    Printf(\"Hello, World!\");\n"
    "    return 0;\n"
    "}\n",

    "#include <stdio.h>\n"
    "\n"
    "int main() {\n"
    "    printf(\"Hello, \");\n"
    "    // printf(\"World!\");\n"
    "    return 0;\n"
    "}\n",
};

#define STATE "/var/lib/pwnctual/level"
#define BUILT "/tmp/.broken"

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
        if (!f || fputs(broken[level - 1], f) == EOF || fclose(f)) {
            _exit(1);
        }
        _exit(0);
    }
    if (pid < 0 || !wait_ok(pid)) {
        exit(1);
    }
    printf("Level %d of %d: broken.c should print Hello, World!\n\n%s\n\n", level, LEVELS, task[level - 1]);
    puts("Compile it with: make broken");
    puts("Open it with:    nano broken.c");
    puts("Check it with:   /challenge/run");
}

/* Level 1: reading a real error message, piece by piece */
static int ask(const char *question, const char *answer)
{
    printf("%s ", question);
    char line[64];
    if (!fgets(line, sizeof line, stdin)) {
        return 0;
    }
    line[strcspn(line, "\r\n")] = '\0';
    char *p = line;
    while (*p == ' ') {
        p++;
    }
    size_t n = strlen(p);
    while (n && p[n - 1] == ' ') {
        p[--n] = '\0';
    }
    if (strcmp(p, answer) != 0) {
        printf("Not quite. Look at the error again: broken.c, then the line, then the column, then the message.\n");
        return 0;
    }
    return 1;
}

static int quiz(void)
{
    puts("Level 1 of 6: read the error\n");
    puts("You compile broken.c and the compiler says:\n");
    puts("    broken.c:4:28: error: expected ';' before 'return'\n");
    return ask("Which line is the problem on?", "4")
        && ask("How far into that line (which column)?", "28")
        && ask("What is missing? (type just the character)", ";");
}

/* Compiles broken.c as the learner. strict: warnings count as errors, and nothing is shown */
static int builds(int strict)
{
    pid_t pid = fork();
    if (pid == 0) {
        drop();
        if (chdir(home)) {
            _exit(1);
        }
        unlink(BUILT);
        if (strict) {
            int null = open("/dev/null", 1);
            if (null >= 0) {
                dup2(null, STDOUT_FILENO);
                dup2(null, STDERR_FILENO);
            }
            execlp("gcc", "gcc", "-Werror", FILE_C, "-o", BUILT, (char *)NULL);
        } else {
            execlp("gcc", "gcc", FILE_C, "-o", BUILT, (char *)NULL);
        }
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
        level = 1;
    }
    if (level > LEVELS) {
        print_flag();
        return 0;
    }

    if (level == 1) {
        if (!quiz()) {
            puts("Run /challenge/run to try again.");
            return 1;
        }
        puts("\nLevel 1 done!\n");
        save_level(2);
        hand_out(2);
        return 0;
    }

    if (!builds(0)) {
        printf("\nLevel %d: broken.c still has a mistake.\n", level);
        return 1;
    }
    if (!builds(1)) {
        printf("\nLevel %d: it compiles, but with warnings (see above). Make it compile with no warnings at all.\n", level);
        return 1;
    }
    if (!prints_hello()) {
        printf("Level %d: broken.c runs, but it doesn't print Hello, World!\n", level);
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
