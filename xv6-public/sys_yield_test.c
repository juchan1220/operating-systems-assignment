#include "types.h"
#include "stat.h"
#include "user.h"

void parent (void) {
    for (int i = 0; i < 50; i++) {
        printf(1, "Parent\n");
        yield();
    }
}

void child (void) {
    for (int i = 0; i < 50; i++) {
        printf(1, "Child\n");
        yield();
    }
}

int main (int argc, char *argv[]) {

    int ret = fork();

    if (ret == 0) {
        child();
    } else if (ret == -1) {
        printf(1, "fork failed...\n");
    } else {
        printf(1, "fork success. (child pid = %d)\n", ret);
        parent();
        wait();
    }

    exit();
}