/*!
    \file    voice_codec_es8311.c
    \brief   Minimal ES8311 codec bring-up for the AI audio daughter board.
*/

#include <stdint.h>
#include <stdio.h>
#include "app_cfg.h"

#ifdef CONFIG_VOICE_AI_AUDIO_BOARD

#include "gd32vw55x.h"
#include "systime.h"
#include "wrapper_os.h"
#include "voice_codec_es8311.h"

#define ES8311_ADDR_A               0x18
#define ES8311_ADDR_B               0x19

typedef struct {
    uint32_t scl_gpio;
    uint32_t scl_pin;
    uint32_t sda_gpio;
    uint32_t sda_pin;
} voice_i2c_bus_t;

static const voice_i2c_bus_t voice_i2c_buses[] = {
    /* AI audio board schematic labels: IIC_SCL/PB15s, IIC_SDA/PA8s. */
    {GPIOB, GPIO_PIN_15, GPIOA, GPIO_PIN_8},
    /* Earlier bring-up wiring candidate. */
    {GPIOB, GPIO_PIN_12, GPIOB, GPIO_PIN_13},
    /* Swapped earlier bring-up candidate, useful when SCL/SDA labels were reversed. */
    {GPIOB, GPIO_PIN_13, GPIOB, GPIO_PIN_12},
};

static voice_i2c_bus_t voice_i2c_bus = {GPIOB, GPIO_PIN_15, GPIOA, GPIO_PIN_8};
static uint8_t es8311_addr = ES8311_ADDR_A;
static volatile uint8_t es8311_reg_dump[0x50];
static volatile uint32_t es8311_reg_dump_ok;
static volatile uint32_t es8311_write_ok_count;
static volatile uint32_t es8311_write_fail_count;
static volatile uint32_t es8311_write_retry_count;
static volatile uint8_t es8311_last_fail_reg;
static volatile uint8_t es8311_last_fail_val;
static volatile int32_t es8311_init_result;
static volatile uint32_t voice_i2c_idle_scl;
static volatile uint32_t voice_i2c_idle_sda;
static volatile uint32_t voice_i2c_scl_low_seen;
static volatile uint32_t voice_i2c_sda_low_seen;
static volatile uint32_t voice_i2c_scl_release_seen;
static volatile uint32_t voice_i2c_sda_release_seen;
static volatile uint32_t voice_i2c_bus_index = 0xFFFFFFFF;

static void i2c_delay(void)
{
    systick_udelay(5);
}

static void soft_i2c_select_bus(const voice_i2c_bus_t *bus)
{
    voice_i2c_bus = *bus;
}

static void sda_output_mode(void)
{
    gpio_mode_set(voice_i2c_bus.sda_gpio, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP,
                  voice_i2c_bus.sda_pin);
    gpio_output_options_set(voice_i2c_bus.sda_gpio, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ,
                            voice_i2c_bus.sda_pin);
}

static void sda_input_mode(void)
{
    gpio_mode_set(voice_i2c_bus.sda_gpio, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP,
                  voice_i2c_bus.sda_pin);
}

static void scl_high(void)
{
    gpio_bit_set(voice_i2c_bus.scl_gpio, voice_i2c_bus.scl_pin);
    i2c_delay();
}

static void scl_low(void)
{
    gpio_bit_reset(voice_i2c_bus.scl_gpio, voice_i2c_bus.scl_pin);
    i2c_delay();
}

static void sda_high(void)
{
    sda_output_mode();
    gpio_bit_set(voice_i2c_bus.sda_gpio, voice_i2c_bus.sda_pin);
    i2c_delay();
}

static void sda_low(void)
{
    sda_output_mode();
    gpio_bit_reset(voice_i2c_bus.sda_gpio, voice_i2c_bus.sda_pin);
    i2c_delay();
}

static uint8_t sda_read(void)
{
    return gpio_input_bit_get(voice_i2c_bus.sda_gpio, voice_i2c_bus.sda_pin) == SET;
}

static void soft_i2c_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    gpio_bit_set(voice_i2c_bus.scl_gpio, voice_i2c_bus.scl_pin);
    gpio_bit_set(voice_i2c_bus.sda_gpio, voice_i2c_bus.sda_pin);
    gpio_mode_set(voice_i2c_bus.scl_gpio, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP,
                  voice_i2c_bus.scl_pin);
    gpio_mode_set(voice_i2c_bus.sda_gpio, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP,
                  voice_i2c_bus.sda_pin);
    gpio_output_options_set(voice_i2c_bus.scl_gpio, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ,
                            voice_i2c_bus.scl_pin);
    gpio_output_options_set(voice_i2c_bus.sda_gpio, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ,
                            voice_i2c_bus.sda_pin);
}

static void soft_i2c_bus_selftest(void)
{
    scl_high();
    sda_high();
    voice_i2c_idle_scl = gpio_input_bit_get(voice_i2c_bus.scl_gpio, voice_i2c_bus.scl_pin) == SET;
    voice_i2c_idle_sda = gpio_input_bit_get(voice_i2c_bus.sda_gpio, voice_i2c_bus.sda_pin) == SET;

    scl_low();
    voice_i2c_scl_low_seen = gpio_input_bit_get(voice_i2c_bus.scl_gpio, voice_i2c_bus.scl_pin) == RESET;
    scl_high();
    voice_i2c_scl_release_seen = gpio_input_bit_get(voice_i2c_bus.scl_gpio, voice_i2c_bus.scl_pin) == SET;

    sda_low();
    voice_i2c_sda_low_seen = gpio_input_bit_get(voice_i2c_bus.sda_gpio, voice_i2c_bus.sda_pin) == RESET;
    sda_high();
    voice_i2c_sda_release_seen = gpio_input_bit_get(voice_i2c_bus.sda_gpio, voice_i2c_bus.sda_pin) == SET;
}

static void soft_i2c_start(void)
{
    sda_high();
    scl_high();
    sda_low();
    scl_low();
}

static void soft_i2c_stop(void)
{
    sda_low();
    scl_high();
    sda_high();
}

static int soft_i2c_write_byte(uint8_t byte)
{
    uint8_t i;
    uint8_t nack;

    for (i = 0; i < 8; i++) {
        if (byte & 0x80) {
            sda_high();
        } else {
            sda_low();
        }
        scl_high();
        scl_low();
        byte <<= 1;
    }

    sda_input_mode();
    i2c_delay();
    scl_high();
    nack = sda_read();
    scl_low();
    sda_output_mode();
    gpio_bit_set(voice_i2c_bus.sda_gpio, voice_i2c_bus.sda_pin);

    return nack ? -1 : 0;
}

static uint8_t soft_i2c_read_byte(uint8_t ack)
{
    uint8_t i;
    uint8_t byte = 0;

    sda_input_mode();
    i2c_delay();
    for (i = 0; i < 8; i++) {
        byte <<= 1;
        scl_high();
        if (sda_read()) {
            byte |= 1;
        }
        scl_low();
    }

    sda_output_mode();
    if (ack) {
        sda_low();
    } else {
        sda_high();
    }
    scl_high();
    scl_low();
    sda_high();

    return byte;
}

static int soft_i2c_probe(uint8_t addr)
{
    int ret;

    soft_i2c_start();
    ret = soft_i2c_write_byte((uint8_t)(addr << 1));
    soft_i2c_stop();

    return ret;
}

static int es8311_write_once(uint8_t reg, uint8_t val)
{
    int ret = 0;

    soft_i2c_start();
    ret |= soft_i2c_write_byte((uint8_t)(es8311_addr << 1));
    ret |= soft_i2c_write_byte(reg);
    ret |= soft_i2c_write_byte(val);
    soft_i2c_stop();

    if (ret != 0) {
        return -1;
    }

    return 0;
}

static int es8311_write(uint8_t reg, uint8_t val)
{
    uint32_t attempt;

    for (attempt = 0; attempt < 5; attempt++) {
        if (es8311_write_once(reg, val) == 0) {
            if (attempt != 0) {
                es8311_write_retry_count += attempt;
            }
            es8311_write_ok_count++;
            return 0;
        }

        soft_i2c_stop();
        sys_ms_sleep(2);
    }

    es8311_write_fail_count++;
    es8311_last_fail_reg = reg;
    es8311_last_fail_val = val;
    printf("voice codec: es8311 write 0x%02x=0x%02x failed\r\n", reg, val);
    return -1;
}

static int es8311_read(uint8_t reg, uint8_t *val)
{
    int ret = 0;

    soft_i2c_start();
    ret |= soft_i2c_write_byte((uint8_t)(es8311_addr << 1));
    ret |= soft_i2c_write_byte(reg);
    soft_i2c_start();
    ret |= soft_i2c_write_byte((uint8_t)((es8311_addr << 1) | 1));
    if (ret == 0) {
        *val = soft_i2c_read_byte(0);
    }
    soft_i2c_stop();

    return ret ? -1 : 0;
}

static void es8311_dump_registers(void)
{
    uint8_t reg;
    uint8_t val;
    uint32_t ok = 0;

    for (reg = 0; reg < sizeof(es8311_reg_dump); reg++) {
        if (es8311_read(reg, &val) == 0) {
            es8311_reg_dump[reg] = val;
            ok++;
        } else {
            es8311_reg_dump[reg] = 0xEE;
        }
    }

    es8311_reg_dump_ok = ok;
}

static void voice_mclk_start(void)
{
    rcu_periph_clock_enable(RCU_GPIOB);
    gpio_af_set(GPIOB, GPIO_AF_0, GPIO_PIN_11);
    gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_11);
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, GPIO_PIN_11);
    rcu_ckout1_config(RCU_CKOUT1SRC_HXTAL, RCU_CKOUT1_DIV4);
}

static int es8311_write_init_table(void)
{
    static const uint8_t init_table[][2] = {
        /* Reset, power on, I2S slave. */
        {0x00, 0x1F}, {0x00, 0x00}, {0x00, 0x80},

        /* Clock source: use BCLK/SCK as internal MCLK.
         * For 48 kHz, 16-bit stereo frame: BCLK = 48k * 16 * 2 = 1.536 MHz.
         * These divider values mirror the public Espressif ES8311 driver
         * coeff entry for mclk=1536000, rate=48000.
         */
        {0x01, 0xBF},
        {0x02, 0x18}, {0x03, 0x10}, {0x04, 0x10}, {0x05, 0x00},
        {0x06, 0x03}, {0x07, 0x00}, {0x08, 0xFF},

        /* Serial data: I2S, 16-bit. */
        {0x09, 0x0C}, {0x0A, 0x0C},

        /* Power analog/DAC path and headphone/line output driver. */
        {0x0D, 0x01}, {0x0E, 0x02}, {0x12, 0x00}, {0x13, 0x10},
        {0x1C, 0x6A},

        /* DAC unmute, moderate digital volume, EQ bypass/ramp default. */
        {0x31, 0x00}, {0x32, 0xC0}, {0x37, 0x08},
        {0x44, 0x00},
    };
    uint32_t i;

    for (i = 0; i < sizeof(init_table) / sizeof(init_table[0]); i++) {
        if (es8311_write(init_table[i][0], init_table[i][1]) != 0) {
            if (init_table[i][0] == 0x37) {
                printf("voice codec: continue after optional reg 0x37 write failure\r\n");
                continue;
            }
            return -1;
        }
        sys_ms_sleep(init_table[i][0] == 0x00 ? 10 : 2);
    }

    return 0;
}

int voice_codec_es8311_init(void)
{
    uint32_t i;
    uint8_t found = 0;

    es8311_init_result = -100;
    es8311_write_ok_count = 0;
    es8311_write_fail_count = 0;
    es8311_write_retry_count = 0;
    voice_i2c_bus_index = 0xFFFFFFFF;

    voice_mclk_start();
    sys_ms_sleep(20);

    for (i = 0; i < sizeof(voice_i2c_buses) / sizeof(voice_i2c_buses[0]); i++) {
        soft_i2c_select_bus(&voice_i2c_buses[i]);
        soft_i2c_init();
        soft_i2c_bus_selftest();

        if (soft_i2c_probe(ES8311_ADDR_A) == 0) {
            es8311_addr = ES8311_ADDR_A;
            found = 1;
        } else if (soft_i2c_probe(ES8311_ADDR_B) == 0) {
            es8311_addr = ES8311_ADDR_B;
            found = 1;
        }

        if (found) {
            voice_i2c_bus_index = i;
            break;
        }
    }

    if (!found) {
        printf("voice codec: ES8311 not found on configured I2C candidate buses\r\n");
        es8311_init_result = -1;
        return -1;
    }

    printf("voice codec: ES8311 found at 0x%02x, init DAC path\r\n", es8311_addr);

    if (es8311_write_init_table() != 0) {
        es8311_init_result = -2;
        return -1;
    }

    es8311_dump_registers();

    printf("voice codec: ES8311 init done\r\n");
    es8311_init_result = 0;
    return 0;
}

#endif /* CONFIG_VOICE_AI_AUDIO_BOARD */
