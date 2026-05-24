#include "indicator.hpp"
#include "hardware_define.h"

void IIndicator::set_pin(uint8_t pin)
{
    this->pin = pin;
}

void IIndicator::set_state(enum IIndicator::State state)
{
    switch (state)
    {
    case Busy:
        this->set_busy();
        break;
    case Success:
        this->set_success();
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

  protected:
    CRGB leds[1];

    void show(CRGB color)
    {
        leds[0] = color;
        FastLED.show();
    }

    virtual void set_busy() {
        show(CRGB::Red);
    }

    virtual void set_success() {
        show(CRGB::Green);
    }
        
    virtual void set_wifi_connecting() {
        show(CRGB::Blue);
    }
        
    virtual void set_wifi_connected() {
        show(CRGB::Cyan);
    }
        
    virtual void set_image_fetching() {
        show(CRGB::Cyan1);
    }
        
    virtual void set_updating() {
        show(CRGB::Yellow);
    }
        
    virtual void set_sleep() {
        show(CRGB::Black);
    }

    virtual void set_idle() {
        show(CRGB(4, 0, 8));
    }

    virtual void set_error() {
        show(CRGB::OrangeRed);
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