#pragma once
#include "FactorySurface.h"
#include "FactoryMiniGameRules.h"
#include "FactoryText.h"
#include <cstdio>
#include <cstring>

namespace FactoryMiniGamesUI {
using namespace FactoryGameRules;
constexpr uint16_t Ink=RGB565(35,44,57), Muted=RGB565(117,128,142);
constexpr uint16_t Green=RGB565(35,164,101), Purple=RGB565(117,91,202);
constexpr int BoardX=10, SnakeY=40, MazeY=36, SnakeCell=14, MazeCell=16;
inline bool inside(int x,int y,int left,int top,int w,int h) { return x>=left && x<left+w && y>=top && y<top+h; }
inline bool actionAt(int x,int y) { return inside(x,y,354,44,120,36); }
inline bool rotationAt(int x,int y) { return inside(x,y,264,4,76,30); }
inline bool boardAt(int x,int y) { return inside(x,y,10,36,336,176); }
inline Direction controlAt(int x,int y) {
    if (inside(x,y,392,90,40,38)) return Direction::Up;
    if (inside(x,y,352,128,40,38)) return Direction::Left;
    if (inside(x,y,432,128,42,38)) return Direction::Right;
    if (inside(x,y,392,166,40,38)) return Direction::Down;
    return Direction::None;
}
inline void text(Arduino_GFX &c,const char *value,int x,int y,int size=1,uint16_t color=Ink) {
    c.setFont(nullptr); c.setTextWrap(false); c.setTextSize(size); c.setTextColor(color);
    c.setCursor(x,y); c.print(value);
}
inline void centered(Arduino_GFX &c,const char *value,int x,int y,int width,int size,uint16_t color) {
    text(c,value,x+(width-int(std::strlen(value))*6*size)/2,y,size,color);
}
inline void arrow(Arduino_GFX &c,Direction d,int x,int y,uint16_t color) {
    if (d==Direction::Up) c.fillTriangle(x,y-6,x-6,y+4,x+6,y+4,color);
    else if (d==Direction::Down) c.fillTriangle(x,y+6,x-6,y-4,x+6,y-4,color);
    else if (d==Direction::Left) c.fillTriangle(x-6,y,x+4,y-6,x+4,y+6,color);
    else if (d==Direction::Right) c.fillTriangle(x+6,y,x-4,y-6,x-4,y+6,color);
}
inline void icon(Arduino_GFX &c,bool maze,int x,int y) {
    if (!maze) {
        const uint16_t light=RGB565(107,204,136);
        c.fillRoundRect(x-23,y-18,14,39,6,light);
        c.fillRoundRect(x-22,y+8,43,14,6,Green);
        c.fillRoundRect(x+8,y-5,14,26,6,Green);
        c.fillRoundRect(x-6,y-11,28,15,6,Green);
        c.fillCircle(x-2,y-7,2,WHITE); c.fillCircle(x-2,y,2,WHITE);
        c.fillCircle(x-3,y-7,1,Ink); c.fillCircle(x-3,y,1,Ink);
        c.fillCircle(x+15,y-22,5,RGB565(234,98,100));
        c.drawLine(x+15,y-27,x+18,y-29,Green);
    } else {
        c.fillRoundRect(x-25,y-25,50,50,7,RGB565(241,238,251));
        c.drawRoundRect(x-22,y-22,44,44,3,Purple);
        c.fillRect(x-10,y-22,4,17,Purple); c.fillRect(x+7,y-12,15,4,Purple);
        c.fillRect(x-22,y+2,16,4,Purple); c.fillRect(x+4,y+1,4,21,Purple);
        c.fillRect(x-8,y+12,12,4,Purple);
        c.fillCircle(x-16,y-15,4,RGB565(65,159,225));
        c.fillCircle(x+15,y+15,5,Green); c.fillCircle(x+15,y+15,2,WHITE);
    }
}
inline void hardwareRequired(FactorySurface &c,bool maze) {
    const uint16_t accent=maze ? Purple : Green;
    text(c,maze ? "MAZE" : "SNAKE",12,12,2,accent);
    c.fillCircle(240,66,29,RGB565(245,247,250));
    c.fillRoundRect(225,58,30,25,7,accent);
    c.fillRoundRect(229,46,5,16,2,accent); c.fillRoundRect(246,46,5,16,2,accent);
    c.fillRoundRect(237,80,6,12,2,accent);
    const char *labels[]={"Please connect T-QUnit","Device not detected. Connect to play.","Auto-detecting device..."};
    const int rows[]={110,140,174};
    for (int i=0;i<3;++i) {
        const int width=factoryTextWidth(labels[i]);
        factoryDrawText(c,labels[i],(480-width)/2,rows[i],width+32,i==0 ? Ink : Muted,WHITE);
    }
    centered(c,"BOOT / Home: return",0,204,480,1,Muted);
}
inline void controls(FactorySurface &c,State state,bool maze,uint8_t rotation,Direction held) {
    const uint16_t accent=maze ? Purple : Green;
    if (c.clipTop()<36) {
        text(c,maze ? "MAZE" : "SNAKE",10,10,2,accent);
        c.fillRoundRect(264,5,76,27,7,RGB565(244,246,249));
        char label[12]; std::snprintf(label,sizeof(label),"ROT %u",unsigned(rotation)*90);
        centered(c,label,264,15,76,1,Muted);
        c.fillCircle(360,16,3,Green);
        text(c,"PAW READY",369,13,1,Muted);
    }
    c.fillRoundRect(354,44,120,36,9,accent);
    const char *action=state==State::Ready ? "START" : state==State::Playing ? "PAUSE" :
        state==State::Paused ? "RESUME" : state==State::Won && maze ? "NEXT" : "RETRY";
    centered(c,action,354,55,120,2,WHITE);
    const int px[]={394,434,394,354},py[]={94,132,170,132};
    for (int i=0;i<4;++i) {
        const auto d=static_cast<Direction>(i); const bool selected=held==d;
        c.fillRoundRect(px[i],py[i]+2,36,32,8,RGB565(225,230,236));
        c.fillRoundRect(px[i],py[i],36,32,8,selected ? accent : RGB565(244,246,249));
        arrow(c,d,px[i]+18,py[i]+16,selected ? WHITE : accent);
    }
    c.fillCircle(412,148,10,RGB565(247,248,250)); c.fillCircle(412,148,3,accent);
}
inline void snakeBoard(FactorySurface &c,const Snake &s) {
    c.fillRoundRect(8,38,340,172,5,RGB565(216,232,222));
    for (int y=0;y<Snake::Rows;++y) {
        const int py=SnakeY+y*SnakeCell;
        if (py+SnakeCell<=c.clipTop() || py>=c.clipBottom()) continue;
        for (int x=0;x<Snake::Columns;++x)
            c.fillRect(BoardX+x*SnakeCell,py,SnakeCell,SnakeCell,
                       (x+y)%2 ? RGB565(244,249,245) : RGB565(239,246,241));
    }
    if (s.food.x>=0) {
        const int x=BoardX+s.food.x*SnakeCell+7,y=SnakeY+s.food.y*SnakeCell+7;
        c.fillCircle(x,y+1,5,RGB565(236,99,105));
        c.fillCircle(x-1,y,1,RGB565(255,212,208)); c.drawLine(x,y-4,x+3,y-6,Green);
    }
    for (int i=s.length-1;i>=0;--i) {
        const auto p=s.segment(i); const int x=BoardX+p.x*SnakeCell,y=SnakeY+p.y*SnakeCell;
        if (y+SnakeCell<=c.clipTop() || y>=c.clipBottom()) continue;
        c.fillRoundRect(x+1,y+1,12,12,4,i==0 ? Green : RGB565(95,194,126));
        if (i==0) {
            const int ex=x+7+dx(s.heading)*3,ey=y+7+dy(s.heading)*3;
            const int sx=dy(s.heading)*3,sy=dx(s.heading)*3;
            c.fillCircle(ex+sx,ey+sy,2,WHITE); c.fillCircle(ex-sx,ey-sy,2,WHITE);
            c.fillCircle(ex+sx,ey+sy,1,Ink); c.fillCircle(ex-sx,ey-sy,1,Ink);
        }
    }
}
inline void mazeBoard(FactorySurface &c,const Maze &m) {
    c.fillRect(BoardX,MazeY,Maze::Columns*MazeCell,Maze::Rows*MazeCell,RGB565(249,249,253));
    for (int y=0;y<Maze::Rows;++y) {
        const int py=MazeY+y*MazeCell;
        if (py+MazeCell<=c.clipTop() || py>=c.clipBottom()) continue;
        for (int x=0;x<Maze::Columns;++x) {
            const int px=BoardX+x*MazeCell;
            if (m.wall(x,y)) {
                c.fillRect(px,py,MazeCell,MazeCell,RGB565(188,195,222));
                if (!m.wall(x,y-1)) c.drawFastHLine(px,py,MazeCell,RGB565(157,168,204));
            } else if (m.trail[y*Maze::Columns+x]) c.fillCircle(px+8,py+8,1,RGB565(198,190,231));
        }
    }
    const int gx=BoardX+(Maze::Columns-2)*MazeCell,gy=MazeY+(Maze::Rows-2)*MazeCell;
    c.fillRoundRect(gx+1,gy+1,14,14,3,RGB565(211,242,224));
    c.drawFastVLine(gx+5,gy+4,9,Green); c.fillTriangle(gx+6,gy+3,gx+12,gy+6,gx+6,gy+8,Green);
    const int x=BoardX+std::lround(m.x*MazeCell),y=MazeY+std::lround(m.y*MazeCell);
    c.fillCircle(x+1,y+1,5,RGB565(214,209,236)); c.fillCircle(x,y,4,Purple); c.fillCircle(x-1,y-1,1,WHITE);
}
inline void draw(FactorySurface &c,bool maze,const Snake &s,const Maze &m,bool sensor,uint8_t rotation,Direction held) {
    c.fillScreen(WHITE);
    if (!sensor) { hardwareRequired(c,maze); return; }
    const State state=maze ? m.state : s.state;
    controls(c,state,maze,rotation,held);
    if (c.clipTop()<36) {
        char value[28];
        if (maze) {
            std::snprintf(value,sizeof(value),"LEVEL %u",m.level); text(c,value,92,8,1,Purple);
            const unsigned seconds=m.elapsedMs/1000;
            std::snprintf(value,sizeof(value),"TIME %02u:%02u",seconds/60,seconds%60); text(c,value,92,22,1,Muted);
        } else {
            std::snprintf(value,sizeof(value),"SCORE %04d",s.score); text(c,value,100,8,1,Green);
            std::snprintf(value,sizeof(value),"BEST  %04d",s.best); text(c,value,100,22,1,Muted);
        }
    }
    if (c.clipBottom()>36 && c.clipTop()<212) {
        if (maze) mazeBoard(c,m); else snakeBoard(c,s);
        if (state!=State::Playing) {
            c.fillRoundRect(58,87,240,84,10,RGB565(219,224,234));
            c.fillRoundRect(56,84,240,84,10,WHITE);
            const char *title=state==State::Ready ? (maze ? "FIND THE EXIT" : "EAT & GROW") :
                state==State::Paused ? "PAUSED" : state==State::Lost ? "GAME OVER" : "WELL DONE!";
            centered(c,title,56,98,240,2,maze ? Purple : Green);
            centered(c,state==State::Won ? (maze ? "Tap NEXT for a new maze" : "Board filled! A perfect run") :
                state==State::Lost ? "Try again and beat your best" : "PAW swipe or screen arrows",56,126,240,1,Muted);
            centered(c,state==State::Paused ? "Tap RESUME or the board" :
                state==State::Won && maze ? "A new route every level" : "Tap button or board to play",56,146,240,1,Ink);
        }
    }
    if (c.clipBottom()>212) text(c,maze ? "Reach the green flag." : "Eat apples. Avoid walls and your tail.",10,214,1,Muted);
}
} // namespace FactoryMiniGamesUI
