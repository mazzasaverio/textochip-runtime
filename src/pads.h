#pragma once
#include <string>

// PADS: everything this firmware knows about a board's pins, in one portable,
// host-tested place (contract 3, SPEC.md §1). The HAL only says which board it
// is (hal::padBoard()) and drives a pad number; the names, the reserved pads,
// the pads its peripherals are wired to and the contract-2 pin map live here.
//
// A pad number is the board's own GPIO index:
//   ESP32-S3   GPIO<n>                  -> n
//   nRF54L     P<port>.<pin>            -> port * 32 + pin
//   UNO R4     D<n> / A<n> (the header) -> n / 14 + n (the overlay's tc-gpios)
// The names are the ones the IDE's board descriptors print as `physical`
// (textochip/lib/boards/<id>.json), so a program carries a pad by name and
// neither side keeps a table of the other's numbers.

enum PadBoard { PAD_HOST, PAD_ESP32S3, PAD_NRF54LM20, PAD_UNOR4 };

// What a contract-3 resource is for (SPEC.md §1, `RES`).
enum ResKind { RES_DOUT, RES_DIN, RES_TONE, RES_SERVO, RES_ADC, RES_UNKNOWN };

namespace pads {

// The pad a name stands for on this board, or -1 when the name is not one of
// its pads ("GPIO99", "P9.01", a UNO name on an ESP32).
int byName(PadBoard board, const std::string& name);

// The name of a pad, for messages ("GPIO19", "P1.22", "D4").
std::string name(PadBoard board, int pad);

// Why a program may not use this pad (the flash, the USB, a strapping pin, a
// pad shorted to ground, one a driver claims at boot), or nullptr.
const char* reserved(PadBoard board, int pad);

// The pad a fixed peripheral is wired to in the board's overlay: the buzzer's
// PWM (RES_TONE), the servo's (RES_SERVO), the ADC channel (RES_ADC). -1 when
// any pad goes (the host) or the kind is a plain GPIO one.
int fixedPad(PadBoard board, ResKind kind);

// Contract 2: the pad a logical pin number lands on, or -1 for none. The pin
// map the HAL used to keep (map_pin), kept for programs saved before contract 3.
int legacyPad(PadBoard board, int logical);

// "DOUT" -> RES_DOUT; RES_UNKNOWN for anything else.
ResKind kindByName(const std::string& name);
const char* kindName(ResKind kind);

}  // namespace pads
