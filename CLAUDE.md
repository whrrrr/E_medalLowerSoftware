# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an embedded firmware project for the HC32L110C6UA microcontroller (ARM Cortex-M0+) that drives an e-paper display (EPD) with external SPI flash storage (W25Q32). The system receives image data via wireless communication (E104 module) and displays it on a 4.2" tri-color e-paper display.

**Target MCU:** HC32L110C6UA (Cortex-M0+, 256KB Flash, 128KB RAM)
**External Flash:** W25Q32 (4MB SPI Flash)
**Display:** GDEY042Z98 (4.2" tri-color EPD, 400x300, Black/White/Red)
**Wireless:** E104 Bluetooth module

## Build System

This project uses **Keil µVision 5** (ARM MDK) as the build system.

### Build Commands

**Build the project:**
```bash
# Open the project in Keil µVision
# File: HC32L110C6UA.uvprojx
# Then use Project → Build Target (F7)
```

**Note:** There is no command-line Makefile. All builds must be done through Keil µVision IDE or via `UV4.exe` command-line interface if available on the system.

**Output directory:** `./output/release/`
**Binary output:** `HC32L110C6UA.hex` and `HC32L110C6UA.axf`

### Include Paths
- `./common` - MCU system files and base types
- `./source` - Application source code
- `./driver/inc` - Peripheral driver headers

## Architecture

### Directory Structure

```
├── common/              # MCU system initialization and base types
│   ├── hc32l110.h      # MCU register definitions
│   ├── base_types.h    # Common type definitions
│   └── system_hc32l110c6ua.c  # System initialization
├── driver/             # Hardware abstraction layer (HAL)
│   ├── inc/           # Driver headers
│   └── src/           # Driver implementations
├── source/            # Application layer
│   ├── main.c         # Main application entry
│   ├── epd.c/h        # E-paper display driver
│   ├── w25q32.c/h     # External flash driver
│   ├── flash_manager.c/h  # Flash wear-leveling system
│   ├── e104.c/h       # Bluetooth module interface
│   ├── uart_interface.c/h  # UART communication
│   ├── drawWithFlash.c/h   # Drawing operations
│   └── image_transfer.c/h  # Image protocol handling
└── HC32L110C6UA.uvprojx  # Keil project file
```

### Key Subsystems

#### 1. Flash Manager (Wear-Leveling System)
**Files:** `flash_manager.c/h`, `flash_config.h`

The flash manager implements a two-segment wear-leveling system for the external W25Q32 flash:

- **Dual Segment Architecture:** Flash is divided into two 2MB segments (Segment 0: 0x000000, Segment 1: 0x200000)
- **Active/Backup Pattern:** One segment is active for writes, the other is backup
- **Garbage Collection:** Automatic GC when active segment is full, copies valid data to backup segment
- **Data Types:**
  - Generic data pages (magic: 0xA0)
  - BW image headers (magic: 0xA1) and data (magic: 0xA3)
  - Red image headers (magic: 0xA2) and data (magic: 0xA4)
- **Page Structure:** 256-byte pages with CRC32 protection
- **Entry Mapping:** Maintains in-RAM tables for fast lookup (MAX_DATA_ENTRIES=16, MAX_IMAGE_ENTRIES=8)

**Key Functions:**
- `FM_init()` - Initialize flash manager, scan segments
- `FM_writeData()` / `FM_readData()` - Generic data storage
- `FM_writeImageHeader()` / `FM_readImage()` - Image data storage
- `FM_forceGarbageCollect()` - Manual GC trigger

#### 2. E-Paper Display System
**Files:** `epd.c/h`, `drawWithFlash.c/h`

- **Display Model:** GDEY042Z98 (400x300 pixels, tri-color)
- **Frame Buffer:** 160x150 effective resolution (WIDTH_420=160, HEIGHT_420=150)
- **Image Types:**
  - `IMAGE_BW_AND_RED` - Full tri-color display
  - `IMAGE_BW` - Black/white only
  - `IMAGE_RED` - Red/white only
- **Data Source:** Reads image data from external flash via flash manager
- **Refresh Strategy:** Full screen refresh with separate BW and Red layers

**Key Functions:**
- `EPD_initGDEY042Z98()` - Initialize display controller
- `EPD_WhiteScreenGDEY042Z98UsingFlashDate(imageType_t type, uint8_t slotId)` - Display image from flash

#### 3. Wireless Communication (E104)
**Files:** `e104.c/h`

Interface to E104 Bluetooth module for wireless image transfer:
- **Link Detection:** `E104_getLinkState()` returns connection status
- **Power Modes:** Sleep mode for low power, wake mode for data transfer
- **Command Execution:** `E104_executeCommand()` processes received commands

#### 4. UART Interface
**Files:** `uart_interface.c/h`

- **UART0:** Debug output via `UARTIF_uartPrintf()`
- **LPUART:** Low-power UART for E104 communication
- **Pass-through:** `UARTIF_passThrough()` handles bidirectional data flow

### Timer System

**TIM0 (Base Timer):** 5ms tick timer for system timing
- Configured in `timInit()` at main.c:175
- Interrupt handler: `Bt0Int()` at main.c:87
- Used for UART pass-through polling and link state checking

### Image Data Protocol

Images are stored as paginated data in external flash:
- **Slot-based Storage:** Each image has a slot ID (0-7)
- **Frame-based Pages:** Images split into frames (0-60), each frame = 248 bytes payload
- **Header Pages:** Separate header page marks start of each image
- **CRC Protection:** Each page has CRC32 validation

### Critical Patterns

1. **Flash Write Pattern:**
   ```c
   // Initialize flash manager first
   FM_init();

   // Write image header
   FM_writeImageHeader(MAGIC_BW_IMAGE_HEADER, slot_id);

   // Write image data pages
   FM_writeData(MAGIC_BW_IMAGE_DATA, frame_num, data, size);
   ```

2. **Display Update Pattern:**
   ```c
   // Initialize display
   EPD_initGDEY042Z98();

   // Display image from flash
   EPD_WhiteScreenGDEY042Z98UsingFlashDate(IMAGE_BW_AND_RED, slot_id);
   ```

3. **Debug Output:**
   ```c
   UARTIF_uartPrintf(0, "Debug message: %d\n", value);
   ```

### Hardware Configuration

**SPI Flash (W25Q32):**
- CS Pin: PA4
- Interface: SPI hardware peripheral
- Capacity: 4MB (0x000000 - 0x3FFFFF)

**E-Paper Display:**
- Communication: SPI/parallel (check epd.c for exact pins)
- Resolution: 400x300 physical, 160x150 in code (likely downsampled)

**Timers:**
- TIM0: 5ms system tick
- LPT: Low-power timer for wake-up (commented out in main.c)

## Development Notes

### Working with Flash Manager

- Always call `FM_init()` before any flash operations
- Flash manager automatically handles wear-leveling and GC
- Use appropriate magic numbers for different data types (see flash_config.h)
- Image frames must be written sequentially (0 to MAX_FRAME_NUM)

### Debugging

- UART0 is configured for debug output at 9600 baud (check uart_interface.c for exact config)
- Use `UARTIF_uartPrintf()` for formatted debug output
- Check W25Q32 chip ID with `W25Q32_ReadID()` - should return manufacturer ID

### Memory Constraints

- **MCU Flash:** 256KB - limited space for code
- **MCU RAM:** 128KB - manage buffers carefully
- **External Flash:** 4MB - used for image storage
- **Display Buffer:** Each full frame = 160*150/8 = 3KB (BW) + 3KB (Red) = 6KB total

### Common Tasks

**To add a new image slot:**
1. Ensure slot_id < MAX_IMAGE_ENTRIES (8)
2. Write header: `FM_writeImageHeader(magic, slot_id)`
3. Write frames sequentially
4. Display: `EPD_WhiteScreenGDEY042Z98UsingFlashDate(type, slot_id)`

**To test flash operations:**
- Uncomment test functions in main.c (lines 435-466)
- Functions like `testFlashManagerReadAndWrite()` demonstrate usage patterns

**To change display resolution:**
- Modify WIDTH_420 and HEIGHT_420 in epd.h
- Update EPD_ARRAY calculation
- Verify frame count in flash_config.h (MAX_FRAME_NUM)
