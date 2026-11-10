// Pinbelegung der unterstützten Boards. Gewählt wird über das PlatformIO-Profil:
//   amoled-175c  Waveshare ESP32-S3-Touch-AMOLED-1.75C (rund, 466 × 466, Schaltplan Rev. 2026)
//   amoled-216   Waveshare ESP32-S3-Touch-AMOLED-2.16  (quadratisch, 480 × 480; Pins aus Waveshares Beispielen)
#pragma once

// Größe der Oberfläche (LCD_W × LCD_H) und Lage im Panel (PANEL_*): rund 466, eckig 480 (ganze Fläche,
// die Ecken zeigen auf dem Zifferblatt Akku, Wetter, Datum und was gerade läuft).

// Display CO5300 (QSPI)
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK 38
#define LCD_CS 12

// I2C: Touch CST92xx (0x5A), AXP2101 (0x34), QMI8658 (0x6B), ES8311 (0x18), ES7210 (0x40)
#define I2C_SDA 15
#define I2C_SCL 14
#define TP_INT 11

// Audio: ES8311 (Lautsprecher) und ES7210 (2 Mikrofone) teilen sich einen I2S-Bus
#define I2S_BCLK 9
#define I2S_WS 45
#define I2S_DOUT 8   // ESP → ES8311
#define I2S_DIN 10   // ES7210 → ESP
#define PA_EN 46     // Verstärker

// Tasten: BOOT direkt am GPIO, PWR am AXP2101 (PWRON, wird per I2C abgefragt)
#define BTN_BOOT 0
#define PWR_SENSE 3  // SYS_OUT laut Schaltplan, nur zur Diagnose

#if defined(BOARD_AMOLED_216)
#define BOARD_NAME "AMOLED 2.16"
#define BOARD_SQUARE 1
#define LCD_W 480
#define LCD_H 480
#define PANEL_W 480
#define PANEL_H 480
#define PANEL_X 0
#define PANEL_Y 0
#define LCD_COL_OFS 0
#define LCD_MADCTL 0xA0    // Ausrichtung wie in Waveshares Beispielen
#define LCD_RESET 39
#define TP_RST 40
#define EXTRA_RST 2        // manche Baureihen führen den Reset hierüber – wird beim Start mit gepulst
#define I2S_MCLK 42
#define IMU_INT1 21
#else
#define BOARD_NAME "AMOLED 1.75C"
#define BOARD_SQUARE 0
#define LCD_W 466
#define LCD_H 466
#define PANEL_W 466
#define PANEL_H 466
#define PANEL_X 0
#define PANEL_Y 0
#define LCD_COL_OFS 6
#define LCD_RESET 1
#define LCD_TE 13
#define TP_RST 2
#define I2S_MCLK 16
#define IMU_INT1 21
#endif
