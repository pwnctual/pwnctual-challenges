#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned random_below(unsigned n)
{
    unsigned v = 0;
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(&v, sizeof v, 1, f) != 1) {
        exit(1);
    }
    fclose(f);
    return v % n;
}

int main(void)
{
    const int widths[] = {4, 6, 8};

    for (int i = 0; i < 3; i++) {
        int w = widths[i];
        unsigned n = (1u << (w - 1)) + random_below(1u << (w - 1));

        printf("%u = ", n);
        fflush(stdout);

        char line[64];
        if (!fgets(line, sizeof line, stdin)) {
            return 1;
        }
        /* The answer is the number in binary: only 0s and 1s */
        char *end;
        long answer = strtol(line, &end, 2);
        while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') {
            end++;
        }
        if (end == line || *end != '\0' || line[0] == '-' || line[0] == '+' || answer != (long)n) {
            puts("Wrong.");
            return 1;
        }
    }

    FILE *flag = fopen("/flag", "r");
    if (!flag) {
        return 1;
    }
    char buf[128];
    if (fgets(buf, sizeof buf, flag)) {
        buf[strcspn(buf, "\n")] = '\0';
        puts(buf);
    }
    fclose(flag);
    return 0;
}
