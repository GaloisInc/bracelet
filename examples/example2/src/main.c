#include "handlers.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef IS_BRACELET_BUILD
#include "snapshot.h"
#endif

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
#ifdef IS_BRACELET_BUILD
    bracelet_snapshot();
#endif

    /* Read all of stdin into a buffer */
    size_t cap = 4096, len = 0;
    unsigned char *buf = malloc(cap);
    if (!buf) { fprintf(stderr, "malloc failed\n"); return 1; }
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf + len, cap - len)) != 0) {
        if (n < 0) {
            perror("read");
            free(buf);
            return 1;
        }
        len += n;
        if (len == cap) {
            cap *= 2;
            unsigned char *new_buf = realloc(buf, cap);
            if (!new_buf) {
                fprintf(stderr, "realloc failed\n");
                free(buf);
                return 1;
            }
            buf = new_buf;
        }
    }

    dispatch_buffer(buf, len);
    free(buf);
    return 0;
}
