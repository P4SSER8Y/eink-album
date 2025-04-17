#pragma once
#include <cstddef>
#include <cstdint>

class canvas_interface {
public:
    virtual void set_pixel(size_t x, size_t y, uint8_t color) = 0;
};
