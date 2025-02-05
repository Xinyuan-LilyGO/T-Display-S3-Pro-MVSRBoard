/*
 * @Description:
 *      This is an example of a serial port simulation file manager reading the content of an SD card.
 * @Author: LILYGO_L
 * @Date: 2023-09-27 14:50:04
 * @LastEditTime: 2025-02-05 14:39:45
 * @License: GPL 3.0
 */
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "pin_config.h"
#include "Arduino_Explorer.h"
#include "Audio.h"

static int8_t Volume_Value = 0;
static bool Play_Music_Flag = false;

std::vector<String> current_directory = {"/"};

Arduino_Explorer AE_Class;

Audio audio;

void Current_Directory_Print(void)
{
    String current_directory_merge;
    for (int i = 0; i < current_directory.size(); i++)
    {
        if (current_directory[i] == "/") // 判断尾部是否有文件页操作符号
        {
            current_directory_merge = current_directory[i];
        }
        else
        {
            current_directory_merge = current_directory_merge + "/" + current_directory[i];
        }
    }

    Serial.print("Current directory: ");
    Serial.println(current_directory_merge);
    Serial.println();
}

void File_Directory_Serial_Output(std::vector<String> folder_name,
                                  std::vector<Arduino_Explorer::File_Manager> file_manager)
{
    for (int i = 0; i < folder_name.size(); i++)
    {
        Serial.print("  Folder: ");
        Serial.println(folder_name[i]);
    }
    for (int i = 0; i < file_manager.size(); i++)
    {
        Serial.print("  File: ");
        Serial.print(file_manager[i].file_name);
        Serial.print("   ");
        Serial.printf("%d Kb\n", file_manager[i].file_size / 1024);
    }
    Serial.printf("\n");
}

void File_Root_Initialization(void)
{
    std::vector<String> root_directory = {"/"};
    std::vector<String> folder_name;
    std::vector<Arduino_Explorer::File_Manager> file_manager_struct;

    AE_Class.File_Directory_Refresh(&SD, root_directory, &folder_name, &file_manager_struct);
    AE_Class.File_Directory_Sort(Arduino_Explorer::File_Manager_Sort_Order, &folder_name, &file_manager_struct);
    Current_Directory_Print();
    File_Directory_Serial_Output(folder_name, file_manager_struct);
}

void setup()
{
    Serial.begin(115200);

    pinMode(RT9080_EN, OUTPUT);
    digitalWrite(RT9080_EN, HIGH);

    pinMode(MAX98357A_SD_MODE, OUTPUT);
    digitalWrite(MAX98357A_SD_MODE, HIGH);

    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS); // SPI boots

    while (!SD.begin(SD_CS, SPI, 80000000))
    {
        Serial.printf("SD card initialization failed !\n");
        delay(1000);
    }

    if (audio.setPinout(MAX98357A_BCLK, MAX98357A_LRCLK, MAX98357A_DATA) != true)
    {
        Serial.println("Music driver initialization failed");
        delay(1000);
    }

    Volume_Value = 3;
    audio.setVolume(Volume_Value); // 0...21,Volume setting
                                   // audio.setBalance(-16);

    File_Root_Initialization();
}

void loop()
{
    if (Serial.available() > 0)
    {
        char Serial_Command[100] = {'\0'};
        std::vector<String> folder_name;
        std::vector<Arduino_Explorer::File_Manager> file_manager_s;

        uint8_t uart_data_count = 0;

        while (Serial.available() > 0)
        {
            Serial_Command[uart_data_count] = Serial.read();
            uart_data_count++;
            // Serial.write(Serial_Command,sizeof(Serial_Command));
        }

        AE_Class.File_Directory_Refresh(&SD, current_directory, &folder_name, &file_manager_s);
        AE_Class.File_Directory_Sort(Arduino_Explorer::File_Manager_Sort_Order, &folder_name, &file_manager_s);

        if (Serial_Command[0] == '/')
        {
            if (Serial_Command[1] == 'h' && Serial_Command[2] == 'e' &&
                Serial_Command[3] == 'l' && Serial_Command[4] == 'p')
            {
                Serial.printf("\n\n    ----------Arduino Explorer Help----------\n\n");
                Serial.printf("    * [/cd "
                              "xxx"
                              "]   ->   [Return to the previous level directory] *\n");
                Serial.printf("    * [/music play "
                              "xxx"
                              "]   ->   [Play the specified music in the current directory] *\n");
                Serial.printf("    * [/music volume  "
                              "xxx"
                              "]   ->   [Adjust the current music volume size(0-21)] *\n");
                Serial.printf("\n");
                Serial.printf("    ----------Arduino Explorer Help----------\n\n");
            }
            else if (Serial_Command[1] == 'c' && Serial_Command[2] == 'd')
            {
                if (Serial_Command[4] != 0) // 如果cd到文件夹名字不为空
                {
                    char temp_buf[100];
                    memmove(temp_buf, &Serial_Command[4], sizeof(Serial_Command) - 4);

                    if (std::find(folder_name.begin(), folder_name.end(),
                                  (const char *)temp_buf) != folder_name.end()) // 如果文件夹里面有这个文件夹
                    {
                        current_directory.push_back(temp_buf);

                        folder_name.clear();
                        file_manager_s.clear();

                        AE_Class.File_Directory_Refresh(&SD, current_directory, &folder_name, &file_manager_s);
                        AE_Class.File_Directory_Sort(Arduino_Explorer::File_Manager_Sort_Order, &folder_name, &file_manager_s);
                        Current_Directory_Print();
                        File_Directory_Serial_Output(folder_name, file_manager_s);
                    }
                    else
                    {
                        Serial.printf("cmd[error]->The folder does not exist\n");
                    }
                }
                else
                {
                    if (current_directory.back() == "/")
                    {
                        Serial.printf("cmd[error]->This directory is already a root directory and cannot be returned!\n");
                    }
                    else
                    {
                        current_directory.pop_back();

                        folder_name.clear();
                        file_manager_s.clear();

                        AE_Class.File_Directory_Refresh(&SD, current_directory, &folder_name, &file_manager_s);
                        AE_Class.File_Directory_Sort(Arduino_Explorer::File_Manager_Sort_Order, &folder_name, &file_manager_s);
                        Current_Directory_Print();
                        File_Directory_Serial_Output(folder_name, file_manager_s);
                    }
                }
            }
            else if (Serial_Command[1] == 'm' && Serial_Command[2] == 'u' &&
                     Serial_Command[3] == 's' && Serial_Command[4] == 'i' &&
                     Serial_Command[5] == 'c')
            {
                if (Serial_Command[7] == 'p' && Serial_Command[8] == 'l' &&
                    Serial_Command[9] == 'a' && Serial_Command[10] == 'y')
                {
                    std::vector<String> temp_file_name;
                    std::vector<Arduino_Explorer::File_Manager> temp_file_manager_s;
                    char temp_buf[1000];
                    memmove(temp_buf, &Serial_Command[12], sizeof(Serial_Command) - 12);

                    // 搜索文件夹后缀为"".mp3"的文件
                    AE_Class.File_Directory_Suffix_Scan(&SD, current_directory, ".mp3", &temp_file_manager_s);

                    if (temp_file_manager_s.size() != 0) // 如果有音乐文件的话
                    {
                        for (int i = 0; i < temp_file_manager_s.size(); i++)
                        {
                            temp_file_name.push_back(temp_file_manager_s[i].file_name);
                        }

                        if (std::find(temp_file_name.begin(), temp_file_name.end(),
                                      (const char *)temp_buf) != temp_file_name.end()) // 如果能找到这个文件名
                        {
                            Serial.printf("cmd[info]->Attempting to play music: %s\n", temp_buf);
                            String temp_target_directory; // 执行目录操作的目标路径
                            for (int i = 0; i < current_directory.size(); i++)
                            {
                                if (current_directory[i] == "/") // 判断尾部是否有文件页操作符号
                                {
                                    temp_target_directory = current_directory[i];
                                }
                                else
                                {
                                    temp_target_directory = temp_target_directory + "/" + current_directory[i];
                                }
                            }
                            temp_target_directory = temp_target_directory + "/" + temp_buf;
                            audio.connecttoSD(temp_target_directory.c_str());
                            Play_Music_Flag = true;
                        }
                        else
                        {
                            Serial.printf("cmd[error]->This MP3 file does not exist\n");
                        }
                    }
                    else
                    {
                        Serial.printf("cmd[error]->This MP3 file does not exist\n");
                    }
                }
                else if (Serial_Command[7] == 'v' && Serial_Command[8] == 'o' &&
                         Serial_Command[9] == 'l' && Serial_Command[10] == 'u' &&
                         Serial_Command[11] == 'm' && Serial_Command[12] == 'e')
                {
                    char temp_buf[100];
                    memmove(temp_buf, &Serial_Command[14], sizeof(Serial_Command) - 14);

                    audio.setVolume(atoi(temp_buf));
                    Serial.printf("cmd[info]->Music volume set successfully \ncmd[info]->Current volume: %d\n", audio.getVolume());
                }
                else
                {
                    Serial.printf("cmd[error]->Unknown Command\nEnter [/help] To View Existing Instructions\n");
                }
            }
            else
            {
                Serial.printf("cmd[error]->Unknown Command\nEnter [/help] To View Existing Instructions\n");
            }
        }
        else
        {
            Serial.printf("cmd[error]->Unknown Command\nEnter [/help] To View Existing Instructions\n");
        }
        // delay(3000);
    }
    if (Play_Music_Flag == true)
    {
        audio.loop();
    }
}

// optional
void audio_info(const char *info)
{
    Serial.print("info        ");
    Serial.println(info);
}
void audio_id3data(const char *info)
{ // id3 metadata
    Serial.print("id3data     ");
    Serial.println(info);
}
void audio_eof_mp3(const char *info)
{ // end of file
    Serial.print("eof_mp3     ");
    Serial.println(info);
}
void audio_showstation(const char *info)
{
    Serial.print("station     ");
    Serial.println(info);
}
void audio_showstreamtitle(const char *info)
{
    Serial.print("streamtitle ");
    Serial.println(info);
}
void audio_bitrate(const char *info)
{
    Serial.print("bitrate     ");
    Serial.println(info);
}
void audio_commercial(const char *info)
{ // duration in sec
    Serial.print("commercial  ");
    Serial.println(info);
}
void audio_icyurl(const char *info)
{ // homepage
    Serial.print("icyurl      ");
    Serial.println(info);
}
void audio_lasthost(const char *info)
{ // stream URL played
    Serial.print("lasthost    ");
    Serial.println(info);
}
