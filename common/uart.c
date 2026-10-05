#include "uart.h"
#include "board.h"
#include "config.h"
#include "hal.h"

#define RING_485_RX   2048u   /* a whole poll frame between two drains */
#define RING_VE_RX    512u
#define RING_VE_TX    64u

struct ring {
    uint8_t          *bytes;
    uint16_t          mask;
    volatile uint16_t head;   /* written by the producer */
    volatile uint16_t tail;   /* written by the consumer */
};

struct uart {
    Sercom     *sercom;
    uint8_t     index;        /* SERCOM number */
    uint8_t     tx_pin, rx_pin;
    uint8_t     mux;          /* peripheral function */
    uint8_t     tx_pad, rx_pad;
    struct ring rx, tx;
    uint32_t    lost;
};

static uint8_t rx485[RING_485_RX];
static uint8_t rx_ve[4][RING_VE_RX];
static uint8_t tx_ve[4][RING_VE_TX];

#define MUX_C 2u
#define MUX_D 3u

static struct uart uarts[UART_COUNT] = {
    [UART_485] = { SERCOM0, 0, PIN(PORT_A, 10), PIN(PORT_A, 11), MUX_C, 2, 3,
                   { rx485, RING_485_RX - 1u, 0, 0 }, { 0, 0, 0, 0 }, 0 },
    [UART_VE1] = { SERCOM1, 1, PIN(PORT_A, 18), PIN(PORT_A, 19), MUX_C, 2, 3,
                   { rx_ve[0], RING_VE_RX - 1u, 0, 0 }, { tx_ve[0], RING_VE_TX - 1u, 0, 0 }, 0 },
    [UART_VE2] = { SERCOM2, 2, PIN(PORT_A, 14), PIN(PORT_A, 15), MUX_C, 2, 3,
                   { rx_ve[1], RING_VE_RX - 1u, 0, 0 }, { tx_ve[1], RING_VE_TX - 1u, 0, 0 }, 0 },
    [UART_VE3] = { SERCOM3, 3, PIN(PORT_A, 22), PIN(PORT_A, 23), MUX_C, 0, 1,
                   { rx_ve[2], RING_VE_RX - 1u, 0, 0 }, { tx_ve[2], RING_VE_TX - 1u, 0, 0 }, 0 },
    [UART_VE4] = { SERCOM4, 4, PIN(PORT_B, 8), PIN(PORT_B, 9), MUX_D, 0, 1,
                   { rx_ve[3], RING_VE_RX - 1u, 0, 0 }, { tx_ve[3], RING_VE_TX - 1u, 0, 0 }, 0 },
};

static uint16_t ring_used(const struct ring *ring)
{
    return (uint16_t)((ring->head - ring->tail) & ring->mask);
}

void uart_init(enum uart_id id, uint32_t baud)
{
    struct uart *uart = &uarts[id];
    SercomUsart *usart = &uart->sercom->USART;

    PM->APBCMASK.reg |= PM_APBCMASK_SERCOM0 << uart->index;
    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID(GCLK_CLKCTRL_ID_SERCOM0_CORE_Val + uart->index)
                      | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_CLKEN;
    while (GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY) {
    }

    usart->CTRLA.reg = SERCOM_USART_CTRLA_SWRST;
    while (usart->SYNCBUSY.reg & SERCOM_USART_SYNCBUSY_SWRST) {
    }

    /* TXPO 0 = TX on pad 0, TXPO 1 = TX on pad 2. 16x oversampling, LSB first. */
    usart->CTRLA.reg = SERCOM_USART_CTRLA_MODE_USART_INT_CLK | SERCOM_USART_CTRLA_DORD
                     | SERCOM_USART_CTRLA_RXPO(uart->rx_pad)
                     | SERCOM_USART_CTRLA_TXPO(uart->tx_pad == 2u ? 1u : 0u);
    usart->BAUD.reg = (uint16_t)(65536ull - (65536ull * 16ull * baud) / CPU_HZ);
    usart->CTRLB.reg = SERCOM_USART_CTRLB_RXEN | SERCOM_USART_CTRLB_TXEN;
    while (usart->SYNCBUSY.reg & SERCOM_USART_SYNCBUSY_CTRLB) {
    }

    pin_peripheral(uart->tx_pin, uart->mux);
    pin_peripheral(uart->rx_pin, uart->mux);

    usart->INTENSET.reg = SERCOM_USART_INTENSET_RXC | SERCOM_USART_INTENSET_ERROR;
    NVIC_EnableIRQ((IRQn_Type)(SERCOM0_IRQn + uart->index));

    usart->CTRLA.reg |= SERCOM_USART_CTRLA_ENABLE;
    while (usart->SYNCBUSY.reg & SERCOM_USART_SYNCBUSY_ENABLE) {
    }
}

static void interrupt(struct uart *uart)
{
    SercomUsart *usart = &uart->sercom->USART;
    const uint8_t flags = usart->INTFLAG.reg;

    if (flags & SERCOM_USART_INTFLAG_ERROR) {
        usart->STATUS.reg = SERCOM_USART_STATUS_BUFOVF | SERCOM_USART_STATUS_FERR
                          | SERCOM_USART_STATUS_PERR;
        usart->INTFLAG.reg = SERCOM_USART_INTFLAG_ERROR;
        uart->lost++;
    }
    if (flags & SERCOM_USART_INTFLAG_RXC) {
        const uint8_t byte = (uint8_t)usart->DATA.reg;
        const uint16_t next = (uint16_t)((uart->rx.head + 1u) & uart->rx.mask);
        if (next != uart->rx.tail) {
            uart->rx.bytes[uart->rx.head] = byte;
            uart->rx.head = next;
        } else {
            uart->lost++;
        }
    }
    if ((flags & SERCOM_USART_INTFLAG_DRE) && (usart->INTENSET.reg & SERCOM_USART_INTENSET_DRE)) {
        if (uart->tx.head != uart->tx.tail) {
            usart->DATA.reg = uart->tx.bytes[uart->tx.tail];
            uart->tx.tail = (uint16_t)((uart->tx.tail + 1u) & uart->tx.mask);
        } else {
            usart->INTENCLR.reg = SERCOM_USART_INTENCLR_DRE;
        }
    }
}

void SERCOM0_Handler(void) { interrupt(&uarts[UART_485]); }
void SERCOM1_Handler(void) { interrupt(&uarts[UART_VE1]); }
void SERCOM2_Handler(void) { interrupt(&uarts[UART_VE2]); }
void SERCOM3_Handler(void) { interrupt(&uarts[UART_VE3]); }
void SERCOM4_Handler(void) { interrupt(&uarts[UART_VE4]); }

size_t uart_rx_available(enum uart_id id)
{
    return ring_used(&uarts[id].rx);
}

size_t uart_read(enum uart_id id, uint8_t *data, size_t limit)
{
    struct ring *ring = &uarts[id].rx;
    size_t count = 0u;
    while (count < limit && ring->tail != ring->head) {
        data[count++] = ring->bytes[ring->tail];
        ring->tail = (uint16_t)((ring->tail + 1u) & ring->mask);
    }
    return count;
}

void uart_rx_flush(enum uart_id id)
{
    uarts[id].rx.tail = uarts[id].rx.head;
}

uint32_t uart_rx_lost(enum uart_id id)
{
    return uarts[id].lost;
}

size_t uart_inject(enum uart_id id, const uint8_t *data, size_t length)
{
    struct uart *uart = &uarts[id];
    struct ring *ring = &uart->rx;
    const IRQn_Type irq = (IRQn_Type)(SERCOM0_IRQn + uart->index);
    size_t count = 0u;

    /* The interrupt is the ring's other producer. */
    NVIC_DisableIRQ(irq);
    while (count < length) {
        const uint16_t next = (uint16_t)((ring->head + 1u) & ring->mask);
        if (next == ring->tail) {
            break;
        }
        ring->bytes[ring->head] = data[count++];
        ring->head = next;
    }
    uart->lost += (uint32_t)(length - count);
    NVIC_EnableIRQ(irq);
    return count;
}

size_t uart_tx_free(enum uart_id id)
{
    const struct ring *ring = &uarts[id].tx;
    return ring->mask == 0u ? 0u : (size_t)(ring->mask - ring_used(ring));
}

size_t uart_write(enum uart_id id, const uint8_t *data, size_t length)
{
    struct uart *uart = &uarts[id];
    struct ring *ring = &uart->tx;
    size_t count = 0u;
    if (ring->mask == 0u) {
        return 0u;
    }
    while (count < length) {
        const uint16_t next = (uint16_t)((ring->head + 1u) & ring->mask);
        if (next == ring->tail) {
            break;
        }
        ring->bytes[ring->head] = data[count++];
        ring->head = next;
    }
    uart->sercom->USART.INTENSET.reg = SERCOM_USART_INTENSET_DRE;
    return count;
}

/* ── RS-485 (hal.h) ─────────────────────────────────────────────────── */

bool rs485_getc(uint8_t *byte)
{
    return uart_read(UART_485, byte, 1u) == 1u;
}

void rs485_send(const uint8_t *data, size_t length)
{
    SercomUsart *usart = &uarts[UART_485].sercom->USART;

    delay_us(CFG_485_TURNAROUND_US);    /* the master lets go of the bus */
    pin_write(PIN_485_DE, true);
    delay_us(60);                       /* let the driver settle to idle-mark */
    for (size_t index = 0u; index < length; index++) {
        while (!(usart->INTFLAG.reg & SERCOM_USART_INTFLAG_DRE)) {
        }
        usart->INTFLAG.reg = SERCOM_USART_INTFLAG_TXC;   /* so it can only mean the last byte */
        usart->DATA.reg = data[index];
    }
    while (!(usart->INTFLAG.reg & SERCOM_USART_INTFLAG_TXC)) {
    }
    delay_us(20);                       /* hold the final stop bit */
    pin_write(PIN_485_DE, false);
    delay_us(60);
    /* If the transceiver's receiver stays enabled we heard ourselves; the bus
     * was ours throughout, so everything captured is echo. */
    uart_rx_flush(UART_485);
}
