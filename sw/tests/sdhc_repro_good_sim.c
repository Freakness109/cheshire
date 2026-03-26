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
static u_char scratch[SIZE * BLOCKS] = { 0 };
_Static_assert(sizeof(scratch) >= 512, "Scratch buffer needs to be atleast 512bytes");

int repro(void) {
    int seed = 0xdeadbeef;

    size_t size = 512;
    printf("Running repro with size %d and seed %x\n", size, seed);

    memset((void*) scratch, 0, 512);
    /* memset((void*) scratch + 512 - 8, 0xDF, 8); */ // hangs definetly
    /* memset(scratch + 256 + 128 + 64 + 32 + 16 + 8, 0xDF, 7); */
    /* memset(scratch + 256 + 128 + 64 + 32 + 16 + 8, 0xDF, 7); */
    /* scratch[511] = 0xdf; */
    /* scratch[510] = 0xdf; */
    /* scratch[509] = 0xdf; */
    /* scratch[508] = 0xdf; */
    /* scratch[507] = 0xdf; */
    /* scratch[506] = 0xdf; */
    /* scratch[505] = 0xdf; */
    /* scratch[504] = 0xdf; //makes it hang */
    /* scratch[504] = 0xd0; //makes it hang */
    /* scratch[504] = 0xc0; // does not make it hang */
    scratch[504] = 0x08; // does not make it hang

    // Reset Block
    ASSERT_OK(sdmmc_mem_write_block(&sc.sc_card, 0, scratch, size));
    printf("Done with write. Please look at the interrupts\n", size, seed);

    bzero((void*) scratch, size);

    ASSERT_OK(sdmmc_mem_read_block(&sc.sc_card, 0, scratch, size));

    int err = 0;
    for (size_t i = 0; i < size; ++i) {
        if (scratch[i] != 0xDF) {
            printf("scratch[%d] not as expected, should be zeroed, got %x\n", i, scratch[i]);
            err = 1;
        }
    }
    if (err) return 1;


    s_Seed = seed;
    for (size_t i = 0; i < size; ++i) scratch[i] = rand();

    uint64_t start_write = clint_get_mtime();
    ASSERT_OK(sdmmc_mem_write_block(&sc.sc_card, 0, scratch, size));
    uint64_t end_write = clint_get_mtime();

    memset((void*) scratch, 0xFF, size);

    uint64_t start_read = clint_get_mtime();
    ASSERT_OK(sdmmc_mem_read_block(&sc.sc_card, 0, scratch, size));
    uint64_t end_read = clint_get_mtime();
    printf("Write start at 0x%016x, end at 0x%016x, total 0x%016x\n", start_write, end_write, end_write - start_write);
    printf("Read  start at 0x%016x, end at 0x%016x, total 0x%016x\n", start_read, end_read, end_read - start_read);
    printf("Timebase is an RTC at %d Hz (expecting 1MHz)\n", *reg32(&__base_regs, CHESHIRE_RTC_FREQ_REG_OFFSET));

    s_Seed = seed;
    for (size_t i = 0; i < size; ++i) {
        char exp = rand();
        if (scratch[i] != exp) {
            printf("scratch[%d] not as expected, should be %x, got %x\n", i, exp, scratch[i]);
            err = 1;
        }
    }
    if (err) return 1;

    printf("Succesfuly ran read write test\n");

    return 0;
}

int main() {
    uint32_t rtc_freq = *reg32(&__base_regs, CHESHIRE_RTC_FREQ_REG_OFFSET);
    uint64_t reset_freq = clint_get_core_freq(rtc_freq, 2500);
    uart_init(&__base_uart, reset_freq, 1000000);

    printf("Hello world!\n");


#ifdef SDHC_DEBUG
    debug_funcs = 2;
    sdhcdebug = 1;
#endif


    ASSERT_OK(sdhc_init(&hp, SDHCI_BASE_ADDR, 0, 0));

#define WITH_SD_MODEL
//#define SDHC_INITIALIZED_MODEL

#ifdef WITH_SD_MODEL
    ASSERT_OK(sdhc_bus_width(&hp, 4));
#endif

#ifdef SDHC_INITIALIZED_MODEL
    sc.sc_caps = SMC_CAPS_4BIT_MODE | SMC_CAPS_AUTO_STOP | SMC_CAPS_NONREMOVABLE;
    sc.sc_flags = SMF_SD_MODE | SMF_MEM_MODE | SMF_CARD_PRESENT | SMF_CARD_ATTACHED;
    sc.sch = &hp;

    sc.sc_card.sc = &sc;
    sc.sc_card.rca = 1;
    sc.sc_card.csd.capacity = 20000000;
    sc.sc_card.csd.sector_size = SIZE;

#else
    printf("Before init\n");
    sdmmc_init(&sc, &hp, scratch);
    if (!ISSET(sc.sc_flags, SMF_CARD_ATTACHED)) {
        printf("Failed to initialize SD Card\n");
        uart_write_flush(&__base_uart);
        return 1;
    }
    printf("After init\n");
#endif

    printf("Before sdhc_bus_clock\n");
    ASSERT_OK(sdhc_bus_clock(sc.sch, SDMMC_SDCLK_50MHZ, SDMMC_TIMING_LEGACY));
    printf("After sdhc_bus_clock\n");
    uart_write_flush(&__base_uart);

#ifdef WITH_SD_MODEL
    ASSERT_OK(sdmmc_mem_set_blocklen(&sc, &sc.sc_card));
#endif

    // Single block RW
    ASSERT_OK(repro());

    printf("Success\n");
    uart_write_flush(&__base_uart);

    return 0xC007;
}
