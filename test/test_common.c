#include "test_common.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void print_hexdump(const char *label, const atc_hdlc_u8 *data, int len) {
  printf("%s%s (%d bytes):%s ", COL_CYAN, label, len, COL_RESET);
  for (int i = 0; i < len; i++) {
    printf("%02X ", data[i]);
  }
  printf(" | ");
  for (int i = 0; i < len; i++) {
    printf("%c", isprint(data[i]) ? data[i] : '.');
  }
  printf("\n");
}

void test_pass(const char *test_name) {
  printf("%s[PASS] %s%s\n\n", COL_GREEN, test_name, COL_RESET);
}

void test_fail(const char *test_name, const char *reason) {
  printf("%s[FAIL] %s: %s%s\n", COL_RED, test_name, reason, COL_RESET);
  exit(1);
}
