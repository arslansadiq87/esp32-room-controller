#include "StatusLed.h"

StatusLed& StatusLed::getInstance() {
    static StatusLed instance;
    return instance;
}

StatusLed::StatusLed() {
    leds[0] = CRGB::Black;
}

void StatusLed::begin() {
    FastLED.addLeds<WS2812B, PIN_STATUS_LED, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(maxBrightness);
    leds[0] = CRGB::Blue;
    FastLED.show();
    setState(LedState::PULSING_BLUE);
    Serial.println("[LED] FastLED WS2812B initialized on GPIO11");
}

void StatusLed::setState(LedState state) {
    currentState = state;
    if (state == LedState::OFF) {
        leds[0] = CRGB::Black;
        FastLED.show();
    } else if (state == LedState::SOLID_GREEN) {
        leds[0] = CRGB(0, 180, 50); // Clean emerald green
        FastLED.show();
    }
}

void StatusLed::setColor(CRGB color) {
    currentState = LedState::MANUAL;
    leds[0] = color;
    FastLED.show();
}

void StatusLed::setBrightness(uint8_t brightness) {
    maxBrightness = brightness;
    FastLED.setBrightness(maxBrightness);
    FastLED.show();
}

void StatusLed::update() {
    uint32_t now = millis();

    switch (currentState) {
        case LedState::PULSING_BLUE: {
            // Breathing Cyan/Blue pulse (20ms step)
            if (now - lastAnimationTick >= 20) {
                lastAnimationTick = now;
                breathCounter += (breathDirection * 3);
                if (breathCounter >= 240) {
                    breathCounter = 240;
                    breathDirection = -1;
                } else if (breathCounter <= 15) {
                    breathCounter = 15;
                    breathDirection = 1;
                }

                // Cyan-Blue blend
                uint8_t r = 0;
                uint8_t g = breathCounter / 3;
                uint8_t b = breathCounter;
                leds[0] = CRGB(r, g, b);
                FastLED.show();
            }
            break;
        }

        case LedState::SOLID_GREEN: {
            // Subtle slow alive heartbeat (low brightness modulation)
            if (now - lastAnimationTick >= 50) {
                lastAnimationTick = now;
                static uint8_t pulse = 0;
                pulse = (pulse + 1) % 100;
                uint8_t greenLevel = 130 + (sin(pulse * 0.0628) * 40);
                leds[0] = CRGB(0, greenLevel, 30);
                FastLED.show();
            }
            break;
        }

        case LedState::BLINKING_RED: {
            // Flash red every 250ms
            if (now - lastAnimationTick >= 250) {
                lastAnimationTick = now;
                isBlinkOn = !isBlinkOn;
                leds[0] = isBlinkOn ? CRGB(255, 0, 0) : CRGB::Black;
                FastLED.show();
            }
            break;
        }

        case LedState::WARNING_ORANGE: {
            // Flash orange/amber every 400ms
            if (now - lastAnimationTick >= 400) {
                lastAnimationTick = now;
                isBlinkOn = !isBlinkOn;
                leds[0] = isBlinkOn ? CRGB(255, 120, 0) : CRGB(40, 20, 0);
                FastLED.show();
            }
            break;
        }

        case LedState::OFF:
        case LedState::MANUAL:
        default:
            break;
    }
}
