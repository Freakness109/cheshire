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

#include "sdmmcvar.h"
#include "sdhcvar.h"

#include "regs/cheshire.h"
#include "params.h"

#define SDHCI_BASE_ADDR 0x01001000

struct sdmmc_softc sc = { 0 };
struct sdhc_host hp = { 0 };

static unsigned int s_Seed = 1;
unsigned int rand(void) {
    s_Seed = s_Seed * 1103515245 + 12345;
    return s_Seed;
}

#define SIZE     512
#define BLOCKS   64
// static u_char scratch[SIZE * BLOCKS] = { 0 };
// _Static_assert(sizeof(scratch) >= 512, "Scratch buffer needs to be atleast 512bytes");
u_char *scratch = 0x80000000;

int bench_read(size_t size, unsigned int seed, size_t max_iters) {

    bzero((void*) scratch, size);

    uint64_t start_read = clint_get_mtime();
    for (size_t iters = 0; iters < max_iters; ++iters) {
        ASSERT_OK(sdmmc_mem_read_block(&sc.sc_card, 0, scratch, size));
    }
    uint64_t end_read = clint_get_mtime();

    printf("Read  start at 0x%016x, end at 0x%016x, total 0x%016x\n", start_read, end_read, end_read - start_read);

    return 0;
}
int bench_write(size_t size, unsigned int seed, size_t max_iters) {

    bzero((void*) scratch, size);
    uint64_t start_write = clint_get_mtime();
    for (size_t iters = 0; iters < max_iters; ++iters) {
        ASSERT_OK(sdmmc_mem_write_block(&sc.sc_card, 0, scratch, size));
    }
    uint64_t end_write = clint_get_mtime();

    printf("Write start at 0x%016x, end at 0x%016x, total 0x%016x\n", start_write, end_write, end_write - start_write);

    return 0;
}
int __attribute__((noinline)) bench(size_t size, size_t max_iters) {
    printf("Running benchmark with size %d and %d iters\n", size, max_iters);
    printf("Timebase is an RTC at %d Hz (expecting 1MHz)\n", *reg32(&__base_regs, CHESHIRE_RTC_FREQ_REG_OFFSET));
    unsigned int seed = 0xdeadbeef;
    ASSERT_OK(bench_write(size, seed, max_iters));
    ASSERT_OK(bench_read(size, seed, max_iters));
    return 0;
}


int main() {
    uint32_t rtc_freq = *reg32(&__base_regs, CHESHIRE_RTC_FREQ_REG_OFFSET);
    uint64_t reset_freq = clint_get_core_freq(rtc_freq, 2500);
    uart_init(&__base_uart, reset_freq, 1000000);

    printf("------------------------------------------------------------------------------------------\n");
    printf("Hello world!\r\n");

    ASSERT_OK(sdhc_init(&hp, SDHCI_BASE_ADDR, 0, 0));

    sdmmc_init(&sc, &hp, scratch);
    if (!ISSET(sc.sc_flags, SMF_CARD_ATTACHED)) {
        printf("Failed to initialize SD Card\r\n");
        uart_write_flush(&__base_uart);
        return 1;
    }
    ASSERT_OK(sdhc_bus_clock(sc.sch, SDMMC_SDCLK_25MHZ, SDMMC_TIMING_LEGACY));
    // The SD model is not smart enough to support switching from 1 to 4, it always does 4
    /* ASSERT_OK(sdhc_bus_width(&hp, 4)); */
    ASSERT_OK(sdmmc_mem_set_blocklen(&sc, &sc.sc_card));

    size_t blocks[] = {
        1,
        /* 2, */
        /* 4, */
        /* 8, */
        /* 16, */
        /* 32, */
        /* 64 */
    };

    for (size_t i = 0; i < (sizeof(blocks)/sizeof(blocks[0])); ++i) {
        ASSERT_OK(bench(SIZE * blocks[i], 10));
        clint_spin_ticks(10);
    }

    printf("Success\n");
    uart_write_flush(&__base_uart);

    return 0xC007;
}
