/*Based on the Adafruit Neopixel library, which has been heavily modified to support a short WS281x chain with RGB/GRB color order for lowest possible CPU usage*/

#include "Adafruit_NeoPixel.h"

extern "C" void espShow(uint8_t pin, uint8_t *pixels, uint32_t numBytes);

Adafruit_NeoPixel::Adafruit_NeoPixel(int16_t p, uint16_t count)
    : numPixels(count), numBytes(count * BYTES_PER_PIXEL), pixels(NULL) {
  updateLength();
  setPin(p);
}

void Adafruit_NeoPixel::updateLength(void) {
  free(pixels);
  pixels = (uint8_t *)malloc(numBytes);
  if (pixels) memset(pixels, 0, numBytes);
}

void Adafruit_NeoPixel::setPin(int16_t p) {
  if (pin >= 0) pinMode(pin, INPUT);
  pin = p;
  if ((p >= 0)) {
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
}

void Adafruit_NeoPixel::setColorOrder(led_color_order o) {
  color_order = o;
}

void Adafruit_NeoPixel::show(void) {
  if (!pixels) return;
  espShow(pin, pixels, numBytes);
}

void Adafruit_NeoPixel::setPixelColor(uint16_t index, uint32_t c) {
  if (!pixels || index >= numPixels) return;
  uint8_t *p = pixels + index * BYTES_PER_PIXEL;
  uint8_t r = (uint8_t)(c >> 16), g = (uint8_t)(c >> 8), b = (uint8_t)c;
  if (color_order == led_color_order::GRB) {
    p[rOffset] = g;
    p[gOffset] = r;
    p[bOffset] = b;
  } else {
    p[rOffset] = r;
    p[gOffset] = g;
    p[bOffset] = b;
  }
}
