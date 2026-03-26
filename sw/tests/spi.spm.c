
#include <stdint.h>
#include "util.h"
#include "params.h"
#include "regs/cheshire.h"
#include "spi_host_regs.h"
#include "dif/clint.h"
#include "dif/uart.h"
#include "hal/spi_sdcard.h"
#include "printf.h"
#include <string.h>

#define SIZE     512

static uint8_t *scratch = (uint8_t*)0x80000000;

int bench_read(size_t size, spi_sdcard_t *device, size_t max_iters) {
    int rc = 0;

    bzero((void*) scratch, size);

    uint64_t start_read = clint_get_mtime();
    for (size_t iters = 0; iters < max_iters; ++iters) {
        rc |= spi_sdcard_read_checkcrc(device, scratch, 0, size);
    }
    uint64_t end_read = clint_get_mtime();

    printf("Read  start at 0x%016x, end at 0x%016x, total 0x%016x\r\n", start_read, end_read, end_read - start_read);

    return rc;
}

int bench_write(size_t size, spi_sdcard_t *device, size_t max_iters) {
    int rc = 0;

    bzero((void*) scratch, size);
    uint64_t start_write = clint_get_mtime();
    for (size_t iters = 0; iters < max_iters; ++iters) {
        rc |= spi_sdcard_write_blocks(device, scratch, 2, size / 512, 1);
    }
    uint64_t end_write = clint_get_mtime();

    printf("Write start at 0x%016x, end at 0x%016x, total 0x%016x\r\n", start_write, end_write, end_write - start_write);

    return rc;
}

int __attribute__((noinline)) bench(size_t size, spi_sdcard_t *device, size_t max_iters) {
    printf("Running benchmark with size %d and %d iters\r\n", size, max_iters);
    printf("Timebase is an RTC at %d Hz (expecting 1MHz)\r\n", *reg32(&__base_regs, CHESHIRE_RTC_FREQ_REG_OFFSET));
    int rc = 0;
    if ((rc = bench_write(size, device, max_iters)) != 0) {
        printf("Write failed with rc %d\r\n", rc);
        /* return rc; */
    }
    if ((rc = bench_read(size, device, max_iters)) != 0) {
        printf("Read failed with rc %d\r\n", rc);
        /* return rc; */
    }
    return rc;
}

int main(void) {
    uint32_t rtc_freq = *reg32(&__base_regs, CHESHIRE_RTC_FREQ_REG_OFFSET);
    uint64_t core_freq = clint_get_core_freq(rtc_freq, 2500);
    uart_init(&__base_uart, core_freq, 1000000);
    printf("Hello, working\r\n");
    // Initialize device handle
    spi_sdcard_t device = {
        .spi_freq = MIN(24 * 1000 * 1000, core_freq / 2), // Up to half core freq or 24MHz (<25MHz)
        .csid = 0,
        .csid_dummy = SPI_HOST_PARAM_NUM_C_S - 1 // Last physical CS is designated dummy
    };
    CHECK_CALL(spi_sdcard_init(&device, core_freq));
    printf("After init\r\n");
    // Wait for device to be initialized (1ms, round up extra tick to be sure)
    clint_spin_until((1000 * rtc_freq) / (1000 * 1000) + 1);

    size_t blocks[] = {
        1,
        2,
        4,
        8,
        16,
        32,
        64,
        128,
        256,
        512
        // TODO: allocate the block with malloc and test larger sizes
    };
    int rc = 0;

    for (size_t i = 0; i < (sizeof(blocks)/sizeof(blocks[0])); ++i) {
        if ((rc = bench(SIZE * blocks[i], &device, 10)) != 0) {
            printf("Failed with %d blocks, RC %d\r\n", blocks[i], rc);
        }
        clint_spin_ticks(10);
    }

    printf("Success\r\n");
    uart_write_flush(&__base_uart);
}
