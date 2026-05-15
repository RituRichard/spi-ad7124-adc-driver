# SPI Driver for AD7124-8 Precision ADC
 
A bare-metal SPI driver for the **Analog Devices AD7124-8** 24-bit sigma-delta ADC, developed on a **Silicon Labs EFM32** microcontroller using Simplicity Studio. Implemented as part of an embedded systems internship at INNIRION GmbH, Freiburg.
 
---
 
## Hardware
 
| Component | Part |
|---|---|
| ADC | AD7124-8BCPZ (24-bit, sigma-delta, 8-channel) |
| MCU | Silicon Labs EFM32 (EFR32xG series) |
| Interface | SPI Mode 3 (CPOL=1, CPHA=1), USART0 |
| Reference Voltage | ADR3625ARMZ — 2.500 V precision reference |
| AVDD LDO | ADP7118 — 3.00 V regulated supply |
| Logic Analyser | Saleae Logic 2 |
| IDE | Simplicity Studio (GCC ARM toolchain) |
| Debugger | SEGGER J-Link |
 
**Pin mapping (EFM32 → AD7124):**
 
| Signal | EFM32 Pin |
|---|---|
| SPI CS | PA4 |
| SPI MOSI | PB2 |
| SPI MISO | PB3 |
| SPI SCK | PB4 |
 
---
 
## Project Structure
 
```
.
├── main.c                        # Application code (all tasks)
├── images/
│   ├── schematic_adc.png         # AD7124 mockup schematic
│   ├── schematic_connectors.png  # Connector / socket schematic
│   ├── pin_configuration.png     # Debugger — pin config view
│   ├── config0_configuration.png # Debugger — CONFIG0 register
│   ├── filter0_configuration.png # Debugger — FILTER0 register
│   ├── continuous_mode.png       # Debugger — ADC_CONTROL continuous mode
│   ├── read_data.png             # Debugger — DATA register read
│   ├── read_status_1.png         # Debugger — STATUS register (1)
│   ├── read_status_2.png         # Debugger — STATUS register (2)
│   ├── read_error.png            # Debugger — ERROR register
│   ├── reading_channel_config.png# Debugger — CHANNEL register
│   ├── logic_soft_reset.png      # Logic analyser — soft reset (8×0xFF)
│   ├── logic_write_control.png   # Logic analyser — ADC_CONTROL write
│   ├── logic_channel_config.png  # Logic analyser — channel setup
│   └── logic_read_data.png       # Logic analyser — data read
└── README.md
```
 
---
 
## Implementation
 
### Task 1 — SPI Hardware Init
 
Configured USART0 as a synchronous SPI master (Mode 3: CPOL=1, CPHA=1) with GPIO routing for MOSI, MISO, SCK, and a software-controlled CS on PA4.
 
```c
void initUSART0(void) {
    CMU_ClockEnable(cmuClock_USART0, true);
    USART_InitSync_TypeDef init = USART_INITSYNC_DEFAULT;
    init.msbf = true;
    init.clockMode = usartClockMode3;
    // GPIO routing for MOSI/MISO/SCK...
    USART_InitSync(USART0, &init);
}
```
 
### Task 2 — Register Read
 
Single-byte read using the AD7124 command byte format: `0x40 | address`.
 
```c
uint8_t ad7124_read_register(uint8_t address) {
    uint8_t command_byte = 0x40 | (address & 0x3F);
    GPIO_PinOutClear(SPI_CS_PORT, SPI_CS_PIN);
    USART_SpiTransfer(USART0, command_byte);
    uint8_t result = USART_SpiTransfer(USART0, 0x00);
    GPIO_PinOutSet(SPI_CS_PORT, SPI_CS_PIN);
    return result;
}
```
 
### Task 3 — Soft Reset
 
Sends 64 consecutive `0xFF` bytes (8 SPI transfers) to trigger the AD7124 hardware reset sequence.
 
```c
void ad7124_soft_reset(void) {
    GPIO_PinOutClear(SPI_CS_PORT, SPI_CS_PIN);
    for (int i = 0; i < 8; i++)
        My_SpiTransfer(USART0, 0xFF);
    GPIO_PinOutSet(SPI_CS_PORT, SPI_CS_PIN);
}
```
 
### Task 4 — Generic Read/Write (N-byte)
 
Abstracted multi-byte read and write functions used for all subsequent register accesses.
 
```c
void ad7124_write_bytes(uint8_t address, const uint8_t *data, uint8_t length);
void ad7124_read_bytes(uint8_t address, uint8_t *data, uint8_t length);
```
 
### Task 5 — Register Configuration
 
Configured ADC_CONTROL, ERROR_ENABLE, CHANNEL0, CONFIG0, and FILTER0 registers:
 
- **ADC_CONTROL**: Standby mode, internal reference enabled
- **CONFIG0**: REFIN1 reference, bipolar mode, all input buffers enabled, gain = 1
- **FILTER0**: Sinc3 filter, output data rate ~5 SPS (FS calculated from 153.6 kHz mid-power clock)
- **CHANNEL0**: AIN0+, AIN1−, linked to CONFIG0
### Task 6 — Continuous Conversion Loop
 
Switched ADC to continuous conversion mode, then polled STATUS, ERROR, and DATA registers in a loop. Raw 24-bit values converted to millivolts:
 
```c
float convertToMillivolts(uint32_t raw, bool bipolar, int gain) {
    float offset     = bipolar ? 8388608.0f : 0.0f;   // 2^23
    float resolution = bipolar ? 8388608.0f : 16777216.0f;
    float vref_mV    = 2500.0f;
    return ((float)raw - offset) * (vref_mV / resolution / gain);
}
```
 
---
 
## Logic Analyser Captures
 
SPI bus captured with Saleae Logic 2 (D0=CS, D1=MOSI, D2=MISO, D3=SCK).
 
### Soft Reset — 8 × 0xFF
 
![Soft Reset](images/logic_soft_reset.png)
 
CS pulled low; eight `0xFF` bytes sent on MOSI. MISO returns `0xFF` (no data driven by ADC during reset). Clock frequency ~103 kHz.
 
### ADC_CONTROL Write — Standby Mode
 
![Control Write](images/logic_write_control.png)
 
Command byte `0x01` (write ADC_CONTROL), followed by `0x00`, `0xC0` (standby + REFEN bits set).
 
### Channel Configuration Write
 
![Channel Config](images/logic_channel_config.png)
 
Four-byte transaction: command byte `0x21` (write FILTER0), then three configuration bytes setting the FS rate for ~5 SPS.
 
### DATA Register Read
 
![Data Read](images/logic_read_data.png)
 
Command byte `0x42` (read DATA register). ADC returns three bytes on MISO (`0xAB`, `0x78`, `0xA3`) — a live 24-bit conversion result.
 
---
 
## Debugger Screenshots
 
All register values verified live in Simplicity Studio via SEGGER J-Link.
 
| View | Description |
|---|---|
| ![Pin Config](images/pin_configuration.png) | SPI pin routing confirmed |
| ![Config0](images/config0_configuration.png) | CONFIG0: bipolar, REFIN1, buffers on, gain=1 |
| ![Filter0](images/filter0_configuration.png) | FILTER0: FS register value for ~5 SPS |
| ![Continuous](images/continuous_mode.png) | ADC_CONTROL: continuous conversion mode active |
| ![Status 1](images/read_status_1.png) | STATUS register — channel ID, RDY bit |
| ![Status 2](images/read_status_2.png) | STATUS register — second read |
| ![Error](images/read_error.png) | ERROR register — no active errors |
| ![Data](images/read_data.png) | DATA register — 24-bit conversion result |
| ![Channel](images/reading_channel_config.png) | CHANNEL0 register — AIN0/AIN1 mapping |
 
---
 
## Schematic
 
The hardware target is the INNIRION AD7124 mockup board (1118_Mockup_AD7124, rev V1I1).
 
Key design features:
- ADP7118 LDO generates a clean 3.00 V AVDD for the analog supply
- ADR3625 provides a precision 2.500 V reference (REFIN1+/REFIN1−)
- Anti-aliasing RC filters (40.2 kΩ + 100 nF, fc ≈ 39.6 Hz) on analog inputs
- Internal 32 Hz sine reference signal for calibration channel
- SPI routed through a 20-pin socket (JS1) to the EFM32 host board
---
 
## Tools & Environment
 
- **IDE:** Simplicity Studio 5 (Silicon Labs)
- **Toolchain:** GCC ARM
- **Debugger:** SEGGER J-Link
- **Logic Analyser:** Saleae Logic 2
- **ADC Datasheet:** [AD7124-8 (Analog Devices)](https://www.analog.com/en/products/ad7124-8.html)
