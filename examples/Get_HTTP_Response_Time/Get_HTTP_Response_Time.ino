/*
 * @Description: Get_HTTP_Response_Time test
 * @Author: LILYGO_L
 * @Date: 2024-06-08 14:55:10
 * @LastEditTime: 2025-02-05 13:59:42
 * @License: GPL 3.0
 */
#include <WiFi.h>
#include <HTTPClient.h>

#define WIFI_SSID "xinyuandianzi"
#define WIFI_PASSWORD "AA15994823428"
// #define WIFI_SSID "LilyGo-AABB"
// #define WIFI_PASSWORD "xinyuandianzi"

// 百度首页URL
const char *url = "http://www.baidu.com";

void setup()
{
    Serial.begin(115200);

    // 连接到Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connection successful");

    Serial.printf("10 download Baidu homepage\n\n");

    for (int i = 1; i < 11; i++)
    {
        // 初始化HTTP客户端
        HTTPClient http;
        http.setTimeout(5000); // 设置超时时间为5秒

        // 开始测量时间
        size_t startTime = millis();
        // 发起GET请求
        http.begin(url);
        int httpCode = http.GET();
        // 计算响应时间
        size_t endTime = millis();
        size_t duration = endTime - startTime;
        // 检查请求是否成功
        // Serial.printf("%d", httpCode);
        switch (httpCode)
        {
        case HTTP_CODE_OK:
            // 打印响应时间
            Serial.printf("[%d]Response time: %d ms\n", i, duration);
            // 获取响应包大小
            Serial.printf("Response packet size: %d byte\n\n", http.getSize());

            // 可以选择打印出响应体（网页内容）
            // String payload = http.getString();
            // Serial.println("Baidu homepage content:");
            // Serial.println(payload);
            break;

        default:
            Serial.printf("[%d]Request was aborted\n", i);
            break;
        }
        // 关闭连接
        http.end();
        delay(500);
    }
}

void loop()
{
    // 此处不执行任何操作
}