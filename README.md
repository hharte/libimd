# libimd - ImageDisk Library (Cross-Platform)

[![Coverity](https://scan.coverity.com/projects/31720/badge.svg)](https://scan.coverity.com/projects/hharte-libimd)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/af9ec85e59ec4bf4a25531df74603041)](https://app.codacy.com/gh/hharte/libimd/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade)

## Introduction

This project provides a cross-platform C library, **libimd**, for reading, writing, and manipulating ImageDisk (`.IMD`) floppy disk image files. The ImageDisk format and original tools were created by Dave Dunfield to archive diskette images, particularly for classic computers, allowing for long-term storage and transfer via the internet.

The original MS-DOS version and further information can be found at [Dave's Old Computers](http://dunfield.classiccmp.org/img/). This library aims to provide the core functionality for modern operating systems.

## Core Libraries

The functionality is primarily encapsulated in the following static libraries:

* **`libimd`** (`libimd.c`, `libimd.h`): This is the fundamental library providing low-level functions for `.IMD` file operations. It handles reading and writing of IMD file headers, comment blocks, and track data including sector maps and sector data records. It supports various data rates and encoding modes (FM/MFM).
  * Defines structures like `ImdTrackInfo` for holding track details (mode, cylinder, head, sector count, sector size, maps, and data) and `ImdHeaderInfo` for the file header.
  * Provides functions such as `imd_read_file_header`, `imd_read_comment_block`, `imd_load_track`, `imd_write_track_imd`, `imd_write_track_bin`, `imd_get_sector_size`, `imd_alloc_track_data`, and `imd_free_track_data`.
  * Includes constants for sector sizes (128 to 8192 bytes), modes, and sector data record types (e.g., Normal, Compressed, Unavailable, Error flags).

* **`libimdf`** (`libimdf.c`, `libimdf.h`): An in-memory ImageDisk file library built upon `libimd`. It provides higher-level functions to open, access, and modify IMD image files by maintaining the entire image structure in memory.
  * Defines an opaque `ImdImageFile` structure to manage the in-memory image.
  * Offers functions like `imdf_open`, `imdf_close`, `imdf_get_header_info`, `imdf_get_comment`, `imdf_get_num_tracks`, `imdf_get_track_info`, `imdf_read_sector`, and `imdf_write_sector`.
  * Manages write protection and geometry limits.

* **`libimdchk`** (`libimdchk.c`, `libimdchk.h`): A library for performing consistency checks on `.IMD` files.
  * Defines structures `ImdChkOptions` and `ImdChkResults` for managing check parameters and storing outcomes.
  * Provides `imdchk_check_file` to validate aspects like header, comment termination, track readability, duplicate sector IDs, and invalid sector flags.

## Image File Format (.IMD)

The `.IMD` file format consists of:

1. **ASCII Header**: Starts with "IMD v.vv: dd/mm/yyyy hh:mm:ss" (e.g., "IMD 1.18: 25/04/2024 15:30:00").
2. **ASCII Comment Block**: Unlimited size, terminated by an `0x1A` (EOF) character.
3. **Track Data**: A sequence of records for each track on the disk. Each track record includes:
    * **Mode Value (1 byte)**: Indicates data transfer rate and density (FM/MFM at 250/300/500 kbps).
    * **Cylinder (1 byte)**: Physical cylinder number.
    * **Head (1 byte)**: Physical head number (0 or 1). Upper bits indicate presence of optional Cylinder/Head maps.
    * **Number of Sectors (1 byte)**: Sectors in this track.
    * **Sector Size Code (1 byte)**: Code for sector size (0=128B, 1=256B, ..., 6=8192B). A value of 0xFF indicates variable sector sizes within the track.
    * **Sector Numbering Map (smap)**: `num_sectors` bytes, defining the logical ID for each physical sector.
    * **Sector Cylinder Map (cmap)** (optional): `num_sectors` bytes, if head bit 7 is set. Contains logical cylinder ID for each sector if different from physical.
    * **Sector Head Map (hmap)** (optional): `num_sectors` bytes, if head bit 6 is set. Contains logical head ID for each sector if different from physical.
    * **Sector Data Records**: One record per sector, indicating data status and the data itself:
        * `0x00`: Sector data unavailable.
        * `0x01`: Normal data follows.
        * `0x02 xx`: Compressed data; all bytes in sector have value `xx`.
        * `0x03` to `0x08`: Variations indicating Deleted Address Marks (DAM) and/or data errors, for normal or compressed data.

## Building

This project uses CMake as the build system. The `CMakeLists.txt` file defines the following libraries:

* `libimd` (STATIC from `libimd.c`, `libimd_utils.c`)
* `libimdf` (STATIC from `libimdf.c`, depends on `libimd`)
* `libimdchk` (STATIC from `libimdchk.c`, depends on `libimd`)

**Prerequisites:**

* A C compiler (GCC, Clang, MSVC, etc.)
* CMake (version 3.24 or later specified)

**Basic Build Steps (from project root):**
```bash
mkdir build
cd build
cmake ..
cmake --build .
```
This will compile the static libraries.

## License

The original ImageDisk package by Dave Dunfield is provided with a free license for non-commercial use. The `.IMD` file format specification was placed into the public domain by its creator. This cross-platform library (`libimd` and related components) is available at [www.github.com/hharte/libimd](https://www.github.com/hharte/libimd). Specific licensing for this port is an MIT license detailed in the `LICENSE` file within the repository.

## Copyright

* Original ImageDisk: Copyright 2005-2023 Dave Dunfield. All rights reserved.
* This cross-platform library (`libimd`, `libimdf`, `libimdchk`): Copyright (c) 2025, Howard M. Harte.