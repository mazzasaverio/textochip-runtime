// The CAPS handshake (SPEC.md §2): one line the IDE parses on connect to know
// which board, which build and contract, how long a program may be, and which
// primitives this firmware can drive. Build & run:
//   make test-caps
#include <cstdio>
#include <string>

#include "../src/isa.h"
#include "../src/runtime.h"
#include "../src/vm.h"

static int fail = 0;
static void expect(const char* what, bool ok) {
  printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) fail = 1;
}

int main(void) {
  const std::string caps = runtime::capsLine();
  printf("  %s\n", caps.c_str());
  expect("starts with CAPS", caps.rfind("CAPS ", 0) == 0);
  expect("names the build", caps.find(" build=") != std::string::npos);
  expect("names the contract version",
         caps.find(" contract=" + std::to_string(TEXTOCHIP_CONTRACT) + " ") !=
             std::string::npos);
  expect("names the program cap",
         caps.find(" max=" + std::to_string(MAX_PROGRAM) + " ") !=
             std::string::npos);
  expect("names the board", caps.find(" board=host ") != std::string::npos);
  expect("lists digital among the primitives",
         caps.find(" prims=digital") != std::string::npos);
  expect("one line", caps.find('\n') == std::string::npos);
  // Every field is key=value: the IDE skips keys it does not know.
  size_t pos = caps.find(' ') + 1;
  bool pairs = true;
  while (pos < caps.size()) {
    size_t end = caps.find(' ', pos);
    if (end == std::string::npos) end = caps.size();
    if (caps.substr(pos, end - pos).find('=') == std::string::npos) pairs = false;
    pos = end + 1;
  }
  expect("every field is key=value", pairs);
  printf(fail ? "FAIL: CAPS\n" : "OK: CAPS line is well-formed\n");
  return fail;
}
