/*
 * hdlc_platform.h — Platform Abstraction Layer (PAL) contract
 *
 * Implement these three functions for your MCU/RTOS, then add
 * hdlc_mcu_port.c to your build system.
 *
 * Example (STM32 HAL + DMA):
 *
 *   void port_tx_byte(uint_least8_t byte, bool flush) {
 *       tx_ring[tx_head] = byte;
 *       tx_head = (tx_head + 1) & TX_RING_MASK;
 *       if (flush) tx_flush_dma();
 *   }
 *
 *   uint32_t port_tick_ms(void) { return HAL_GetTick(); }
 *
 *   uint16_t port_rx_read(uint_least8_t *buf, uint16_t max_len) {
 *       uint16_t dma_head = RX_SIZE - __HAL_DMA_GET_COUNTER(huart.hdmarx);
 *       // ... copy from DMA ring into buf, return byte count
 *   }
 */

#ifndef HDLC_PLATFORM_H
#define HDLC_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Write one byte to the physical layer (UART, SPI, …).
 * Called for every outgoing byte. flush == true on the last byte of each
 * HDLC frame — use it to trigger a DMA transfer or FIFO flush.
 */
void port_tx_byte(uint_least8_t byte, bool flush);

/*
 * Return a free-running millisecond counter (may wrap at UINT32_MAX).
 * Used for T1/T2 timer expiry checks in hdlc_port_run().
 */
uint32_t port_tick_ms(void);

/*
 * Copy available received bytes into buf (non-blocking).
 * Called once per hdlc_port_run() iteration.
 * Return the number of bytes copied; return 0 if nothing is available.
 */
uint16_t port_rx_read(uint_least8_t *buf, uint16_t max_len);

#endif /* HDLC_PLATFORM_H */
