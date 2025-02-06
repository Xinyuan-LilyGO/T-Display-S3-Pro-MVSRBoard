/*
 * @Description: None
 * @version: V1.0.0
 * @Author: None
 * @Date: 2023-08-16 14:24:03
 * @LastEditors: Please set LastEditors
 * @LastEditTime: 2025-02-06 09:48:59
 * @License: GPL 3.0
 */
#pragma once

// ST7796
#define LCD_WIDTH 222
#define LCD_HEIGHT 480
#define LCD_BL 48
#define LCD_MOSI 17
#define LCD_MISO 8
#define LCD_SCLK 18
#define LCD_CS 39
#define LCD_DC 9
#define LCD_RST 47

// CST226SE
#define TOUCH_RST 13
#define TOUCH_INT 21

// SY6970
#define SY6970_SDA 5
#define SY6970_SCL 6
#define SY6970_Address 0x6A
#define SY6970_INT 21

// IIC
#define IIC_SDA 5
#define IIC_SCL 6

// SD
#define SD_CS 14
#define SD_MISO 8
#define SD_MOSI 17
#define SD_SCLK 18

// MAX98357A
#define MAX98357A_BCLK 4
#define MAX98357A_LRCLK 15
#define MAX98357A_DATA 11
#define MAX98357A_SD_MODE 41

// Vibration Motor
#define VIBRATINO_MOTOR_PWM 45

// PCF85063
#define PCF85063_IIC_SDA 5
#define PCF85063_IIC_SCL 6
#define PCF85063_INT 7

// RT9080
#define RT9080_EN 42

// #define T_Display_S3_Pro_MVSRBoard_V1_0
#define T_Display_S3_Pro_MVSRBoard_V1_1

#ifdef T_Display_S3_Pro_MVSRBoard_V1_0

// // AW8624
// #define AW8624_TRIG_INT 40
// #define AW8624_SDA 5
// #define AW8624_SCL 6

// MSM261
#define MSM261_WS 10
#define MSM261_BCLK 1
#define MSM261_DOUT 2
#define MSM261_EN 3

#endif

#ifdef T_Display_S3_Pro_MVSRBoard_V1_1

// MP34DT05TR
#define MP34DT05TR_LRCLK 1
#define MP34DT05TR_DATA 2
#define MP34DT05TR_EN 3

#endif
