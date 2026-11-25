#pragma once
#include <Arduino_GFX_Library.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include "FactoryDevice.h"

namespace FactorySettingsUI {
enum class Page { List, Brightness, Volume, Battery, Zone };
constexpr int ListTop = 98, RowHeight = 66;
constexpr uint16_t Ink = RGB565(23,25,29), Muted = RGB565(117,120,129);
constexpr uint16_t Card = RGB565(246,246,248), Line = RGB565(225,226,230), Blue = RGB565(0,112,240);
struct State {
    char clock[6] = "--:--", zone[20] = "UTC+08:00";
    const char *wifi = "Not connected";
    int brightness = 100, volume = 70;
    FactoryDevice::Battery battery;
};
inline void text(Arduino_GFX &g,const char *value,int x,int y,int size=1,uint16_t color=Ink) {
    g.setFont(nullptr); g.setTextWrap(false); g.setTextSize(size); g.setTextColor(color);
    g.setCursor(x,y); g.print(value);
}
inline void center(Arduino_GFX &g,const char *value,int y,int size=1,uint16_t color=Ink) {
    text(g,value,(222-int(strlen(value))*6*size)/2,y,size,color);
}
inline void batteryIcon(Arduino_GFX &g,int x,int y,int w,int h,int percent,bool charging) {
    const uint16_t color = charging ? RGB565(36,174,95) : percent >= 0 && percent <= 20 ? RGB565(226,55,66) : Ink;
    g.drawRoundRect(x,y,w,h,3,Muted);
    g.fillRoundRect(x+w+1,y+h/2-2,2,4,1,Muted);
    if (percent >= 0) {
        const int fill = (w-6)*percent/100;
        if (fill > 0) g.fillRect(x+3,y+3,fill,h-6,color);
    }
}
inline void statusBar(Arduino_GFX &g,const State &s) {
    g.fillRect(0,0,222,34,WHITE);
    text(g,s.clock,14,11,2);
    char label[8];
    if (s.battery.available) snprintf(label,sizeof(label),"%d%%",s.battery.percent);
    else snprintf(label,sizeof(label),"--%%");
    text(g,label,174-int(strlen(label))*6,14);
    batteryIcon(g,180,11,26,15,s.battery.available ? s.battery.percent : -1,s.battery.charging);
    if (s.battery.charging) {
        g.fillTriangle(139,10,134,18,139,18,RGB565(36,174,95));
        g.fillTriangle(137,17,142,17,137,25,RGB565(36,174,95));
    }
}
inline void icon(Arduino_GFX &g,int item,int x,int y) {
    static const uint16_t colors[] = {Blue,RGB565(116,87,218),RGB565(225,82,113),RGB565(38,165,101),RGB565(39,144,166)};
    g.fillRoundRect(x,y,30,30,8,colors[item]);
    const int cx=x+15, cy=y+15;
    if (item==0) {
        for (int r : {6,11}) for (int a=-140;a < -40;a+=5) {
            const float p=a*0.017453293f,q=(a+5)*0.017453293f;
            g.drawLine(cx+lroundf(cosf(p)*r),cy+6+lroundf(sinf(p)*r),cx+lroundf(cosf(q)*r),cy+6+lroundf(sinf(q)*r),WHITE);
        }
        g.fillCircle(cx,cy+6,2,WHITE);
    } else if (item==1) {
        g.drawCircle(cx,cy,5,WHITE);
        for (int i=0;i<8;++i) {
            const float a=i*0.78539816f;
            g.drawLine(cx+lroundf(cosf(a)*8),cy+lroundf(sinf(a)*8),cx+lroundf(cosf(a)*11),cy+lroundf(sinf(a)*11),WHITE);
        }
    } else if (item==2) {
        g.fillRect(cx-9,cy-3,5,7,WHITE);
        g.fillTriangle(cx-5,cy-3,cx+1,cy-8,cx+1,cy+8,WHITE);
        g.drawLine(cx+5,cy-5,cx+8,cy-2,WHITE); g.drawLine(cx+8,cy-2,cx+8,cy+2,WHITE); g.drawLine(cx+8,cy+2,cx+5,cy+5,WHITE);
    } else if (item==3) {
        g.drawRoundRect(cx-10,cy-6,18,12,2,WHITE); g.fillRect(cx+9,cy-2,2,4,WHITE); g.fillRect(cx-7,cy-3,9,6,WHITE);
    } else {
        g.drawCircle(cx,cy,10,WHITE); g.drawLine(cx,cy,cx,cy-6,WHITE); g.drawLine(cx,cy,cx+5,cy+3,WHITE);
    }
}
inline void control(Arduino_GFX &g,const char *label,int x,int y,int w,int h,bool primary=false) {
    g.fillRoundRect(x,y,w,h,12,primary?Blue:Card);
    text(g,label,x+(w-int(strlen(label))*12)/2,y+(h-16)/2,2,primary?WHITE:Ink);
}
inline const char *batteryState(const FactoryDevice::Battery &b) {
    return !b.available ? "Reading unavailable" : b.charging ? "Charging" : b.full ? "Fully charged" : b.usb ? "USB connected" : "On battery";
}
inline void draw(Arduino_GFX &g,Page page,const State &s) {
    g.fillScreen(WHITE); statusBar(g,s);
    if (page==Page::List) {
        text(g,"Settings",16,51,3);
        g.fillRoundRect(12,ListTop,198,RowHeight*5,16,Card);
        static const char *labels[] = {"Wi-Fi","Brightness","Volume","Battery","Time zone"};
        for (int row=0;row<5;++row) {
            const int y=ListTop+RowHeight*row;
            icon(g,row,22,y+17); text(g,labels[row],64,y+14,2);
            char value[24];
            if (row==0) snprintf(value,sizeof(value),"%s",s.wifi);
            else if (row==1 || row==2) snprintf(value,sizeof(value),"%d%%%s",row==1?s.brightness:s.volume,row==2 && !s.volume?"  Muted":"");
            else if (row==3) {
                if (s.battery.available) snprintf(value,sizeof(value),"%d%%  %s",s.battery.percent,s.battery.charging?"Charging":"");
                else snprintf(value,sizeof(value),"Unavailable");
            } else snprintf(value,sizeof(value),"%s",s.zone);
            text(g,value,64,y+38,1,Muted);
            g.drawLine(197,y+28,201,y+32,Muted); g.drawLine(201,y+32,197,y+36,Muted);
            if (row<4) g.drawFastHLine(64,y+RowHeight-1,133,Line);
        }
        return;
    }
    text(g,"< Settings",16,48,1,Blue);
    const char *title = page==Page::Brightness?"Brightness":page==Page::Volume?"Volume":page==Page::Battery?"Battery":"Time zone";
    center(g,title,86,3);
    if (page==Page::Brightness || page==Page::Volume) {
        const bool light=page==Page::Brightness;
        const int value=light?s.brightness:s.volume, minimum=light?10:0;
        icon(g,light?1:2,96,144);
        char label[12]; snprintf(label,sizeof(label),"%d%%",value); center(g,label,203,5);
        const int fill=(value-minimum)*162/(100-minimum);
        g.fillRoundRect(30,287,162,6,3,Line);
        if (fill) g.fillRoundRect(30,287,fill,6,3,Blue);
        g.fillCircle(30+fill,292,11,Line); g.fillCircle(30+fill,290,10,Blue);
        text(g,light?"10%":"0%",25,313,1,Muted); text(g,"100%",171,313,1,Muted);
        control(g,"-",30,350,64,44); control(g,"+",128,350,64,44);
        center(g,light?"Drag to adjust brightness":"Drag to adjust volume",424,1,Muted);
        center(g,light?"Saved automatically":value?"Works in every app":"Sound is muted",443,1,Muted);
    } else if (page==Page::Battery) {
        batteryIcon(g,63,151,90,42,s.battery.available?s.battery.percent:-1,s.battery.charging);
        char label[16];
        if (s.battery.available) snprintf(label,sizeof(label),"%d%%",s.battery.percent);
        else snprintf(label,sizeof(label),"--%%");
        center(g,label,226,5); center(g,batteryState(s.battery),287,1,Muted);
        g.fillRoundRect(20,331,182,64,12,Card);
        text(g,"Battery voltage",34,343,1,Muted);
        if (s.battery.available) snprintf(label,sizeof(label),"%.2f V",s.battery.millivolts/1000.0);
        else snprintf(label,sizeof(label),"-- V");
        text(g,label,34,365,2);
        center(g,"Estimated from voltage",427,1,Muted);
    } else {
        icon(g,4,96,144); center(g,s.zone,213,2);
        center(g,"UTC offset",251,1,Muted);
        control(g,"-1h",20,294,85,43); control(g,"+1h",117,294,85,43);
        control(g,"-15m",20,349,85,43); control(g,"+15m",117,349,85,43);
        control(g,"Apply",20,414,182,44,true);
    }
}
}
