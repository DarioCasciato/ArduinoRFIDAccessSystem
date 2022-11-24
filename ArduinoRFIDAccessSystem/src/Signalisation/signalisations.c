#include "signalisations.h"
#include "../lib/Arduino_SK6812/SK6812.h"

/*Colors*/
RGBW color_red = {100, 0, 0, 0}; // Values from 0-255
RGBW color_green = {0, 100, 0, 0};
RGBW color_off = {0, 0, 0, 0};

void SignalPositive(SK6812 *LED)
{
  tone(SIGNALIZER_BUZZER, 3000);
  LED->set_rgbw(0, color_green);
  LED->sync();
  delay(150);
  LED->set_rgbw(0, color_off);
  LED->sync();
  noTone(SIGNALIZER_BUZZER);
}