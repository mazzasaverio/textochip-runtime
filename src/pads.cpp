#include "pads.h"

#include <cctype>
#include <cstdlib>

namespace {

// A decimal number spanning the whole of `s`, or -1.
int number(const std::string& s) {
  if (s.empty() || s.size() > 3) return -1;
  for (char c : s)
    if (!std::isdigit((unsigned char)c)) return -1;
  return std::atoi(s.c_str());
}

std::string upper(std::string s) {
  for (char& c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}

int esp32ByName(const std::string& n) {
  if (n.rfind("GPIO", 0) != 0) return -1;
  int g = number(n.substr(4));
  return g >= 0 && g <= 48 ? g : -1;
}

int nrfByName(const std::string& n) {
  // P<port>.<pin>, the pin two digits as the DK prints it ("P1.05")
  if (n.size() < 4 || n[0] != 'P' || n[2] != '.') return -1;
  int port = number(n.substr(1, 1));
  int pin = number(n.substr(3));
  if (port < 0 || port > 3 || pin < 0 || pin > 31) return -1;
  return port * 32 + pin;
}

int unoByName(const std::string& n) {
  if (n.size() < 2) return -1;
  int k = number(n.substr(1));
  if (n[0] == 'D') return k >= 0 && k <= 13 ? k : -1;
  if (n[0] == 'A') return k >= 0 && k <= 5 ? 14 + k : -1;
  return -1;
}

}  // namespace

namespace pads {

int byName(PadBoard board, const std::string& raw) {
  const std::string n = upper(raw);
  switch (board) {
    case PAD_ESP32S3: return esp32ByName(n);
    case PAD_NRF54LM20: return nrfByName(n);
    case PAD_UNOR4: return unoByName(n);
    case PAD_HOST: {
      // The host stands in for every board: it takes all three spellings.
      int p = esp32ByName(n);
      if (p < 0) p = nrfByName(n);
      if (p < 0) p = unoByName(n);
      return p;
    }
  }
  return -1;
}

std::string name(PadBoard board, int pad) {
  switch (board) {
    case PAD_NRF54LM20: {
      std::string pin = std::to_string(pad % 32);
      if (pin.size() < 2) pin = "0" + pin;
      return "P" + std::to_string(pad / 32) + "." + pin;
    }
    case PAD_UNOR4:
      return pad >= 14 ? "A" + std::to_string(pad - 14) : "D" + std::to_string(pad);
    default:
      return "GPIO" + std::to_string(pad);
  }
}

const char* reserved(PadBoard board, int pad) {
  switch (board) {
    case PAD_ESP32S3:
      // Mirrors textochip/lib/boards/esp32s3.json (`reserved`, `strap`).
      if (pad == 0) return "a strapping pin (boot mode)";
      if (pad == 45) return "a strapping pin (flash voltage)";
      if (pad == 19 || pad == 20) return "the USB port";
      if (pad >= 26 && pad <= 32) return "the flash";
      if (pad >= 33 && pad <= 37) return "the PSRAM";
      if (pad == 43 || pad == 44) return "the UART the board is flashed through";
      if (pad >= 15 && pad <= 17) return "the microphone (its driver claims it at boot)";
      return nullptr;
    case PAD_NRF54LM20:
      if (pad == 32 + 1 || pad == 32 + 2) return "shorted to ground on the DK";
      if (pad == 32 + 23 || pad == 32 + 14 || pad == 32 + 31)
        return "the microphone (its driver claims it at boot)";
      if (pad == 32 + 10 || pad == 32 + 3 || pad == 32 + 4 || pad == 96 + 4)
        return "the camera's SPI";
      if (pad == 32 + 11 || pad == 32 + 12) return "the ultrasonic sensor";
      if (pad >= 64 && pad <= 64 + 5)
        return "the external flash (not on the header by default)";
      return nullptr;
    case PAD_UNOR4:
      if (pad == 10 || pad == 11) return "the ultrasonic sensor";
      return nullptr;
    case PAD_HOST:
      return nullptr;
  }
  return nullptr;
}

int fixedPad(PadBoard board, ResKind kind) {
  // The overlays' PWM and ADC pads (zephyr/boards/*.overlay), as the IDE's
  // descriptors mark them `fixed`.
  switch (board) {
    case PAD_ESP32S3:
      return kind == RES_TONE ? 5 : kind == RES_SERVO ? 8 : kind == RES_ADC ? 9 : -1;
    case PAD_NRF54LM20:
      return kind == RES_TONE    ? 32 + 13
             : kind == RES_SERVO ? 96 + 1
             : kind == RES_ADC   ? 3
                                 : -1;
    case PAD_UNOR4:
      return kind == RES_TONE ? 5 : kind == RES_SERVO ? 6 : kind == RES_ADC ? 14 : -1;
    case PAD_HOST:
      return -1;
  }
  return -1;
}

int legacyPad(PadBoard board, int logical) {
  switch (board) {
    case PAD_NRF54LM20:
      // Phase 1 of the DK: the four LEDs and button A are the on-board ones.
      switch (logical) {
        case 4: return 32 + 22;   // red LED    -> LED0 (P1.22)
        case 2: return 32 + 25;   // yellow LED -> LED1 (P1.25)
        case 1: return 32 + 27;   // green LED  -> LED2 (P1.27)
        case 18: return 32 + 28;  // blue LED   -> LED3 (P1.28)
        case 5: return 32 + 13;   // buzzer     -> P1.13
        case 6: return 32 + 26;   // button A   -> Button 0 (P1.26)
        case 7: return 96 + 0;    // PIR        -> P3.00
        case 8: return 96 + 1;    // servo      -> P3.01
        case 9: return 3;         // analog in  -> P0.03
        case 10: return 96 + 2;   // motor L dir1 -> P3.02
        case 11: return 96 + 3;   // motor L dir2 -> P3.03
        case 12: return 32 + 7;   // motor L pwm  -> P1.07
        case 13: return 96 + 5;   // motor R dir1 -> P3.05
        case 14: return 32 + 5;   // motor R dir2 -> P1.05
        case 21: return 32 + 6;   // motor R pwm  -> P1.06 (pwm21 ch1, as the overlay)
        // P2.00-P2.05 are the DK's external flash, not on header P4 by
        // default (user guide v0.7.0, Table 4): line and obstacle sit on
        // PORT3's free pads instead (2026-10-15).
        case 38: return 96 + 7;   // line left    -> P3.07
        case 39: return 96 + 8;   // line center  -> P3.08
        case 40: return 96 + 9;   // line right   -> P3.09
        case 41: return 96 + 10;  // obstacle     -> P3.10
        // The relay, where the IDE wires it. It used to fall through to raw
        // 42 = P1.10, the camera's SPI clock (2026-10-13).
        case 42: return 4;        // relay        -> P0.04
        // The RGB LED's legs: no three free pads on this DK.
        case 47:
        case 3:
        case 46: return -1;
        default: return logical;  // unmapped: raw port*32+pin passthrough
      }
    case PAD_UNOR4:
      switch (logical) {
        case 1: return 2;    // green LED  -> D2
        case 2: return 3;    // yellow LED -> D3
        case 4: return 4;    // red LED    -> D4
        case 18: return 18;  // blue LED   -> A4
        case 5: return 5;    // buzzer     -> D5
        case 6: return 7;    // button A   -> D7
        case 7: return 8;    // PIR        -> D8
        case 8: return 6;    // servo      -> D6
        case 9: return 14;   // analog in  -> A0
        case 38: return 15;  // line left   -> A1
        case 39: return 16;  // line center -> A2
        case 40: return 17;  // line right  -> A3
        case 41: return 12;  // obstacle    -> D12
        case 42: return 9;   // relay       -> D9
        case 47: return 0;   // RGB LED R   -> D0
        case 3: return 1;    // RGB LED G   -> D1
        case 46: return 19;  // RGB LED B   -> A5
        default: return -1;  // not on this board
      }
    default:
      return logical;  // the ESP32-S3 and the host: the logical pin is the GPIO
  }
}

ResKind kindByName(const std::string& raw) {
  const std::string n = upper(raw);
  if (n == "DOUT") return RES_DOUT;
  if (n == "DIN") return RES_DIN;
  if (n == "TONE") return RES_TONE;
  if (n == "SERVO") return RES_SERVO;
  if (n == "ADC") return RES_ADC;
  return RES_UNKNOWN;
}

const char* kindName(ResKind kind) {
  switch (kind) {
    case RES_DOUT: return "DOUT";
    case RES_DIN: return "DIN";
    case RES_TONE: return "TONE";
    case RES_SERVO: return "SERVO";
    case RES_ADC: return "ADC";
    default: return "?";
  }
}

}  // namespace pads
