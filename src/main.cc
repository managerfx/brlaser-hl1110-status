// This file is part of the brlaser printer driver.
//
// Copyright 2013 Peter De Wachter
//
// brlaser is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// brlaser is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with brlaser.  If not, see <http://www.gnu.org/licenses/>.

#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <cups/cups.h>
#include <cups/raster.h>
#include <cups/sidechannel.h>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <array>
#include <map>
#include "config.h"
#include "job.h"
#include "debug.h"
#include "status.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif


namespace {

cups_raster_t *ras;
volatile sig_atomic_t interrupted = 0;


bool query_hl1110(const char *command, double timeout, std::string *response) {
  fputs("\033%-12345X@PJL\r\n@PJL ", stdout);
  fputs(command, stdout);
  fputs("\r\n\033%-12345X", stdout);
  fflush(stdout);

  char side_data[16];
  int side_length = sizeof(side_data);
  if (cupsSideChannelDoRequest(CUPS_SC_CMD_DRAIN_OUTPUT, side_data,
                               &side_length, timeout) != CUPS_SC_STATUS_OK) {
    return false;
  }

  fputc('\033', stdout);
  fflush(stdout);
  side_length = sizeof(side_data);
  if (cupsSideChannelDoRequest(CUPS_SC_CMD_DRAIN_OUTPUT, side_data,
                               &side_length, timeout) != CUPS_SC_STATUS_OK) {
    return false;
  }

  char buffer[1024];
  for (int attempt = 0; attempt < 3; ++attempt) {
    const ssize_t bytes = cupsBackChannelRead(buffer, sizeof(buffer),
                                               attempt == 0 ? timeout : 0.25);
    if (bytes <= 0) {
      break;
    }
    response->append(buffer, static_cast<size_t>(bytes));
  }
  return true;
}


void report_hl1110_status() {
  const char *device_uri = getenv("DEVICE_URI");
  if (!device_uri || !strstr(device_uri, "usb://Brother/HL-1110")) {
    return;
  }

  std::string response;
  const bool first_status = query_hl1110("INFO STATUS", 3.0, &response);
  const bool page_count_status = query_hl1110("INFO PAGECOUNT", 2.0, &response);
  const bool final_status = query_hl1110("INFO STATUS", 2.0, &response);
  if (!first_status && !page_count_status && !final_status) {
    fprintf(stderr, "WARNING: " PACKAGE ": Could not drain output before status query.\n");
    return;
  }

  int code = -1;
  int page_count = -1;
  const bool has_code = pjl_nonnegative_number(response, "CODE", &code);
  const bool has_page_count = pjl_nonnegative_number(response, "PAGECOUNT", &page_count);
  const std::string display = pjl_value(response, "DISPLAY");

  if (!has_code && display.empty()) {
    fprintf(stderr, "WARNING: " PACKAGE ": Printer did not return PJL status.\n");
    return;
  }

  fputs("STATE: -toner-low toner-empty marker-supply-low marker-supply-empty "
        "media-empty media-jam cover-open opc-near-eol opc-life-over "
        "com.brlaser.offline com.brlaser.service-needed\n", stderr);
  fputs("ATTR: marker-colors=#000000,none marker-high-levels=100,100 "
        "marker-low-levels=5,5 marker-names='\"Black Toner\"','\"Drum\"' "
        "marker-types=toner,opc marker-levels=-2,-2 marker-message=''\n", stderr);

  const char *status_message = "Ready";
  switch (classify_hl1110_status(code, display)) {
    case hl1110_status::offline:
      fputs("STATE: +com.brlaser.offline\n", stderr);
      status_message = "Offline";
      break;
    case hl1110_status::warming_up:
      status_message = "Warming up";
      break;
    case hl1110_status::toner_empty:
      fputs("STATE: +toner-empty marker-supply-empty\n", stderr);
      fputs("ATTR: marker-levels=0,-2\n", stderr);
      status_message = "Replace toner";
      break;
    case hl1110_status::toner_low:
      fputs("STATE: +toner-low marker-supply-low\n", stderr);
      status_message = "Toner low";
      break;
    case hl1110_status::media_empty:
      fputs("STATE: +media-empty\n", stderr);
      status_message = "No paper";
      break;
    case hl1110_status::media_jam:
      fputs("STATE: +media-jam\n", stderr);
      status_message = "Paper jam";
      break;
    case hl1110_status::cover_open:
      fputs("STATE: +cover-open\n", stderr);
      status_message = "Cover open";
      break;
    case hl1110_status::drum_low:
      fputs("STATE: +opc-near-eol\n", stderr);
      status_message = "Drum life low";
      break;
    case hl1110_status::drum_empty:
      fputs("STATE: +opc-life-over\n", stderr);
      fputs("ATTR: marker-levels=-2,0\n", stderr);
      status_message = "Replace drum";
      break;
    case hl1110_status::service_needed:
      fputs("STATE: +com.brlaser.service-needed\n", stderr);
      status_message = "Service required";
      break;
    case hl1110_status::ready:
      break;
  }

  std::string marker_message = status_message;
  if (has_code) {
    marker_message += "; PJL " + std::to_string(code);
  }
  if (has_page_count) {
    marker_message += "; pages printed: " + std::to_string(page_count);
  }
  fprintf(stderr, "ATTR: marker-message='%s'\n", marker_message.c_str());

  fprintf(stderr, "INFO: " PACKAGE ": PJL status CODE=%d DISPLAY=%s PAGECOUNT=%d\n",
          code, display.empty() ? "unknown" : display.c_str(), page_count);
}

void sigterm_handler(int sig) {
  interrupted = 1;
}


bool next_line(std::vector<uint8_t> &buf) {
  if (interrupted) {
    return false;
  }
  return cupsRasterReadPixels(ras, buf.data(), buf.size()) == buf.size();
}


bool plain_ascii_string(const char *str) {
  bool result = true;
  for (; result && *str; str++) {
    result = *str >= 32 && *str <= 126;
  }
  return result;
}

std::string ascii_job_name(const char *job_id, const char *job_user, const char *job_name) {
  std::array<const char *, 3> parts = {{
    job_id,
    job_user,
    job_name
  }};
  std::string result;
  for (const char *part : parts) {
    if (*part && plain_ascii_string(part)) {
      if (!result.empty()) {
        result += '/';
      }
      result += part;
    }
  }
  if (result.empty()) {
    result = "brlaser";
  }
  const int max_size = 79;
  if (result.size() > max_size) {
    result.resize(max_size);
  }
  return result;
}

page_params build_page_params(const cups_page_header2_t &header) {
  static const std::array<std::string, 6> sources = {{
    "AUTO", "T1", "T2", "T3", "MP", "MANUAL"
  }};
  static const std::map<std::string, std::string> sizes = {
    { "A4", "A4" },
    { "A5", "A5" },
    { "A6", "A6" },
    { "B5", "B5" },
    { "B6", "B6" },
    { "EnvC5", "C5" },
    { "EnvMonarch", "MONARCH" },
    { "EnvPRC5", "DL" },
    { "EnvDL", "DL" },
    { "Executive", "EXECUTIVE" },
    { "Legal", "LEGAL" },
    { "Letter", "LETTER" }
  };

  page_params p = { };
  p.num_copies = header.NumCopies;
  p.resolution = header.HWResolution[0];
  p.economode = header.cupsInteger[10];
  p.mediatype = header.MediaType;
  p.duplex = header.Duplex;

  if (header.MediaPosition < sources.size())
    p.sourcetray = sources[header.MediaPosition];
  else
    p.sourcetray = sources[0];

  auto size_it = sizes.find(header.cupsPageSizeName);
  if (size_it != sizes.end())
    p.papersize = size_it->second;
  else
    p.papersize = "A4";

  return p;
}

}  // namespace


int main(int argc, char *argv[]) {
  fprintf(stderr, "INFO: %s version %s\n", PACKAGE, VERSION);

  if (argc != 6 && argc != 7) {
      fprintf(stderr, "ERROR: rastertobrlaser job-id user title copies options [file]\n");
      fprintf(stderr, "INFO: This program is a CUPS filter. It is not intended to be run manually.\n");
      return 1;
  }
  const char *job_id = argv[1];
  const char *job_user = argv[2];
  const char *job_name = argv[3];
  // const int job_copies = atoi(argv[4]);
  // const char *job_options = argv[5];
  const char *job_filename = argv[6];
  // const char *job_charset = getenv("CHARSET");

  signal(SIGTERM, sigterm_handler);
  signal(SIGPIPE, SIG_IGN);

  int fd = STDIN_FILENO;
  if (job_filename) {
    fd = open(job_filename, O_RDONLY | O_BINARY);
    if (fd < 0) {
      fprintf(stderr, "ERROR: " PACKAGE ": Unable to open raster file\n");
      return 1;
    }
  }

#ifdef __OpenBSD__
  if (pledge("stdio", nullptr) != 0) {
    fprintf(stderr, "ERROR: " PACKAGE ": pledge failed\n");
    return 1;
  }
#endif

  ras = cupsRasterOpen(fd, CUPS_RASTER_READ);
  if (!ras) {
    fprintf(stderr, "DEBUG: " PACKAGE ": Cannot read raster data. Most likely an earlier filter in the pipeline failed.\n");
    return 1;
  }

  {
    job job(stdout, ascii_job_name(job_id, job_user, job_name));
    cups_page_header2_t header;
    while (!interrupted && cupsRasterReadHeader2(ras, &header)) {
      if (header.cupsBitsPerPixel != 1
          || header.cupsBitsPerColor != 1
          || header.cupsNumColors != 1
          || header.cupsBytesPerLine > 10000) {
        fprintf(stderr, "ERROR: " PACKAGE ": Page %d: Bogus raster data.\n", job.pages() + 1);
        dump_page_header(header);
        return 1;
      }
      if (job.pages() == 0) {
        fprintf(stderr, "DEBUG: " PACKAGE ": Page header of first page\n");
        dump_page_header(header);
      }
      job.encode_page(build_page_params(header),
                      header.cupsHeight,
                      header.cupsBytesPerLine,
                      next_line);
      fprintf(stderr, "PAGE: %d %d\n", job.pages(), header.NumCopies);
    }

    if (job.pages() == 0) {
      fprintf(stderr, "ERROR: " PACKAGE ": No pages were found.\n");
    }
  }

  fflush(stdout);
  if (ferror(stdout)) {
    fprintf(stderr, "DEBUG: " PACKAGE ": Could not write print data. Most likely the CUPS backend failed.\n");
    return 1;
  }
  report_hl1110_status();
  return 0;
}
