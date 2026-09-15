#include "src_lint/check.h"
#include "test.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_bytes(const char *path, const char *bytes, size_t length) {
  FILE *file = fopen(path, "wb");
  CHECK(file != NULL);
  CHECK(fwrite(bytes, 1, length, file) == length);
  CHECK(fclose(file) == 0);
}

static void write_text(const char *path, const char *text) {
  write_bytes(path, text, strlen(text));
}

static void expect_scan(SlCommand command, int expected, const char *rule) {
  char *output = NULL;
  char *errors = NULL;
  size_t output_size = 0;
  size_t error_size = 0;
  FILE *out = open_memstream(&output, &output_size);
  FILE *err = open_memstream(&errors, &error_size);
  CHECK(out != NULL && err != NULL);
  const SlRunOptions options = {".", command, SL_FORMAT_JSON, command == SL_COMMAND_CHECK};
  const int result = sl_run(&options, out, err);
  CHECK(fclose(out) == 0 && fclose(err) == 0);
  fprintf(stderr, "scan result %d: %s\n", result, errors);
  CHECK(result == expected);
  if (rule) CHECK(strstr(output, rule) != NULL);
  if (expected == 2) CHECK(error_size > 0);
  free(output);
  free(errors);
}

static void rejects_nul_bytes(void) {
  const char source[] = "// prefix\0\nimport './missing';";
  write_bytes("index.ts", source, sizeof(source) - 1);
  expect_scan(SL_COMMAND_CHECK, 2, NULL);
  expect_scan(SL_COMMAND_GRAPH, 2, NULL);
  write_text("index.ts", "import './missing';");
  const char config[] = "{\"strict\":false}\0{\"strict\":true}";
  write_bytes(".src-lintrc.json", config, sizeof(config) - 1);
  expect_scan(SL_COMMAND_CHECK, 2, NULL);
  expect_scan(SL_COMMAND_DISCOVER, 2, NULL);
  CHECK(unlink(".src-lintrc.json") == 0);
}

static void rejects_unusable_config(void) {
  CHECK(symlink("missing.json", ".src-lintrc.json") == 0);
  expect_scan(SL_COMMAND_CHECK, 2, NULL);
  CHECK(unlink(".src-lintrc.json") == 0);
  CHECK(mkdir(".src-lintrc.json", 0700) == 0);
  expect_scan(SL_COMMAND_CHECK, 2, NULL);
  CHECK(rmdir(".src-lintrc.json") == 0);
  CHECK(mkfifo(".src-lintrc.json", 0600) == 0);
  expect_scan(SL_COMMAND_CHECK, 2, NULL);
  CHECK(unlink(".src-lintrc.json") == 0);
  expect_scan(SL_COMMAND_CHECK, 1, "SL2001");
}

static void rejects_oversized_import(void) {
  char source[12000];
  memset(source, 'a', sizeof(source));
  memcpy(source, "import './", 10);
  memcpy(source + sizeof(source) - 2, "';", 2);
  write_bytes("index.ts", source, sizeof(source));
  expect_scan(SL_COMMAND_CHECK, 2, NULL);
  expect_scan(SL_COMMAND_GRAPH, 2, NULL);
  CHECK(unlink("index.ts") == 0);
}

static void prepare_cache_fixture(void) {
  CHECK(mkdir("services", 0700) == 0);
  CHECK(mkdir("services/orders", 0700) == 0);
  CHECK(mkdir("services/billing", 0700) == 0);
  CHECK(mkdir("services/billing/internal", 0700) == 0);
  write_text("services/orders/create.ts", "import '../billing/internal/ledger';");
  write_text("services/billing/internal/ledger.ts", "export const value = 1;");
  write_text(".src-lintrc.toml", "[cache]\nmax_mib = 1\n");
}

static void corrupt_records(const char *content, bool append) {
  DIR *directory = opendir(".src-lint/cache");
  CHECK(directory != NULL);
  struct dirent *entry;
  size_t count = 0;
  while ((entry = readdir(directory)) != NULL) {
    if (!strstr(entry->d_name, ".slc")) continue;
    char path[512];
    CHECK(snprintf(path, sizeof(path), ".src-lint/cache/%s", entry->d_name) < (int)sizeof(path));
    FILE *file = fopen(path, append ? "ab" : "wb");
    CHECK(file != NULL && fputs(content, file) >= 0);
    CHECK(fclose(file) == 0);
    count += 1;
  }
  CHECK(closedir(directory) == 0 && count > 0);
}

static void corrupt_cache_falls_back_to_source(void) {
  prepare_cache_fixture();
  expect_scan(SL_COMMAND_CHECK, 1, "SL1001");
  expect_scan(SL_COMMAND_CHECK, 1, "SL1001");
  corrupt_records("TLC1\n0\n", false);
  expect_scan(SL_COMMAND_CHECK, 1, "SL1001");
  corrupt_records("TLC2\n0\n0000000000000000\n", false);
  expect_scan(SL_COMMAND_CHECK, 1, "SL1001");
  corrupt_records("TLC2\n184467440737095516160\n", false);
  expect_scan(SL_COMMAND_CHECK, 1, "SL1001");
  corrupt_records("trailing garbage", true);
  expect_scan(SL_COMMAND_CHECK, 1, "SL1001");
}

static void cache_control_fifos_do_not_block(void) {
  const char *names[] = {".src-lint/cache/.size", ".src-lint/cache/.lock",
                         ".src-lint/cache/.dirty"};
  for (size_t index = 0; index < COUNT(names); index += 1) {
    (void)unlink(names[index]);
    CHECK(mkfifo(names[index], 0600) == 0);
    expect_scan(SL_COMMAND_CHECK, 1, "SL1001");
    (void)unlink(names[index]);
  }
}

int main(void) {
  char root[] = "resilience-XXXXXX";
  CHECK(mkdtemp(root) != NULL && chdir(root) == 0);
  CHECK(mkdir(".git", 0700) == 0);
  rejects_nul_bytes();
  rejects_unusable_config();
  rejects_oversized_import();
  corrupt_cache_falls_back_to_source();
  cache_control_fifos_do_not_block();
  return 0;
}
