#ifndef STATUS_H
#define STATUS_H

#include <string>

enum class hl1110_status {
  ready,
  offline,
  warming_up,
  toner_low,
  toner_empty,
  media_empty,
  media_jam,
  cover_open,
  drum_low,
  drum_empty,
  service_needed,
};

std::string pjl_value(const std::string &response, const char *name);
bool pjl_nonnegative_number(const std::string &response, const char *name, int *value);
hl1110_status classify_hl1110_status(int code, const std::string &display);

#endif