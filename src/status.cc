#include "status.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cstdlib>

std::string pjl_value(const std::string &response, const char *name) {
  const std::string prefix = std::string(name) + "=";
  std::string value;
  std::string::size_type search_from = 0;

  while (true) {
    const std::string::size_type start = response.find(prefix, search_from);
    if (start == std::string::npos) {
      break;
    }
    search_from = start + prefix.size();
    if (start != 0 && response[start - 1] != '\r' && response[start - 1] != '\n') {
      continue;
    }

    const std::string::size_type value_end = response.find_first_of("\r\n", search_from);
    value = response.substr(search_from, value_end - search_from);
  }

  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value = value.substr(1, value.size() - 2);
  }
  return value;
}

bool pjl_nonnegative_number(const std::string &response, const char *name, int *value) {
  const std::string text = pjl_value(response, name);
  if (text.empty()) {
    return false;
  }

  char *end = nullptr;
  errno = 0;
  const long number = std::strtol(text.c_str(), &end, 10);
  if (errno != 0 || *end != '\0' || number < 0 || number > INT_MAX) {
    return false;
  }
  *value = static_cast<int>(number);
  return true;
}

hl1110_status classify_hl1110_status(int code, const std::string &display) {
  switch (code) {
    case 10002:
      return hl1110_status::offline;
    case 10003:
      return hl1110_status::warming_up;
    case 10006:
    case 40038:
      return hl1110_status::toner_low;
    case 40010:
      return hl1110_status::toner_empty;
    case 40021:
      return hl1110_status::cover_open;
    case 40022:
    case 40119:
      return hl1110_status::media_jam;
    case 40128:
    case 40129:
    case 40142:
      return hl1110_status::drum_empty;
    case 40130:
      return hl1110_status::drum_low;
    default:
      if ((code >= 40050 && code <= 40078) ||
          (code >= 50000 && code <= 50599)) {
        return hl1110_status::service_needed;
      }
      break;
  }

  std::string normalized = display;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return std::toupper(c); });

  if (normalized.find("REPLACE TONER") != std::string::npos ||
      normalized.find("INSTALL TONER") != std::string::npos) {
    return hl1110_status::toner_empty;
  }
  if (normalized.find("TONER LOW") != std::string::npos) {
    return hl1110_status::toner_low;
  }
  if (normalized.find("NO PAPER") != std::string::npos) {
    return hl1110_status::media_empty;
  }
  if (normalized.find("JAM") != std::string::npos) {
    return hl1110_status::media_jam;
  }
  if (normalized.find("COVER") != std::string::npos) {
    return hl1110_status::cover_open;
  }
  if (normalized.find("DRUM LIFE LOW") != std::string::npos) {
    return hl1110_status::drum_low;
  }
  if (normalized.find("DRUM") != std::string::npos) {
    return hl1110_status::drum_empty;
  }
  return hl1110_status::ready;
}