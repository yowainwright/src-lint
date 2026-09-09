#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FIXTURE_FILES 10000
#define WARMUP_RUNS 3
#define TIMING_RUNS 31
#define PATH_CAPACITY 4096

extern char **environ;

static bool make_directory(const char *path) { return mkdir(path, 0777) == 0 || errno == EEXIST; }

static bool write_file(const char *path, const char *content) {
  FILE *file = fopen(path, "wb");
  if (!file) return false;
  const bool written = fputs(content, file) >= 0;
  return fclose(file) == 0 && written;
}

static bool join_path(char *path, const char *root, const char *suffix) {
  const int written = snprintf(path, PATH_CAPACITY, "%s/%s", root, suffix);
  return written >= 0 && written < PATH_CAPACITY;
}

static bool make_fixture_directories(const char *root) {
  char path[PATH_CAPACITY];
  if (!make_directory(root)) return false;
  if (!join_path(path, root, ".git") || !make_directory(path)) return false;
  if (!join_path(path, root, "packages") || !make_directory(path)) return false;
  if (!join_path(path, root, "services") || !make_directory(path)) return false;
  if (!join_path(path, root, "services/orders") || !make_directory(path)) return false;
  return true;
}

static bool create_bulk_file(const char *root, size_t index) {
  char path[PATH_CAPACITY];
  const int written = snprintf(path, sizeof(path), "%s/packages/file-%05zu.ts", root, index);
  if (written < 0 || written >= (int)sizeof(path)) return false;
  return write_file(path, "export {};\n");
}

static bool create_bulk_files(const char *root) {
  for (size_t index = 0; index < FIXTURE_FILES; index += 1) {
    if (!create_bulk_file(root, index)) return false;
  }
  return true;
}

static bool create_checked_files(const char *root) {
  char source[PATH_CAPACITY];
  char target[PATH_CAPACITY];
  if (!join_path(source, root, "services/orders/create.ts")) return false;
  if (!join_path(target, root, "services/orders/local.ts")) return false;
  return write_file(source, "import \"./local.ts\";\n") && write_file(target, "export {};\n");
}

static bool fixture_ready(const char *root) {
  char marker[PATH_CAPACITY];
  if (!join_path(marker, root, ".ready")) return false;
  struct stat information;
  if (stat(marker, &information) == 0) return true;
  if (!make_fixture_directories(root) || !create_bulk_files(root)) return false;
  return create_checked_files(root) && write_file(marker, "10000\n");
}

static int run_process(const char *cli, char *const arguments[]) {
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) return -1;
  int error = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
  if (error == 0) error = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
  pid_t child;
  if (error == 0) error = posix_spawn(&child, cli, &actions, NULL, arguments, environ);
  posix_spawn_file_actions_destroy(&actions);
  if (error != 0) return -1;
  int status;
  pid_t waited;
  do {
    waited = waitpid(child, &status, 0);
  } while (waited < 0 && errno == EINTR);
  if (waited < 0 || !WIFEXITED(status)) return -1;
  return WEXITSTATUS(status);
}

static double elapsed_ms(const struct timespec *start, const struct timespec *end) {
  const double seconds = (double)(end->tv_sec - start->tv_sec) * 1000.0;
  const double nanos = (double)(end->tv_nsec - start->tv_nsec) / 1000000.0;
  return seconds + nanos;
}

static double runtime_ms(const char *cli, char *const arguments[]) {
  struct timespec start;
  struct timespec end;
  if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) return -1.0;
  if (run_process(cli, arguments) != 0) return -1.0;
  if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) return -1.0;
  return elapsed_ms(&start, &end);
}

static int compare_runtime(const void *left, const void *right) {
  const double a = *(const double *)left;
  const double b = *(const double *)right;
  return (a > b) - (a < b);
}

static double median_runtime(const char *cli, char *const arguments[], const char *label) {
  for (size_t index = 0; index < WARMUP_RUNS; index += 1) {
    if (run_process(cli, arguments) != 0) return -1.0;
  }
  double samples[TIMING_RUNS];
  for (size_t index = 0; index < TIMING_RUNS; index += 1) {
    samples[index] = runtime_ms(cli, arguments);
    if (samples[index] < 0.0) return -1.0;
  }
  qsort(samples, TIMING_RUNS, sizeof(samples[0]), compare_runtime);
  printf("%s range %.3f–%.3f ms (%d runs)\n", label, samples[0], samples[TIMING_RUNS - 1],
         TIMING_RUNS);
  return samples[TIMING_RUNS / 2];
}

int main(int argc, char **argv) {
  if (argc != 3 || !fixture_ready(argv[2])) return 2;
  char source[PATH_CAPACITY];
  if (!join_path(source, argv[2], "services/orders/create.ts")) return 2;
  char *help[] = {argv[1], "--help", NULL};
  char *check[] = {argv[1], "check", source, "--format", "json", NULL};
  if (run_process(argv[1], check) != 0) return 2;
  const double startup = median_runtime(argv[1], help, "startup");
  const double warm_file = median_runtime(argv[1], check, "warm one-file");
  printf("median startup %.3f ms (budget 10 ms), warm one-file %.3f ms (budget 50 ms)\n", startup,
         warm_file);
  const bool within_budgets =
      startup >= 0.0 && startup <= 10.0 && warm_file >= 0.0 && warm_file <= 50.0;
  return within_budgets ? 0 : 1;
}
