# Third party code and licenses

SavvyCAN's own source is MIT (see `LICENSE`). This file inventories the third
party code that lives in this repository and the third party libraries SavvyCAN
links against, because several of them are under other terms.

## Bundled in this repository

| Component | Files | Copyright | License | Built on |
|---|---|---|---|---|
| QCustomPlot 2.1.1 | `qcustomplot.{h,cpp}` | 2011-2022 Emanuel Eichhammer | **GPL-3.0-or-later** | all platforms |
| candle API | `connections/candle_api/` | 2016 Hubert Denkmair, 2026 Schildkroet | **LGPL-3.0-or-later** | Windows only |
| qmqtt | `mqtt/` | 2013 Ery Lee | BSD-3-Clause | all platforms |
| jsedit | `jsedit.{h,cpp}` | 2010-2011 Ariya Hidayat | BSD-3-Clause | all platforms |
| SimpleCrypt | `simplecrypt.{h,cpp}` | 2011 Andre Somers | BSD-3-Clause | all platforms |

License texts: `licenses/GPL-3.0.txt` and `licenses/LGPL-3.0.txt`. Those two
plus this file and `LICENSE` are what gets shipped in the release packages.
`connections/candle_api/LICENSE` is the same LGPL-3.0 text, kept inside the
vendored directory so that directory stays self contained. The BSD-3-Clause
terms are reproduced in the header comment of each file they cover.

LGPL-3.0 is not a standalone license, it is a set of additional permissions on
top of GPL-3.0, which is why both texts are needed even for the candle API
alone.

## Linked at runtime

- **Qt 5** — LGPL-3.0 as distributed by the Qt Company for open source use, which
  is what the official builds and the CI builds in `.github/workflows/build.yml`
  use. Linked dynamically, so LGPL-3.0 section 4d1 applies: the Qt libraries ship
  as separate replaceable shared libraries (`Qt5*.dll`, the frameworks inside the
  `.app`, or the libraries bundled into the AppImage). Qt sources are available
  from <https://download.qt.io/>.

## What this means for a distributed binary

QCustomPlot is GPL-3.0-or-later and is compiled into the SavvyCAN executable on
every platform. A built and distributed SavvyCAN binary is therefore a combined
work that has to be conveyed under GPL-3.0 terms, not under the MIT terms alone.

Nothing here conflicts. MIT and BSD-3-Clause are GPL compatible, and LGPL-3.0
code may be used in a GPL-3.0 work. The practical obligation is that the
complete corresponding source has to be available to anyone who receives a
binary, which it is, at <https://github.com/collin80/SavvyCAN> and at the fork
this build came from.

Using QCustomPlot under its commercial license instead would remove the
GPL-3.0 obligation. That is a decision for the project owner, not something this
file assumes.

## The candle API specifically

`connections/candle_api/` is the only LGPL-3.0 code in this tree, it is only
compiled into Windows builds, and it exists to talk to gs_usb class adapters
(candleLight, CANable, CANnectivity, cantact) over WinUSB. On Linux and macOS
those adapters are handled by the kernel gs_usb driver and appear as SocketCAN,
so no part of it is built there.

It is statically linked into `SavvyCAN.exe`, which puts it under LGPL-3.0
section 4d0 rather than 4d1: the corresponding application code has to be
conveyed in a form that lets a recipient relink against a modified candle API.
Publishing the complete buildable source of the application satisfies that, and
the terms it is published under (MIT for SavvyCAN's own code, GPL-3.0 for the
combined work) permit the recombination.

Keep those files separable. Do not copy code out of `connections/candle_api/`
into the MIT licensed parts of the tree. See `connections/candle_api/README.md`
for provenance and the local changes made to it.
