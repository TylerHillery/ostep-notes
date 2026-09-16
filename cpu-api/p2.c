#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>


int main(int argc, char *argv[]) {
    printf("hello world from (pid:%d)\n", (int) getpid());
    int rc = fork();
    if (rc < 0) { 
        // fork failed 
        fprintf(stderr, "fork failed\n");
        exit(EXIT_FAILURE);
    } else if (rc == 0) {
        // child
        printf("hello, I am child (pid:%d)\n", (int) getpid());
    } else {
        // forces to pause until child termination
        // then returns the pid of the child which should be the same as rc in this case
        int rc_wait = wait(NULL);
        printf("hello, I am parent of %d (rc_wait:%d) (pid:%d)\n", rc, rc_wait, (int) getpid());
    }
    return EXIT_SUCCESS;
}