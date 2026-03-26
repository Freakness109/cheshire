// Copyright (c) 2024 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0/
//
// Authors:
// - Philippe Sauter <phsauter@iis.ee.ethz.ch>

#include "dif/clint.h"
#include "dif/uart.h"
#include "printf.h"
#include "util.h"
#include <string.h>

#include "regs/cheshire.h"
#include "params.h"
#include <sdhc.h>

#define SDHCI_BASE_ADDR (void*)0x01001000

struct sdhc_cfg cfg = {0};

static unsigned int s_Seed = 1;
unsigned int rand(void) {
    s_Seed = s_Seed * 1103515245 + 12345;
    return s_Seed;
}

#define SIZE     512
#define BLOCKS   5
static uint8_t scratch[SIZE * BLOCKS] = { 0 };
_Static_assert(sizeof(scratch) >= 512, "Scratch buffer needs to be atleast 512bytes");

int test_rw(size_t size, unsigned int seed) {
    sdhc_error_e rc = SDHC_SUCCESS;
    printf("Running read write test with size %d and seed %x\r\n", size, seed);

    bzero((void*) scratch, size);

    // Reset Block
    if ((rc = sdhc_write(&cfg, 0, scratch, size)) != SDHC_SUCCESS) {
        printf("First sdhc_write failed with RC %d\r\n", rc);
        return rc;
    }

    memset((void*) scratch, 0xFF, size);

    if ((rc = sdhc_read(&cfg, 0, scratch, size)) != SDHC_SUCCESS) {
        printf("First sdhc_read failed with RC %d\r\n", rc);
        return rc;
    }

    int err = 0;
    for (size_t i = 0; i < size; ++i) {
        if (scratch[i] != 0) {
            printf("scratch[%d] not as expected, should be zeroed, got %x\r\n", i, scratch[i]);
            err = 1;
        }
    }
    if (err) return 1;


    s_Seed = seed;
    for (size_t i = 0; i < size; ++i) scratch[i] = rand();

    uint64_t start_write = clint_get_mtime();
    if ((rc = sdhc_write(&cfg, 0, scratch, size)) != SDHC_SUCCESS) {
        printf("Second sdhc_write failed with RC %d\r\n", rc);
        return rc;
    }
    uint64_t end_write = clint_get_mtime();

    memset((void*) scratch, 0xFF, size);

    uint64_t start_read = clint_get_mtime();
    if ((rc = sdhc_read(&cfg, 0, scratch, size)) != SDHC_SUCCESS) {
        printf("Second sdhc_read failed with RC %d\r\n", rc);
        return rc;
    }
    uint64_t end_read = clint_get_mtime();
    printf("Write start at 0x%016x, end at 0x%016x, total 0x%016x\r\n", start_write, end_write, end_write - start_write);
    printf("Read  start at 0x%016x, end at 0x%016x, total 0x%016x\r\n", start_read, end_read, end_read - start_read);
    printf("Timebase is an RTC at %d Hz (expecting 1MHz)\r\n", *reg32(&__base_regs, CHESHIRE_RTC_FREQ_REG_OFFSET));


    s_Seed = seed;
    for (size_t i = 0; i < size; ++i) {
        char exp = rand();
        if (scratch[i] != exp) {
            printf("scratch[%d] not as expected, should be %x, got %x\r\n", i, exp, scratch[i]);
            err = 1;
        }
    }
    if (err) return 1;

    printf("Succesfuly ran read write test\r\n");

    return 0;
}

int dummy_printf(const char* fmt, ...){
    (void)fmt;
    return 0;
}

int main() {
    uint32_t rtc_freq = *reg32(&__base_regs, CHESHIRE_RTC_FREQ_REG_OFFSET);
    uint64_t reset_freq = clint_get_core_freq(rtc_freq, 2500);
    uart_init(&__base_uart, reset_freq, 1000000);

    printf("Hello world!\r\n");
    uart_write_flush(&__base_uart);

    sdhc_error_e rc = SDHC_SUCCESS;
    cfg.print = printf;
    if ((rc = sdhc_init_library(&cfg, SDHCI_BASE_ADDR, true)) != SDHC_SUCCESS) {
        printf("Init library failed with RC %d\r\n", rc);
    }
    printf("Library init done\r\n");
    uart_write_flush(&__base_uart);
    if ((rc = sdhc_init_card(&cfg, SDHC_25MHZ)) != SDHC_SUCCESS) {
        printf("Init card failed with RC %d\r\n", rc);
    }
    printf("Card init done\r\n");

    // Single block RW
    /* if (test_rw(SIZE, 0xDEADBEEF) != 0) { */
    /*     printf("Error during single block read/write test\r\n"); */
    /* } */

    // Multiple block RW
    if (test_rw(BLOCKS*SIZE, 0x70EDADA1)) {
        printf("Error during multi-block read/write test\r\n");
    }

    printf("Success\r\n");
    uart_write_flush(&__base_uart);

    return 0xC007;
}
