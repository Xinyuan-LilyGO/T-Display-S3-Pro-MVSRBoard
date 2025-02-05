/*
 * @Description: Sleep
 * @Author: LILYGO_L
 * @Date: 2024-11-07 10:02:24
 * @LastEditTime: 2025-02-05 13:57:53
 * @License: GPL 3.0
 */
#include "Arduino.h"
#include "pin_config.h"
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"

#define SLEEP_WAKE_UP_INT GPIO_NUM_0

static size_t CycleTime = 0;

Arduino_DataBus *bus = new Arduino_HWSPI(
    LCD_DC /* DC */, LCD_CS /* CS */, LCD_SCLK /* SCK */, LCD_MOSI /* MOSI */, LCD_MISO /* MISO */);

Arduino_GFX *gfx = new Arduino_ST7796(
    bus, LCD_RST /* RST */, 0 /* rotation */, true /* IPS */,
    LCD_WIDTH /* width */, LCD_HEIGHT /* height */,
    49 /* col offset 1 */, 0 /* row offset 1 */, 0 /* col_offset2 */, 0 /* row_offset2 */);

std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus =
    std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

void Arduino_IIC_Touch_Interrupt(void);

std::unique_ptr<Arduino_IIC> CST226SE(new Arduino_CST2xxSE(IIC_Bus, CST226SE_DEVICE_ADDRESS,
                                                           TOUCH_RST, TOUCH_INT, Arduino_IIC_Touch_Interrupt));

void Arduino_IIC_Touch_Interrupt(void)
{
    CST226SE->IIC_Interrupt_Flag = true;
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Ciallo");

    pinMode(SLEEP_WAKE_UP_INT, INPUT_PULLUP);

    // ledcAttachPin(LCD_BL, 1);
    // ledcSetup(1, 2000, 8);
    // ledcWrite(1, 255);

    // while (CST226SE->begin() == false)
    // {
    //     Serial.println("CST226SE initialization fail");
    //     delay(2000);
    // }
    // Serial.println("CST226SE initialization successfully");

    // gfx->begin();
    // gfx->fillScreen(WHITE);

    // gfx->setCursor(10, 100);
    // gfx->setTextSize(2);
    // gfx->setTextColor(RED);
    // gfx->println("Enter deep sleep in 5 seconds");
    Serial.println("Enter deep sleep in 5 seconds");

    CycleTime = millis() + 5000;
}

void loop()
{
    if (millis() > CycleTime)
    {
        delay(300);

        // gfx->displayOff();
        // ledcWrite(1, 0);

        // CST226SE->IIC_Write_Device_State(CST226SE->Arduino_IIC_Touch::Device::TOUCH_DEVICE_SLEEP_MODE,
        //                                  CST226SE->Arduino_IIC_Touch::Device_State::TOUCH_DEVICE_ON);

        Serial.println("Enter deep sleep");
        gpio_hold_en(SLEEP_WAKE_UP_INT);
        esp_sleep_enable_ext0_wakeup(SLEEP_WAKE_UP_INT, LOW);
        esp_deep_sleep_start();
    }
}
