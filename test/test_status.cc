#include "lest.hpp"
#include "../src/status.h"

const lest::test specification[] = {
  "Extract the last complete PJL value",
  [] {
    const std::string response = "DISPLAY=\"SLEEP\"\r\nFOODISPLAY=\"WRONG\"\r\n"
                                 "DISPLAY=\"REPLACE TONER\"   \r\n";
    EXPECT(pjl_value(response, "DISPLAY") == "REPLACE TONER");
  },

  "Ignore a missing PJL value",
  [] {
    EXPECT(pjl_value("CODE=10001\r\n", "DISPLAY").empty());
  },

  "Parse non-negative PJL numbers",
  [] {
    int value = -1;
    EXPECT(pjl_nonnegative_number("PAGECOUNT=1234\r\n", "PAGECOUNT", &value));
    EXPECT(value == 1234);
    EXPECT(!pjl_nonnegative_number("PAGECOUNT=-1\r\n", "PAGECOUNT", &value));
    EXPECT(!pjl_nonnegative_number("PAGECOUNT=12 pages\r\n", "PAGECOUNT", &value));
  },

  "Classify consumable states",
  [] {
    EXPECT(classify_hl1110_status(40010, "") == hl1110_status::toner_empty);
    EXPECT(classify_hl1110_status(40038, "") == hl1110_status::toner_low);
    EXPECT(classify_hl1110_status(-1, "Toner Low") == hl1110_status::toner_low);
    EXPECT(classify_hl1110_status(40130, "") == hl1110_status::drum_low);
    EXPECT(classify_hl1110_status(40129, "") == hl1110_status::drum_empty);
  },

  "Classify printer faults",
  [] {
    EXPECT(classify_hl1110_status(-1, "NO PAPER") == hl1110_status::media_empty);
    EXPECT(classify_hl1110_status(40022, "") == hl1110_status::media_jam);
    EXPECT(classify_hl1110_status(40021, "") == hl1110_status::cover_open);
    EXPECT(classify_hl1110_status(50003, "") == hl1110_status::service_needed);
  },

  "Classify informational states",
  [] {
    EXPECT(classify_hl1110_status(10002, "") == hl1110_status::offline);
    EXPECT(classify_hl1110_status(10003, "") == hl1110_status::warming_up);
    EXPECT(classify_hl1110_status(40000, "SLEEP") == hl1110_status::ready);
    EXPECT(classify_hl1110_status(10001, "READY") == hl1110_status::ready);
  },
};

int main() {
  return lest::run(specification);
}