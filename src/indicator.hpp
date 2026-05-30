#include <cstdint>

class IIndicator {
public:
  enum State
  {
      Busy,
      Success,
      WIFI_Connecting,
      WIFI_Connected,
      IMAGE_Fetching,
      Updating,
      Sleep,
      Idle,
      Error,
  };
  IIndicator() = default;
  ~IIndicator() = default;

  void set_pin(uint8_t pin);
  void set_state(enum State);

  virtual void tick() = 0;
  virtual void remote_effect(uint8_t r, uint8_t g, uint8_t b,
                             float freq_hz, uint32_t timeout_s) = 0;
  virtual void remote_effect_clear() = 0;
  virtual bool remote_effect_active() = 0;

protected:
  uint8_t pin;

  virtual void will_set_state(State st) {}

  virtual void set_busy() = 0;
  virtual void set_success() = 0;
  virtual void set_wifi_connecting() = 0;
  virtual void set_wifi_connected() = 0;
  virtual void set_image_fetching() = 0;
  virtual void set_updating() = 0;
  virtual void set_sleep() = 0;
  virtual void set_idle() = 0;
  virtual void set_error() = 0;
};

extern IIndicator *Indicator;

void init_indicator();
