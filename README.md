# brlaser-hl1110-status

`brlaser-hl1110-status` is a fork of [pdewacht/brlaser](https://github.com/pdewacht/brlaser)
focused on the Brother HL-1110. It preserves the original driver's raster
conversion and adds bidirectional printer status queries, CUPS/IPP status
reporting, and a safe administrative command for resetting counters.

This project is not official Brother software.

## What It Adds

The original brlaser driver converts CUPS jobs into the format understood by
Brother laser printers that do not support PCL or PostScript. It prints
correctly on the HL-1110, but it does not query the printer after a job or
publish toner, drum, or page-count information.

This fork adds:

- a PJL `INFO STATUS` query after every job;
- parsing of the numeric `CODE` and the `DISPLAY` message;
- a PJL `INFO PAGECOUNT` query;
- handling for delayed USB responses specific to the HL-1110;
- toner and drum status through standard CUPS `marker-*` attributes;
- IPP reasons for low/empty toner, drum conditions, paper, jams, and covers;
- classification of hardware faults and service conditions;
- a separate `brlaser-hl1110-reset` utility for the PJL
  `EXECUTE REVIVALRESET` command;
- automated tests for PJL parsing, status mapping, and USB framing.

## Differences from brlaser

| Feature | Original brlaser | This fork |
| --- | --- | --- |
| Brother raster printing | Yes | Yes |
| PJL status through the CUPS back-channel | No | Yes, for the USB HL-1110 |
| Toner exposed through IPP | No | Low, empty, or unknown |
| Drum exposed through IPP | No | Near end, end of life, or unknown |
| PJL diagnostic code | No | Yes |
| Printed-page counter | No | Yes |
| Toner/drum/counter reset | No | Separate administrative utility |

The additional logic is enabled only when `DEVICE_URI` identifies a USB
Brother HL-1110. Other printers supported by brlaser continue to use the normal
print path without additional PJL queries.

## Data Published by CUPS

After a job completes successfully, CUPS exposes two markers:

- `Black Toner`, type `toner`;
- `Drum`, type `opc`.

The HL-1110 does not report a reliable percentage. The normal value is
therefore `-2`, which means unknown according to CUPS. The driver does not
invent percentages. When the printer reports that a component is exhausted,
the corresponding level changes to `0`.

`marker-message` also contains a human-readable summary, for example:

```text
Ready; PJL 10001; pages printed: 4
```

Status is updated after a print job. This project does not implement continuous
polling while the printer is idle.

## Requirements

- Linux with CUPS and the USB backend;
- a Brother HL-1110 connected through USB;
- CMake and a C++11 compiler;
- CUPS development headers (`libcups2-dev` and `libcupsimage2-dev` on Debian);
- Python 3 for the reset utility;
- Avahi if the queue should be published through AirPrint.

On the HL-1110 used for development, the reliable mode is A4 at 300 dpi.

## Building and Testing

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Installation:

```bash
sudo cmake --install build
sudo systemctl restart cups
```

The installation copies:

- `rastertobrlaser` to the CUPS filter directory;
- the `brlaser.drv` driver file to the CUPS data directory;
- `brlaser-hl1110-reset` to `${CMAKE_INSTALL_PREFIX}/sbin`.

Before replacing a system driver, keep a backup of the original filter. A
distribution update of `printer-driver-brlaser` may overwrite the custom
binary.

## CUPS Configuration

The queue must use the brlaser HL-1110 model and the `rastertobrlaser` filter.
An example USB URI is:

```text
usb://Brother/HL-1110%20series?serial=XXXXXXXXX
```

To inspect the published attributes:

```bash
ipptool -tv ipp://localhost:631/printers/Brother_HL1110 \
  /usr/share/cups/ipptool/get-printer-attributes.test
```

AirPrint is provided by CUPS IPP sharing and Avahi; it is not implemented by
the driver itself.

## Resetting the HL-1110

Reset functionality is intentionally kept separate from the print filter, so
it cannot be triggered accidentally during a job.

Non-destructive readiness check:

```bash
brlaser-hl1110-reset --check
```

Reset with interactive confirmation:

```bash
sudo brlaser-hl1110-reset
```

Non-interactive use with explicit command-line confirmation:

```bash
sudo brlaser-hl1110-reset --yes
```

The command:

1. verifies through sysfs that the device is a Brother HL-1110;
2. checks that the CUPS queue has no pending jobs;
3. requires explicit confirmation;
4. temporarily disables the queue;
5. sends `EXECUTE REVIVALRESET`;
6. waits for the printer to become available again;
7. reads the status and page counter;
8. re-enables the queue even if an error occurs.

> **Warning:** `REVIVALRESET` is irreversible and resets the toner, drum, and
> printed-page counters.

## Limitations

- Additional PJL monitoring is specific to the USB Brother HL-1110.
- Status is updated at the end of print jobs, not continuously.
- Toner and drum percentages are not available.
- Resetting requires direct root access to the USB device.
- The availability and wording of some PJL codes depend on printer firmware.

## Acknowledgements and Reference Projects

This project was made possible by work already contributed by the open source
community. Special thanks go to:

- [Peter De Wachter and pdewacht/brlaser](https://github.com/pdewacht/brlaser),
  which provides the raster driver, HL-1110 support, and the overall project
  structure;
- [Filis Futsarov and filisko/hl1110-utility-tool](https://github.com/filisko/hl1110-utility-tool),
  an essential reference for HL-1110 PJL commands, the page counter, status-code
  interpretation, and `REVIVALRESET` behavior;
- [OpenPrinting/cups](https://github.com/OpenPrinting/cups), which provides CUPS
  and the back-channel, side-channel, `STATE:`, and `ATTR:` APIs used by this
  integration.

This extension would not have been possible without these projects and their
public documentation.

## License

This project retains the license and notices from the original brlaser driver.
See [COPYING](COPYING) for the full GNU General Public License text.

Copyright © 2013 Peter De Wachter and contributors to the original project.
