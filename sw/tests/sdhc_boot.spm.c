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

static unsigned int s_Seed = 1;
unsigned int rand(void) {
    s_Seed = s_Seed * 1103515245 + 12345;
    return s_Seed;
}

#define SIZE     512
#define BLOCKS   512
// static u_char scratch[SIZE * BLOCKS] = { 0 };
static uint8_t *scratch = (uint8_t*)0x80000000;
// _Static_assert(sizeof(scratch) >= 512, "Scratch buffer needs to be atleast 512bytes");

int main() {
    sdhc_error_e rc = SDHC_SUCCESS;
    uint32_t rtc_freq = *reg32(&__base_regs, CHESHIRE_RTC_FREQ_REG_OFFSET);
    uint64_t reset_freq = clint_get_core_freq(rtc_freq, 2500);
    uart_init(&__base_uart, reset_freq, 1000000);

    printf("-------------------------------------------------------------------------------------------------------\r\n");
    printf("Hello world!\r\n");

    if ((rc = sdhc_init_library(&cfg, SDHCI_BASE_ADDR, false)) != SDHC_SUCCESS) {
        printf("Init library failed with RC %d\r\n", rc);
    }
    printf("Library init done\r\n");
    uart_write_flush(&__base_uart);
    if ((rc = sdhc_init_card(&cfg, SDHC_25MHZ)) != SDHC_SUCCESS) {
        printf("Init card failed with RC %d\r\n", rc);
    }
    printf("Card init done\r\n");

    if ((rc = sdhc_read(&cfg, 0, scratch, 1024)) != SDHC_SUCCESS) {
        return rc;
    }

    // jump to scratch
    void (*code)(void) = (void(*)())scratch;
    code();


    printf("Success\r\n");
    uart_write_flush(&__base_uart);

    return 0xC007;
}
