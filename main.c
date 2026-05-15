#include "em_device.h"
#include "em_chip.h"
#include "em_gpio.h"
#include "em_cmu.h"
#include "em_usart.h"
 
/* ── Pin mapping ─────────────────────────────────────────────────────────── */
#define SPI_CS_PORT    gpioPortA
#define SPI_CS_PIN     4
#define SPI_MOSI_PORT  gpioPortB
#define SPI_MOSI_PIN   2
#define SPI_MISO_PORT  gpioPortB
#define SPI_MISO_PIN   3
#define SPI_SCK_PORT   gpioPortB
#define SPI_SCK_PIN    4
 
/* ── AD7124 register addresses ───────────────────────────────────────────── */
#define AD7124_REG_STATUS       0x00
#define AD7124_REG_ADC_CONTROL  0x01
#define AD7124_REG_DATA         0x02
#define AD7124_REG_ERROR        0x06
#define AD7124_REG_ERROR_ENABLE 0x07
#define AD7124_REG_CHANNEL0     0x09
#define AD7124_REG_CHANNEL1     0x0A
#define AD7124_REG_CHANNEL2     0x0B
#define AD7124_REG_CONFIG0      0x19
#define AD7124_REG_FILTER0      0x21
 
/* ── ADC_CONTROL bit fields ──────────────────────────────────────────────── */
#define AD7124_ADC_CTRL_REF_EN      (1u << 7)
#define AD7124_ADC_CTRL_MODE_POS    2
#define AD7124_MODE_CONTINUOUS      (0x0u << AD7124_ADC_CTRL_MODE_POS)
#define AD7124_MODE_STANDBY         (0x2u << AD7124_ADC_CTRL_MODE_POS)
 
typedef enum {
    AD7124_POWER_LOW,
    AD7124_POWER_MID,
    AD7124_POWER_FULL
} ad7124_power_mode_t;
 
/* ── Task 1: GPIO + USART init ───────────────────────────────────────────── */
void initGPIO(void)
{
    CMU_ClockEnable(cmuClock_GPIO, true);
    GPIO_PinModeSet(SPI_CS_PORT,   SPI_CS_PIN,   gpioModePushPull, 1); // CS idle HIGH
    GPIO_PinModeSet(SPI_MOSI_PORT, SPI_MOSI_PIN, gpioModePushPull, 0);
    GPIO_PinModeSet(SPI_MISO_PORT, SPI_MISO_PIN, gpioModeInput,    0);
    GPIO_PinModeSet(SPI_SCK_PORT,  SPI_SCK_PIN,  gpioModePushPull, 1); // CLK idle HIGH (Mode 3)
}
 
void initUSART0(void)
{
    CMU_ClockEnable(cmuClock_USART0, true);
 
    USART_InitSync_TypeDef init = USART_INITSYNC_DEFAULT;
    init.msbf      = true;
    init.clockMode = usartClockMode3; // CPOL=1, CPHA=1
 
    GPIO->USARTROUTE[0].TXROUTE  = (SPI_MOSI_PORT << _GPIO_USART_TXROUTE_PORT_SHIFT)
                                 | (SPI_MOSI_PIN  << _GPIO_USART_TXROUTE_PIN_SHIFT);
    GPIO->USARTROUTE[0].RXROUTE  = (SPI_MISO_PORT << _GPIO_USART_RXROUTE_PORT_SHIFT)
                                 | (SPI_MISO_PIN  << _GPIO_USART_RXROUTE_PIN_SHIFT);
    GPIO->USARTROUTE[0].CLKROUTE = (SPI_SCK_PORT  << _GPIO_USART_CLKROUTE_PORT_SHIFT)
                                 | (SPI_SCK_PIN   << _GPIO_USART_CLKROUTE_PIN_SHIFT);
    GPIO->USARTROUTE[0].ROUTEEN  = GPIO_USART_ROUTEEN_RXPEN
                                 | GPIO_USART_ROUTEEN_TXPEN
                                 | GPIO_USART_ROUTEEN_CLKPEN;
 
    USART_InitSync(USART0, &init);
}
 
/* ── Low-level SPI byte transfer ─────────────────────────────────────────── */
uint8_t My_SpiTransfer(USART_TypeDef *usart, uint8_t data)
{
    while (!(usart->STATUS & USART_STATUS_TXBL));
    USART_Tx(usart, data);
    while (!(usart->STATUS & USART_STATUS_RXDATAV));
    return USART_Rx(usart);
}
 
/* ── Task 3: Soft reset (8 × 0xFF) ──────────────────────────────────────── */
void ad7124_soft_reset(void)
{
    GPIO_PinOutClear(SPI_CS_PORT, SPI_CS_PIN);
    for (int i = 0; i < 8; i++)
        My_SpiTransfer(USART0, 0xFF);
    GPIO_PinOutSet(SPI_CS_PORT, SPI_CS_PIN);
}
 
/* ── Task 2 / Task 4: Generic N-byte read & write ────────────────────────── */
void ad7124_write_bytes(uint8_t address, const uint8_t *data, uint8_t length)
{
    uint8_t command = 0x00 | (address & 0x3F); // write: bit7 = 0
    GPIO_PinOutClear(SPI_CS_PORT, SPI_CS_PIN);
    My_SpiTransfer(USART0, command);
    for (uint8_t i = 0; i < length; i++)
        My_SpiTransfer(USART0, data[i]);
    GPIO_PinOutSet(SPI_CS_PORT, SPI_CS_PIN);
}
 
void ad7124_read_bytes(uint8_t address, uint8_t *data, uint8_t length)
{
    uint8_t command = 0x40 | (address & 0x3F); // read: bit7 = 1 (0x40)
    GPIO_PinOutClear(SPI_CS_PORT, SPI_CS_PIN);
    My_SpiTransfer(USART0, command);
    for (uint8_t i = 0; i < length; i++)
        data[i] = My_SpiTransfer(USART0, 0x00);
    GPIO_PinOutSet(SPI_CS_PORT, SPI_CS_PIN);
}
 
/* ── Task 5: Filter FS calculation ──────────────────────────────────────── */
uint16_t ad7124_calc_fs(float desired_sps, ad7124_power_mode_t pwr)
{
    float fclk_hz;
    switch (pwr) {
        case AD7124_POWER_LOW:  fclk_hz =  76800.0f; break;
        case AD7124_POWER_MID:  fclk_hz = 153600.0f; break;
        case AD7124_POWER_FULL: fclk_hz = 614400.0f; break;
        default:                fclk_hz = 153600.0f; break;
    }
    float fs_f = fclk_hz / (32.0f * desired_sps);
    if (fs_f <    1.0f) fs_f =    1.0f;
    if (fs_f > 2047.0f) fs_f = 2047.0f;
    return (uint16_t)(fs_f + 0.5f);
}
 
/* ── Task 6: ADC raw → millivolts conversion ─────────────────────────────── */
float convertToMillivolts(uint32_t raw, bool bipolar, int gain)
{
    float offset     = bipolar ? 8388608.0f  : 0.0f;        // 2^23
    float resolution = bipolar ? 8388608.0f  : 16777216.0f; // 2^23 or 2^24
    float vref_mV    = 2500.0f;                              // ADR3625: 2.500 V
    return ((float)raw - offset) * (vref_mV / resolution / (float)gain);
}
 
/* ── Wait for DRDY (STATUS bit 7 low) ───────────────────────────────────── */
void ad7124_wait_for_drdy(void)
{
    uint8_t status;
    do {
        ad7124_read_bytes(AD7124_REG_STATUS, &status, 1);
    } while (status & 0x80); // RDY bit: 0 = data ready
}
 
/* ── Main ────────────────────────────────────────────────────────────────── */
int main(void)
{
    CHIP_Init();
    initGPIO();
    initUSART0();
 
    /* 1) Soft reset */
    ad7124_soft_reset();
 
    uint8_t buf[3];
 
    /* 2) ADC_CONTROL: standby, internal reference enabled */
    buf[0] = 0x00;
    buf[1] = AD7124_ADC_CTRL_REF_EN | (1u << 6) | AD7124_MODE_STANDBY;
    ad7124_write_bytes(AD7124_REG_ADC_CONTROL, buf, 2);
 
    /* 3) ERROR_ENABLE: enable conversion, saturation, OV/UV, SPI errors */
    buf[0] = (1u << 1) | (1u << 0);                          // ADC errors
    buf[1] = (1u << 7) | (1u << 6) | (1u << 5) | (1u << 4); // AIN OV/UV
    buf[2] = (1u << 6) | (1u << 5) | (1u << 4) | (1u << 3); // SPI errors
    ad7124_write_bytes(AD7124_REG_ERROR_ENABLE, buf, 3);
 
    /* 4) Disable all channels first */
    for (uint8_t ch = 0; ch < 8; ch++) {
        uint8_t cbuf[2] = {0x00, 0x00};
        ad7124_write_bytes(AD7124_REG_CHANNEL0 + ch, cbuf, 2);
    }
 
    /* 5) CHANNEL0: enable, AIN0+ / AIN1−, use CONFIG0 */
    {
        uint8_t pos = 0; // AIN0
        uint8_t neg = 1; // AIN1
        buf[0] = (1u << 7) | ((pos >> 3) & 0x03);            // enable + MSBs of AINP
        buf[1] = ((pos & 0x07) << 5) | (neg & 0x1F);         // LSBs of AINP + AINM
        ad7124_write_bytes(AD7124_REG_CHANNEL0, buf, 2);
    }
 
    /* 6) CONFIG0: REFIN1, bipolar, all buffers on, gain = 1 */
    buf[0] = (1u << 3) | (1u << 0);                          // bipolar + RefBuf+
    buf[1] = (1u << 7) | (1u << 6) | (1u << 5);              // RefBuf- + AINBuf+ + AINBuf-
    ad7124_write_bytes(AD7124_REG_CONFIG0, buf, 2);
 
    /* 7) FILTER0: Sinc3, ~5 SPS output rate (mid-power clock 153.6 kHz) */
    uint16_t fs = ad7124_calc_fs(5.0f, AD7124_POWER_MID);
    buf[0] = 0x00;
    buf[1] = (uint8_t)((fs >> 8) & 0x07);
    buf[2] = (uint8_t)(fs & 0xFF);
    ad7124_write_bytes(AD7124_REG_FILTER0, buf, 3);
 
    /* 8) ADC_CONTROL: continuous conversion mode */
    buf[0] = 0x00;
    buf[1] = AD7124_ADC_CTRL_REF_EN | (1u << 6) | AD7124_MODE_CONTINUOUS;
    ad7124_write_bytes(AD7124_REG_ADC_CONTROL, buf, 2);
 
    /* 9) Conversion loop */
    while (1)
    {
        ad7124_wait_for_drdy();
 
        uint8_t status;
        ad7124_read_bytes(AD7124_REG_STATUS, &status, 1);
 
        uint8_t err[3];
        ad7124_read_bytes(AD7124_REG_ERROR, err, 3);
 
        uint8_t data_b[3];
        ad7124_read_bytes(AD7124_REG_DATA, data_b, 3);
 
        uint32_t adc_raw = ((uint32_t)data_b[0] << 16)
                         | ((uint32_t)data_b[1] <<  8)
                         |  (uint32_t)data_b[2];
 
        float mv = convertToMillivolts(adc_raw, true, 1);
 
        (void)status;
        (void)err;
        (void)mv;
    }
}
