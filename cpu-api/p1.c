#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    printf("hello world (pid:%d)\n", (int) getpid());
    int rc = fork();
    if (rc < 0) {
        // fork fialed 
        fprintf(stderr, "for failed\n");
        exit(EXIT_FAILURE);
    } else if (rc == 0) {
        //child (new process)
        printf("hello, I am child (pid: %d)\n", (int) getpid());
    } else {
        // parent goes down this path (main)
        printf("hello, I am parent of %d (pid: %d)\n", rc, (int) getpid());
    }
    return EXIT_SUCCESS;
}