// Copyright (c) 2024 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0/
//
// Authors:
// - Philippe Sauter <phsauter@iis.ee.ethz.ch>
// - Axel Vanoni <axvanoni@iis.ee.ethz.ch>

#include "dif/clint.h"
#include "dif/uart.h"
#include "printf.h"
#include "util.h"

#include "regs/cheshire.h"
#include "params.h"
#include <sdhc.h>
#include <string.h>

#define SDHCI_BASE_ADDR (void*)0x01001000

struct sdhc_cfg cfg = {0};

#define SIZE     512
#define BLOCKS   512
// DRAM
static uint8_t *scratch = (uint8_t*)0x80000000;

sdhc_error_e bench_read(size_t size, size_t max_iters) {
    sdhc_error_e rc = SDHC_SUCCESS;

    bzero((void*) scratch, size);

    uint64_t start_read = clint_get_mtime();
    for (size_t iters = 0; iters < max_iters; ++iters) {
        if ((rc = sdhc_read(&cfg, 0, scratch, size)) != SDHC_SUCCESS) {
            return rc;
        }
    }
    uint64_t end_read = clint_get_mtime();

    printf("Read  start at 0x%016llx, end at 0x%016llx, total 0x%016llx\r\n", start_read, end_read, end_read - start_read);

    return rc;
}
sdhc_error_e bench_write(size_t size, size_t max_iters) {
    sdhc_error_e rc = SDHC_SUCCESS;

    bzero((void*) scratch, size);

    uint64_t start_write = clint_get_mtime();

    for (size_t iters = 0; iters < max_iters; ++iters) {
        if ((rc = sdhc_write(&cfg, 0, scratch, size)) != SDHC_SUCCESS) {
            return rc;
        }
    }
    uint64_t end_write = clint_get_mtime();

    printf("Write start at 0x%016x, end at 0x%016x, total 0x%016x\r\n", start_write, end_write, end_write - start_write);

    return 0;
}
sdhc_error_e __attribute__((noinline)) bench(size_t size, size_t max_iters) {
    printf("Running benchmark with size %d and %d iters\r\n", size, max_iters);
    printf("Timebase is an RTC at %d Hz (expecting 1MHz)\r\n", *reg32(&__base_regs, CHESHIRE_RTC_FREQ_REG_OFFSET));
    sdhc_error_e rc = SDHC_SUCCESS;
    /* if ((rc = bench_write(size, max_iters)) != SDHC_SUCCESS) { */
    /*     printf("Write failed with rc %d\r\n", rc); */
    /*     /1* return rc; *1/ */
    /* } */
    if ((rc = bench_read(size, max_iters)) != SDHC_SUCCESS) {
        printf("Read failed with rc %d\r\n", rc);
        /* return rc; */
    }
    return SDHC_SUCCESS;
}


int main() {
    sdhc_error_e rc = SDHC_SUCCESS;
    uint32_t rtc_freq = *reg32(&__base_regs, CHESHIRE_RTC_FREQ_REG_OFFSET);
    uint64_t reset_freq = clint_get_core_freq(rtc_freq, 2500);
    uart_init(&__base_uart, reset_freq, 1000000);

    printf("-------------------------------------------------------------------------------------------------------\r\n");
    printf("Hello world!\r\n");

    /* cfg.print = NULL; */
    cfg.print = printf;
    if ((rc = sdhc_init_library(&cfg, SDHCI_BASE_ADDR, clint_spin_ticks, false)) != SDHC_SUCCESS) {
        printf("Init library failed with RC %d\r\n", rc);
    }
    printf("Library init done\r\n");
    uart_write_flush(&__base_uart);
    if ((rc = sdhc_init_card(&cfg, SDHC_25MHZ)) != SDHC_SUCCESS) {
        printf("Init card failed with RC %d\r\n", rc);
    }
    printf("Card init done\r\n");

    size_t blocks[] = {
        1,
        /* 2, */
        /* 4, */
        /* 8, */
        /* 16, */
        /* 32, */
        /* 64, */
        /* 128, */
        /* 256, */
        /* 512 */
    };

    for (size_t i = 0; i < (sizeof(blocks)/sizeof(blocks[0])); ++i) {
        if ((rc = bench(SIZE * blocks[i], 10)) != SDHC_SUCCESS) {
            printf("Failed with %d blocks, RC %d\r\n", blocks[i], rc);
        }
        clint_spin_ticks(10);
    }

    printf("Success\r\n");
    uart_write_flush(&__base_uart);

    return 0xC007;
}
