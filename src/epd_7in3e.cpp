#include "epd_7in3e.hpp"
#include "log.hpp"
#include <Arduino.h>
#include <SPI.h>
#include <map>
#include <vector>

static const uint8_t COLOR_VALUE[] = {
    [BLACK] = 0x00, [WHITE] = 0x01, [YELLOW] = 0x02, [RED] = 0x03, [BLUE] = 0x05, [GREEN] = 0x06,
};

EPD_7IN3E::EPD_7IN3E()
{
}

void EPD_7IN3E::begin(uint8_t sck, uint8_t mosi, uint8_t cs, uint8_t dc, uint8_t rst, uint8_t busy, uint8_t pwr,
                      uint8_t led)
{
    pin_sck = sck;
    pin_mosi = mosi;
    pin_cs = cs;
    pin_dc = dc;
    pin_rst = rst;
    pin_busy = busy;
    pin_pwr = pwr;
    pin_led = led;

    pinMode(pin_busy, INPUT);
    pinMode(pin_rst, OUTPUT);
    pinMode(pin_dc, OUTPUT);
    pinMode(pin_pwr, OUTPUT);
    pinMode(pin_cs, OUTPUT);
    pinMode(pin_mosi, OUTPUT);
    pinMode(pin_sck, OUTPUT);
    pinMode(pin_led, OUTPUT);
    digitalWrite(pin_cs, HIGH);
    digitalWrite(pin_led, LOW);

    SPI.begin(pin_sck, 5, pin_mosi, -1);
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
}

void EPD_7IN3E::send_command(uint8_t data)
{
    // LOG("cmd: 0x%02X", data);
    digitalWrite(pin_dc, 0);
    digitalWrite(pin_cs, LOW);
    SPI.transfer(data);
    digitalWrite(pin_cs, HIGH);
}

void EPD_7IN3E::send_data(uint8_t data)
{
    digitalWrite(pin_dc, 1);
    digitalWrite(pin_cs, LOW);
    SPI.transfer(data);
    digitalWrite(pin_cs, HIGH);
    // LOG("data: 0x%02X", data);
}

void EPD_7IN3E::write_table(const table_t &table)
{
    for (auto &pair : table)
    {
        if (pair.first == 0xFF)
        {
            wait_busy();
            continue;
        }
        send_command(pair.first);
        for (auto &data : pair.second)
        {
            send_data(data);
        }
    }
}

void EPD_7IN3E::init()
{
    const table_t TABLE = {
        {0xaa, {0x49, 0x55, 0x20, 0x08, 0x09, 0x18}},
        {0x01, {0x3f}},
        {0x00, {0x5f, 0x69}},
        {0x03, {0x00, 0x54, 0x00, 0x44}},
        {0x05, {0x40, 0x1f, 0x1f, 0x2c}},
        {0x06, {0x6f, 0x1f, 0x17, 0x49}},
        {0x08, {0x6f, 0x1f, 0x1f, 0x22}},
        {0x30, {0x03}},
        {0x50, {0x3f}},
        {0x60, {0x02, 0x00}},
        {0x61, {0x03, 0x20, 0x01, 0xe0}},
        {0x84, {0x01}},
        {0xe3, {0x2f}},
        {0x04, {}},
    };

    LOG("EPD_7IN3::init()");
    digitalWrite(pin_pwr, HIGH);
    reset();
    wait_busy();
    delay(30);
    write_table(TABLE);
    wait_busy();
}

void EPD_7IN3E::reset()
{
    LOG("EPD_7IN3::reset()");
    digitalWrite(pin_rst, 1);
    delay(20);
    digitalWrite(pin_rst, 0);
    delay(2);
    digitalWrite(pin_rst, 1);
    delay(20);
}

bool EPD_7IN3E::is_busy()
{
    return digitalRead(pin_busy) == LOW;
}

void EPD_7IN3E::wait_busy()
{
    LOG("Waiting for BUSY...");
    digitalWrite(pin_led, LOW);
    while (is_busy())
    {
        delay(1);
    }
    digitalWrite(pin_led, HIGH);
    LOG("done");
}

void EPD_7IN3E::turn_on_display()
{
    const table_t TABLE = {
        {0x04, {}},     {0xff, {}}, {0x06, {0x6f, 0x1f, 0x17, 0x49}}, {0xff, {}}, {0x12, {0x00}}, {0xff, {}},
        {0x02, {0x00}}, {0xff, {}},
    };
    LOG("EPD_7IN3::turn_on_display()");
    write_table(TABLE);
}

void EPD_7IN3E::clear(color_index_t color_index)
{
    pre_display();
    LOG("EPD_7IN3::clear()");
    begin_write_buffer();
    auto color = get_color_value(color_index);
    for (auto i = 0; i < WIDTH * HEIGHT / 2; i++)
    {
        write_buffer((color << 4) | color);
    }
    end_write_buffer();
    post_display();
}

void EPD_7IN3E::deep_sleep()
{
    table_t TABLE = {
        {0x02, {0x00}},
        {0xff, {}},
        {0x07, {0xa5}},
    };
    LOG("EPD_7IN3::deep_sleep()");
    write_table(TABLE);
    delay(2e3);
}

void EPD_7IN3E::power_down()
{
    LOG("EPD_7IN3::power_down()");
    digitalWrite(pin_rst, LOW);
    digitalWrite(pin_pwr, LOW);
}

uint8_t EPD_7IN3E::get_color_value(uint8_t index)
{
    return (index < COLOR_NUM) ? COLOR_VALUE[index] : 0;
}

void EPD_7IN3E::pre_display()
{
    this->init();
}

void EPD_7IN3E::post_display()
{
    delay(1e3);
    this->deep_sleep();
    this->power_down();
}

void EPD_7IN3E::begin_write_buffer()
{
    LOG("EPD_7IN3::begin_write_buffer()");
    write_cnt = 0;
    send_command(0x10);
}

bool EPD_7IN3E::write_buffer(uint8_t data)
{
    if (write_cnt < WIDTH * HEIGHT / 2)
    {
        send_data(data);
        write_cnt++;
        return true;
    }
    return false;
}

void EPD_7IN3E::end_write_buffer()
{
    LOG("EPD_7IN3::end_write_buffer()");
    turn_on_display();
}

void EPD_7IN3E::set_pixel(size_t x, size_t y, uint8_t color_index)
{
    set_pixel(y * WIDTH + x, color_index);
}

void EPD_7IN3E::set_pixel(size_t idx, uint8_t color_index)
{
    if (idx >= WIDTH * HEIGHT)
        return;
    auto color = this->get_color_value(color_index);
    if (idx % 2 == 0)
    {
        buffer[idx / 2] = (buffer[idx / 2] & 0x0F) | (color << 4);
    }
    else
    {
        buffer[idx / 2] = (buffer[idx / 2] & 0xF0) | color;
    }
}

void EPD_7IN3E::flush_buffer()
{
    pre_display();

    LOG("EPD_7IN3E::flush_buffer()");
    begin_write_buffer();
    for (auto i = 0; i < BUFFER_SIZE; i++)
    {
        write_buffer(buffer[i]);
    }
    end_write_buffer();

    post_display();
}

void EPD_7IN3E::write_debug_bars()
{
    pre_display();

    LOG("EPD_7IN3::write_debug_bars()");
    begin_write_buffer();
    for (auto row = 0; row < HEIGHT; row++)
    {
        for (auto col = 0; col < WIDTH; col += 2)
        {
            auto color = get_color_value((color_index_t)(row / (HEIGHT / COLOR_NUM)));
            // uint8_t color = row / (HEIGHT / 16);
            write_buffer((color << 4) | (color));
        }
    }
    end_write_buffer();

    post_display();
}
