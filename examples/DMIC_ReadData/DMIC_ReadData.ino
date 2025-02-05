/*
 * @Description:
            T-Display-S3-Pro-MVSRBoard DMIC series microphone testing program
        Send left and right channel signal data on the serial port (value range between 40000 and -40000)
 * @Author: LILYGO_L
 * @Date: 2023-12-20 16:24:06
 * @LastEditTime: 2024-12-09 17:59:28
 * @License: GPL 3.0
 */
#include "Arduino_DriveBus_Library.h"
#include "pin_config.h"

#define IIS_SAMPLE_RATE 44100 // 采样速率
#define IIS_DATA_BIT 16       // 数据位数

#ifdef T_Display_S3_Pro_MVSRBoard_V1_0
std::shared_ptr<Arduino_IIS_DriveBus> IIS_Bus =
    std::make_shared<Arduino_HWIIS>(I2S_NUM_0, MSM261_BCLK, MSM261_WS, MSM261_DATA);
#elif defined T_Display_S3_Pro_MVSRBoard_V1_1
std::shared_ptr<Arduino_IIS_DriveBus> IIS_Bus =
    std::make_shared<Arduino_HWIIS>(I2S_NUM_0, -1, MP34DT05TR_LRCLK, MP34DT05TR_DATA);
#else
#error "Unknown macro definition. Please select the correct macro definition."
#endif

std::unique_ptr<Arduino_IIS> IIS(new Arduino_MEMS(IIS_Bus));

short IIS_Read_Buff[100];

void setup()
{
    Serial.begin(115200);

    pinMode(RT9080_EN, OUTPUT);
    digitalWrite(RT9080_EN, HIGH);

#ifdef T_Display_S3_Pro_MVSRBoard_V1_0
    pinMode(MSM261_EN, OUTPUT);
    digitalWrite(MSM261_EN, HIGH);

    while (IIS->begin(Arduino_IIS_DriveBus::Device_Data_Mode::DATA_IN, I2S_MODE_MASTER,
                      IIS_SAMPLE_RATE, IIS_DATA_BIT) == false)
    {
        Serial.println("MSM261 initialization fail");
        delay(2000);
    }
    Serial.println("MSM261 initialization successfully");
#elif defined T_Display_S3_Pro_MVSRBoard_V1_1
    pinMode(MP34DT05TR_EN, OUTPUT);
    digitalWrite(MP34DT05TR_EN, LOW);

    while (IIS->begin(Arduino_IIS_DriveBus::Device_Data_Mode::DATA_IN, I2S_MODE_PDM,
                      IIS_SAMPLE_RATE, IIS_DATA_BIT) == false)
    {
        Serial.println("MP34DT05TR initialization fail");
        delay(2000);
    }
    Serial.println("MP34DT05TR initialization successfully");
#else
#error "Unknown macro definition. Please select the correct macro definition."
#endif
}

void loop()
{
    if (IIS->IIS_Read_Data(IIS_Read_Buff, sizeof(IIS_Read_Buff)) == true)
    {
        // 输出左右声道数据
        Serial.printf("Left: %d\n", IIS_Read_Buff[0]);
        Serial.printf("Right: %d\n", IIS_Read_Buff[1]);

        // Arduino
        // Serial.println(IIS_Read_Buff[0]);
        // Serial.print(",");
        // Serial.print(IIS_Read_Buff[1]);

        // for (int i = 0; i < 25; i++)
        // {
        //     Serial.printf("debug[%d]: %d\n", i, IIS_Read_Buff[i]);
        // }
    }
    else
    {
        Serial.printf("Failed to read iis data");
    }

    delay(50);
}
