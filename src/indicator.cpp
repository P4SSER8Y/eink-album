#include "indicator.hpp"
#include "hardware_define.h"

void IIndicator::set_pin(uint8_t pin)
{
    this->pin = pin;
}

void IIndicator::set_state(enum IIndicator::State state)
{
    will_set_state(state);
    switch (state)
    {
    case Busy:
        this->set_busy();
        break;
    case Success:
        this->set_success();
        break;
    case WIFI_Connecting:
        this->set_wifi_connecting();
        break;
    case WIFI_Connected:
        this->set_wifi_connected();
        break;
    case IMAGE_Fetching:
        this->set_image_fetching();
        break;
    case Updating:
        this->set_updating();
        break;
    case Sleep:
        this->set_sleep();
        break;
    case Idle:
        this->set_idle();
        break;
    case Error:
        this->set_error();
        break;
    }
}

#ifdef LED_WS2812_CNT
#include <FastLED.h>
class WS2812_Indicator : public IIndicator
{
  public:
    WS2812_Indicator() : IIndicator()
    {
        FastLED.addLeds<NEOPIXEL, PIN_LED>(leds, sizeof(leds) / sizeof(leds[0]));
        FastLED.setBrightness(5);
        leds[0] = CRGB::Black;
        FastLED.show();
    }
    ~WS2812_Indicator() = default;

    void tick() override
    {
        if (!_remote_active) return;
        if (_system_override) return;

        unsigned long now = millis();

        if (_remote_timeout_ms > 0 && now - _effect_start_ms >= _remote_timeout_ms) {
            _remote_active = false;
            show(_normal_color);
            return;
        }

        if (_remote_freq_hz == 0) {
            show(_remote_color);
            return;
        }

        uint16_t half_ms = (uint16_t)(500.0f / _remote_freq_hz);
        if (now - _last_toggle_ms >= half_ms) {
            _last_toggle_ms = now;
            _remote_led_on = !_remote_led_on;
            show(_remote_led_on ? _remote_color : CRGB::Black);
        }
    }

    void remote_effect(uint8_t r, uint8_t g, uint8_t b, float freq_hz, uint32_t timeout_s) override
    {
        _remote_active = true;
        _remote_color = CRGB(r, g, b);
        _remote_freq_hz = freq_hz;
        _remote_timeout_ms = timeout_s * 1000;
        _effect_start_ms = millis();
        _last_toggle_ms = millis();
        _remote_led_on = true;
        show(_remote_color);
    }

    void remote_effect_clear() override
    {
        _remote_active = false;
        show(_normal_color);
    }

    bool remote_effect_active() override
    {
        return _remote_active;
    }

  protected:
    CRGB leds[1];
    bool _remote_active = false;
    CRGB _remote_color;
    float _remote_freq_hz = 0;
    uint32_t _remote_timeout_ms = 0;
    bool _remote_led_on = false;
    unsigned long _last_toggle_ms = 0;
    unsigned long _effect_start_ms = 0;
    bool _system_override = false;
    CRGB _normal_color = CRGB::Black;

    void show(CRGB color)
    {
        leds[0] = color;
        FastLED.show();
    }

    void will_set_state(State st) override
    {
        if (st == Idle || st == Sleep)
            _system_override = false;
        else
            _system_override = true;
    }

    void set_busy() override {
        _normal_color = CRGB::Red;
        show(_normal_color);
    }

    void set_success() override {
        _normal_color = CRGB::Green;
        show(_normal_color);
    }

    void set_wifi_connecting() override {
        _normal_color = CRGB::Blue;
        show(_normal_color);
    }

    void set_wifi_connected() override {
        _normal_color = CRGB::Cyan;
        show(_normal_color);
    }

    void set_image_fetching() override {
        _normal_color = CRGB::Cyan1;
        show(_normal_color);
    }

    void set_updating() override {
        _normal_color = CRGB::Yellow;
        show(_normal_color);
    }

    void set_sleep() override {
        _normal_color = CRGB::Black;
        _system_override = false;
        if (_remote_active) {
            show(_remote_color);
            _remote_led_on = true;
            _last_toggle_ms = millis();
        } else {
            show(_normal_color);
        }
    }

    void set_idle() override {
        _normal_color = CRGB(4, 0, 8);
        _system_override = false;
        if (_remote_active) {
            show(_remote_color);
            _remote_led_on = true;
            _last_toggle_ms = millis();
        } else {
            show(_normal_color);
        }
    }

    void set_error() override {
        _normal_color = CRGB::OrangeRed;
        show(_normal_color);
    }
};
IIndicator *Indicator = nullptr;
#endif

void init_indicator()
{
#ifdef LED_WS2812_CNT
    Indicator = new WS2812_Indicator();
#endif
}