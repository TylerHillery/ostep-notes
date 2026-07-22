#define _GNU_SOURCE

/*
 * One thing you’ll have to take into account is the precision and accuracy of
 * your timer. A typical timer that you can use is gettimeofday();  read the man
 * page for details. What you’ll see there is that gettimeofday()  returns the
 * time in microseconds since 1970; however, this does not mean  that the timer
 * is precise to the microsecond. Measure back-to-back calls  to gettimeofday()
 * to learn something about how precise the timer really is; this will tell you
 * how many iterations of your null system-call  test you’ll have to run in
 * order to get a good measurement result. If  gettimeofday() is not precise
 * enough for you, you might look into  using the rdtsc instruction available on
 * x86 machines.
 *
 * Measuring the cost of a context switch is a little trickier.
 * The lmbench  benchmark does so by running two processes on a single CPU, and
 * setting up two UNIX pipes between them; a pipe is just one of many ways
 * processes in a UNIX system can communicate with one another. The first
 * process then issues a write to the first pipe, and waits for a read on the
 * second; upon seeing the first process waiting for something to read from  the
 * second pipe, the OS puts the first process in the blocked state, and switches
 * to the other process, which reads from the first pipe and then  writes to the
 * second. When the second process tries to read from the first  pipe again, it
 * blocks, and thus the back-and-forth cycle of communication  continues. By
 * measuring the cost of communicating like this repeatedly,  lmbench can make a
 * good estimate of the cost of a context switch. You  can try to re-create
 * something similar here, using pipes, or perhaps some  other communication
 * mechanism such as UNIX sockets.
 *
 * One difficulty in measuring context-switch cost arises in systems with  more
 * than one CPU; what you need to do on such a system is ensure that  your
 * context-switching processes are located on the same processor. Fortunately,
 * most operating systems have calls to bind a process to a particular
 * processor; on Linux, for example, the sched setaffinity() call  is what
 * you’re looking for. By ensuring both processes are on the same  processor,
 * you are making sure to measure the cost
 */

#include <bits/time.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CONTEXT_SWITCH_ITERATIONS 100000

struct timer {
  struct timespec *start;
  struct timespec *end;
};

struct timer_rdtsc {
  uint64_t start;
  uint64_t end;
};

void timer_start(struct timer *timer) {
  clock_gettime(CLOCK_MONOTONIC, timer->start);
}

void timer_end(struct timer *timer) {
  clock_gettime(CLOCK_MONOTONIC, timer->end);
}

int64_t timer_elapsed_nanos(const struct timer *timer) {
  int64_t seconds = (int64_t)timer->end->tv_sec - (int64_t)timer->start->tv_sec;

  int64_t nanoseconds =
      (int64_t)timer->end->tv_nsec - (int64_t)timer->start->tv_nsec;

  return seconds * INT64_C(1000000000) + nanoseconds;
}

#if defined(__x86_64__) || defined(__i386__)
static uint64_t rdtsc(void) {
  uint32_t low, high;

  __asm__ volatile("lfence\n\t"
                   "rdtsc"
                   : "=a"(low), "=d"(high)
                   :
                   : "memory");

  return ((uint64_t)high << 32) | low;
}

void timer_rdtsc_start(struct timer_rdtsc *timer) { timer->start = rdtsc(); }

void timer_rdtsc_end(struct timer_rdtsc *timer) { timer->end = rdtsc(); }

uint64_t timer_rdtsc_elapsed_cycles(const struct timer_rdtsc *timer) {
  return timer->end - timer->start;
}
#endif

static void pin_to_cpu(int cpu) {
  cpu_set_t set;

  CPU_ZERO(&set);
  CPU_SET(cpu, &set);

  if (sched_setaffinity(0, sizeof(set), &set) == -1) {
    perror("sched_setaffinity");
    exit(EXIT_FAILURE);
  }
}

static void write_byte(int fd, char byte) {
  ssize_t n;

  do {
    n = write(fd, &byte, 1);
  } while (n == -1 && errno == EINTR);

  if (n != 1) {
    perror("write");
    exit(EXIT_FAILURE);
  }
}

static void read_byte(int fd, char *byte) {
  ssize_t n;

  do {
    n = read(fd, byte, 1);
  } while (n == -1 && errno == EINTR);

  if (n != 1) {
    perror("read");
    exit(EXIT_FAILURE);
  }
}

void piping(void) {
  int parent_to_child[2];
  int child_to_parent[2];

  if (pipe(parent_to_child) == -1) {
    perror("pipe");
    exit(EXIT_FAILURE);
  }

  if (pipe(child_to_parent) == -1) {
    perror("pipe");
    exit(EXIT_FAILURE);
  }

  int cpu = sched_getcpu();
  if (cpu == -1) {
    perror("sched_getcpu");
    exit(EXIT_FAILURE);
  }

  fflush(NULL);

  pid_t pid = fork();
  switch (pid) {
  case -1: {
    perror("fork");
    exit(EXIT_FAILURE);
  }
  case 0: {
    char byte;

    pin_to_cpu(cpu);
    close(parent_to_child[1]);
    close(child_to_parent[0]);

    for (int i = 0; i < CONTEXT_SWITCH_ITERATIONS; i++) {
      read_byte(parent_to_child[0], &byte);
      write_byte(child_to_parent[1], byte);
    }

    close(parent_to_child[0]);
    close(child_to_parent[1]);
    exit(EXIT_SUCCESS);
  }
  default: {
    char byte = 'x';
    struct timespec start, end;
    struct timer timer = (struct timer){.start = &start, .end = &end};

    pin_to_cpu(cpu);
    close(parent_to_child[0]);
    close(child_to_parent[1]);

    timer_start(&timer);
    for (int i = 0; i < CONTEXT_SWITCH_ITERATIONS; i++) {
      write_byte(parent_to_child[1], byte);
      read_byte(child_to_parent[0], &byte);
    }
    timer_end(&timer);

    int64_t elapsed_nanos = timer_elapsed_nanos(&timer);
    double round_trip_nanos = (double)elapsed_nanos / CONTEXT_SWITCH_ITERATIONS;
    double context_switch_nanos = round_trip_nanos / 2.0;

    printf("Context switch pipe ping-pong iterations: %d\n",
           CONTEXT_SWITCH_ITERATIONS);
    printf("Total pipe ping-pong elapsed nanos: %ld\n", elapsed_nanos);
    printf("Average round-trip nanos: %.2f\n", round_trip_nanos);
    printf("Approx nanos per context switch: %.2f\n", context_switch_nanos);

    close(parent_to_child[1]);
    close(child_to_parent[0]);

    if (waitpid(pid, NULL, 0) == -1) {
      perror("waitpid");
      exit(EXIT_FAILURE);
    }
    break;
  }
  }
}

int main(void) {
  struct timespec start, end;
  int64_t elapsed_nanos;
  struct timer timer = (struct timer){.start = &start, .end = &end};
#if defined(__x86_64__) || defined(__i386__)
  struct timer_rdtsc rdtsc_timer;
#endif

  /*
   * Check the elasped time when called clock_getime() back to back to see
   * accuracy of our timer
   */
  timer_start(&timer);
  timer_end(&timer);
  elapsed_nanos = timer_elapsed_nanos(&timer);
  printf("Nanos elapsed from back to back calls of clock_gettime: %ld\n",
         elapsed_nanos);

#if defined(__x86_64__) || defined(__i386__)
  timer_rdtsc_start(&rdtsc_timer);
  timer_rdtsc_end(&rdtsc_timer);
  printf("Cycles elapsed from back to back calls of rdtsc: %" PRIu64 "\n",
         timer_rdtsc_elapsed_cycles(&rdtsc_timer));
#else
  printf("rdtsc is only available on x86/x86_64\n");
#endif

  /*
   * See how long it take to do a syscall, zero byte read
   */
  int fd = open("/dev/null", O_RDONLY);
  if (fd == -1) {
    perror("open");
    return 1;
  }
  char buffer;

  timer_start(&timer);
  read(fd, &buffer, 0);
  timer_end(&timer);
  elapsed_nanos = timer_elapsed_nanos(&timer);
  printf("Nanos elapsed from zero byte read call: %ld\n", elapsed_nanos);
  close(fd);

  /*
   * Sets up two pipes and forks. One pipe is from child_to_parent and other is
   * parent to child. They both takes turns writing back and fourth to each other
   * and we use `sched_setaffinity` call to ensure the child and parent execute
   * on the same processor since I am running this ona mutli-core CPU.
   *
   * Another note is `read()` inheritly waits and "gives up" control for the other
   * process to write so they can ping pong back and fourth to each other. 
   * 
   * This allows us to try to measure how long a context switch is taking.
   */
  piping();
}
