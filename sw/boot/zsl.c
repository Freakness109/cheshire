// Copyright 2023 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Paul Scheffler <paulsc@iis.ee.ethz.ch>

// Zero-stage loader for Cheshire; loads a larger payload and DT into DRAM.

#include <stdint.h>
#include "util.h"
#include "params.h"
#include "regs/cheshire.h"
#include "dif/clint.h"
#include "gpt.h"
#include "dif/uart.h"
#include "printf.h"
#include "sdhcvar.h"
#include "sdmmcvar.h"

// Type for firmware payload
typedef int (*payload_t)(uint64_t, uint64_t, uint64_t);

// Static variables to wrap relocated reads
static gpt_read_t rread;
static void *rgp;

static uint8_t scratch_buffer_sd[512] = {0};

// Call foreign read procedure with another global pointer
static inline int grread(void *priv, void *buf, uint64_t addr, uint64_t len) {
    void *zsl_gp = gprw(rgp);
    uint64_t ret = rread(priv, buf, addr, len);
    gprw(zsl_gp);
    return ret;
}

static inline void load_part_or_spin(void *priv, const uint64_t *pguid, void *const dst,
                                     const char *name, uint64_t max_lbas, gpt_read_t read_func) {
    uint64_t lba_begin, lba_end;
    int64_t part_idx = -1;
    int ret = 0;
    if (gpt_find_partition(read_func, priv, &part_idx, &lba_begin, &lba_end, max_lbas, pguid, 0))
        printf("[ZSL] Error finding %s", name);
    else if (part_idx < 0)
        printf("[ZSL] No %s", name);
    else {
        printf("[ZSL] Copy %s (part %d, LBA %d-%d) to 0x%lx... ", name, part_idx, lba_begin,
               lba_end, dst);
        ret = read_func(priv, dst, 0x200 * lba_begin, 0x200 * (lba_end - lba_begin + 1));
        if (ret == 0) {
            printf("OK\r\n");
            return;
        }
    }
    // Catch
    printf(" with at most %d sectors and type GUID 0x%llx%llx", max_lbas, pguid[1], pguid[0]);
    while (1) wfi();
}

static inline int sd_grread(void *priv, void *buf, uint64_t addr, uint64_t len) {
    struct sdmmc_softc *sc = priv;
    size_t max_read_size = 0x040000;

    int ret = 0;
    // optimizer hint
    if (priv == buf)
        return 1;

    if (addr & 0x1FF) {
        ret = sdmmc_mem_read_block(&sc->sc_card, (addr / 0x200), scratch_buffer_sd, 512);
        if (ret) {
            printf("First read (0x%x bytes @ Block 0x%x), ret=%d\r\n", 512, (addr / 0x200), ret);
            return ret;
        }
        for (size_t i = addr & 0x1FF; i < 512 && len > 0; ++i) {
            *(uint8_t*)buf = scratch_buffer_sd[i];
            ++buf;
            --len;
        }
    }

    while (len >= max_read_size) {
        ret = sdmmc_mem_read_block(&sc->sc_card, ((addr+0x1FF) / 0x200), buf, max_read_size);
        if (ret) {
            printf("Loop read (0x%x bytes @ Block 0x%x), ret=%d\r\n", max_read_size, ((addr+0x1FF) / 0x200), ret);
            return ret;
        }
        len -= max_read_size;
        addr += max_read_size;
        buf += max_read_size;
    }

    if (len == 0)
        return ret;

    ret = sdmmc_mem_read_block(&sc->sc_card, ((addr+0x1FF) / 0x200), buf, len);
    if (ret)
        printf("Last read (0x%x bytes @ Block 0x%x), ret=%d\r\n", len, ((addr+0x1FF) / 0x200), ret);
    return ret;
}

int main(void) {
    // Get system parameters
    uint32_t bootmode = *reg32(&__base_regs, CHESHIRE_BOOT_MODE_REG_OFFSET);
    uint32_t rtc_freq = *reg32(&__base_regs, CHESHIRE_RTC_FREQ_REG_OFFSET);
    uint64_t core_freq = clint_get_core_freq(rtc_freq, 2500);
    rgp = (void *)(uintptr_t)*reg32(&__base_regs, CHESHIRE_SCRATCH_3_REG_OFFSET);
    uint32_t read = *reg32(&__base_regs, CHESHIRE_SCRATCH_0_REG_OFFSET);
    void *priv = (void *)(uintptr_t)*reg32(&__base_regs, CHESHIRE_SCRATCH_1_REG_OFFSET);

    // Initialize UART
    uart_init(&__base_uart, core_freq, __BOOT_BAUDRATE);

    // Print boot-critical cat, and also parameters
    printf(" /\\___/\\       Boot mode:       %d\r\n"
           "( o   o )      Real-time clock: %d Hz\r\n"
           "(  =^=  )      System clock:    %d Hz\r\n"
           "(        )     Read global ptr: 0x%08x\r\n"
           "(    P    )    Read pointer:    0x%08x\r\n"
           "(  U # L   )   Read argument:   0x%08x\r\n"
           "(    P      )\r\n"
           "(           ))))))))))\r\n\r\n",
           bootmode, rtc_freq, core_freq, rgp, read, priv);

    // If this is a GPT disk boot, load payload and device tree
    if (bootmode == 1) {
        struct sdmmc_softc sc = { 0 };
        struct sdhc_host hp = { 0 };
        sdhc_init(&hp, 0x01001000, 0, 0);
        sdmmc_init(&sc, &hp, scratch_buffer_sd);
        sdhc_bus_clock(sc.sch, SDMMC_SDCLK_25MHZ, SDMMC_TIMING_LEGACY);
        if (!ISSET(sc.sc_flags, SMF_CARD_ATTACHED)) {
            printf("Failed to initialize SD Card\n");
            uart_write_flush(&__base_uart);
            return 1;
        }
        priv = &sc;
        load_part_or_spin(priv, __BOOT_DTB_TYPE_GUID, __BOOT_ZSL_DTB, "device tree", 64, sd_grread);
        load_part_or_spin(priv, __BOOT_FW_TYPE_GUID, __BOOT_ZSL_FW, "firmware", 8192, sd_grread);
    } else if (read & 1) {
        rread = (gpt_read_t)(void *)(uintptr_t)(read & ~1);
        load_part_or_spin(priv, __BOOT_DTB_TYPE_GUID, __BOOT_ZSL_DTB, "device tree", 64, grread);
        load_part_or_spin(priv, __BOOT_FW_TYPE_GUID, __BOOT_ZSL_FW, "firmware", 8192, grread);
    }

    // Launch payload
    payload_t fw = __BOOT_ZSL_FW;
    printf("[ZSL] Launch firmware at %lx with device tree at %lx\r\n", fw, __BOOT_ZSL_DTB);
    fencei();
    return fw(0, (uintptr_t)__BOOT_ZSL_DTB, 0);
}

// On trap, report relevant CSRs and spin
void trap_vector() {
    uint64_t mcause, mepc, mip, mie, mstatus, mtval;
    asm volatile("csrr %0, mcause; csrr %1, mepc; csrr %2, mip;"
                 "csrr %3, mie; csrr %4, mstatus; csrr %5, mtval"
                 : "=r"(mcause), "=r"(mepc), "=r"(mip), "=r"(mie), "=r"(mstatus), "=r"(mtval));
    printf("\r\n==== [ZSL] trap encountered ====\r\n"
           " mcause:     0x%016x\r\n mepc:       0x%016x\r\n mip:        0x%016x\r\n"
           " mie:        0x%016x\r\n mstatus:    0x%016x\r\n mtval:      0x%016x\r\n"
           "================================\r\n",
           mcause, mepc, mip, mie, mstatus, mtval);
    while (1) wfi();
}
