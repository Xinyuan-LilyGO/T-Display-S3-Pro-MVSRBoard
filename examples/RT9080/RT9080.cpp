/*
 * @Description: T-Display-S3-Pro-MVSRBoard RT9080 Test
 * @Author: LILYGO_L
 * @Date: 2024-05-06 18:37:15
 * @LastEditTime: 2024-11-27 14:13:23
 * @License: GPL 3.0
 */
#include <Arduino.h>
#include "pin_config.h"

void setup()
{
    Serial.begin(115200);
    Serial.println("Ciallo");

    pinMode(RT9080_EN, OUTPUT);
    digitalWrite(RT9080_EN, LOW);
}

void loop()
{
    digitalWrite(RT9080_EN, HIGH);
    delay(2000);
    digitalWrite(RT9080_EN, LOW);
    delay(2000);
}