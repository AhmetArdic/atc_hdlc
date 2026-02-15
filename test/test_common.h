#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "../inc/hdlc.h" // For atc_hdlc_u8

// --- ANSI Colors ---
#define COL_RESET "\033[0m"
#define COL_GREEN "\033[32m"
#define COL_RED "\033[31m"
#define COL_CYAN "\033[36m"
#define COL_YELLOW "\033[33m"

// --- Logging & Assertions ---
void print_hexdump(const char *label, const atc_hdlc_u8 *data, int len);
void test_pass(const char *test_name);
void test_fail(const char *test_name, const char *reason);

#endif // TEST_COMMON_H
