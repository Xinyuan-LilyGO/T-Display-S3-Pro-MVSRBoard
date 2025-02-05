/*
 * @Description: T-Display-S3-Pro-MVSRBoard Vibration Motor Driver Example
 * @Author: LILYGO_L
 * @Date: 2024-05-06 18:37:15
 * @LastEditTime: 2025-02-05 14:09:00
 * @License: GPL 3.0
 */
#include <Arduino.h>
#include "pin_config.h"

void setup()
{
    Serial.begin(115200);
    Serial.println("Ciallo");

    pinMode(RT9080_EN, OUTPUT);
    digitalWrite(RT9080_EN, HIGH);

    // pinMode(VIBRATINO_MOTOR_PWM, OUTPUT);
    ledcAttachPin(VIBRATINO_MOTOR_PWM, 2);
    ledcSetup(2, 12000, 8);
    ledcWrite(2, 0);
}

void loop()
{
    for (int i = 0; i <= 255; i++)
    {
        ledcWrite(2, i);
        delay(10);
    }
    delay(1000);
    for (int i = 255; i > 0; i--)
    {
        ledcWrite(2, i);
        delay(10);
    }

    // digitalWrite(VIBRATINO_MOTOR_PWM, HIGH);
    // delay(1000);
    // digitalWrite(VIBRATINO_MOTOR_PWM, LOW);
    // delay(1000);
}