#pragma once
#include <cstdint>
#include <utility>
#include <vector>

class EPD_7IN3E
{
  public:
    EPD_7IN3E(uint8_t sck, uint8_t mosi, uint8_t cs, uint8_t dc, uint8_t rst, uint8_t busy, uint8_t pwr, uint8_t led);
    EPD_7IN3E(const EPD_7IN3E &) = delete;
    ~EPD_7IN3E() = default;

    enum color_index_t
    {
        BLACK = 0,
        WHITE = 1,
        YELLOW = 2,
        RED = 3,
        BLUE = 4,
        GREEN = 5,
        COLOR_NUM,
    };
    static const size_t WIDTH = 800;
    static const size_t HEIGHT = 480;
    uint8_t buffer[WIDTH * HEIGHT / 2];

    void set_pixel(size_t x, size_t y, color_index_t color_index);
    void set_pixel(size_t idx, color_index_t color_index);

    void clear(color_index_t color_index);
    void flush_buffer();
    void write_debug_bars();

  private:
    uint8_t pin_sck;
    uint8_t pin_mosi;
    uint8_t pin_cs;
    uint8_t pin_dc;
    uint8_t pin_rst;
    uint8_t pin_busy;
    uint8_t pin_pwr;
    uint8_t pin_led;
    uint32_t write_cnt;

    using table_t = std::vector<std::pair<uint8_t, std::vector<uint8_t>>>;

    void send_command(uint8_t data);
    void send_data(uint8_t data);
    void write_table(const table_t &table);
    void begin_write_buffer();
    bool write_buffer(uint8_t data);
    void end_write_buffer();
    
    void init();
    void reset();
    bool is_busy();
    void wait_busy();
    void turn_on_display();
    void deep_sleep();
    void power_down();
    uint8_t get_color_value(uint8_t index);
    
    void pre_display();
    void post_display();
};
