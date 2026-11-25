#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace FactoryGameRules {
enum class Direction : uint8_t { Up, Right, Down, Left, None };
enum class State : uint8_t { Ready, Playing, Paused, Lost, Won };
enum class Event : uint8_t { None, Food, Crash, Wall, Win };
inline int dx(Direction d) { return d==Direction::Right ? 1 : d==Direction::Left ? -1 : 0; }
inline int dy(Direction d) { return d==Direction::Down ? 1 : d==Direction::Up ? -1 : 0; }
inline bool opposite(Direction a,Direction b) {
    return a!=Direction::None && b!=Direction::None && (int(a)+2)%4==int(b);
}
inline Direction direction(int x,int y) {
    if (!x && !y) return Direction::None;
    return std::abs(x)>std::abs(y) ? (x>0 ? Direction::Right : Direction::Left) :
        (y>0 ? Direction::Down : Direction::Up);
}
struct Cell { int16_t x=0,y=0; Cell()=default; Cell(int x,int y):x(x),y(y){} };
inline bool same(Cell a,Cell b) { return a.x==b.x && a.y==b.y; }
struct Random {
    uint32_t value=1;
    void seed(uint32_t n) { value=n ? n : 0x9e3779b9u; }
    uint32_t next() { value^=value<<13; value^=value>>17; value^=value<<5; return value; }
};

struct Snake {
    static constexpr int Columns=24, Rows=12, Capacity=Columns*Rows;
    Cell body[Capacity];
    Cell food;
    int head=0,length=4,score=0,best=0;
    Direction heading=Direction::Right, turns[2]={Direction::None,Direction::None};
    int pending=0;
    State state=State::Ready;
    Random random;
    Cell segment(int i) const { return body[(head+i)%Capacity]; }
    bool occupies(Cell cell,int count) const {
        for (int i=0;i<count;++i) if (same(segment(i),cell)) return true;
        return false;
    }
    void placeFood() {
        const int free=Capacity-length;
        if (!free) { food={-1,-1}; return; }
        int pick=random.next()%free;
        for (int y=0;y<Rows;++y) for (int x=0;x<Columns;++x)
            if (!occupies({x,y},length) && pick--==0) { food={x,y}; return; }
    }
    void reset(uint32_t seed) {
        random.seed(seed); head=0; length=4; score=0; pending=0;
        heading=Direction::Right; state=State::Ready;
        for (int i=0;i<length;++i) body[i]={6-i,Rows/2};
        placeFood();
    }
    bool turn(Direction d) {
        if (state!=State::Playing || d==Direction::None || pending==2) return false;
        const Direction last=pending ? turns[pending-1] : heading;
        if (d==last || opposite(last,d)) return false;
        turns[pending++]=d; return true;
    }
    uint32_t interval() const { return static_cast<uint32_t>(std::max(75,180-(score/30)*12)); }
    Event step() {
        if (state!=State::Playing) return Event::None;
        if (pending) { heading=turns[0]; if (pending==2) turns[0]=turns[1]; --pending; }
        const Cell old=segment(0), next{old.x+dx(heading),old.y+dy(heading)};
        const bool eating=same(next,food);
        // The old tail vacates its cell on a non-growing move.
        if (next.x<0 || next.x>=Columns || next.y<0 || next.y>=Rows ||
            occupies(next,length-(eating ? 0 : 1))) {
            state=State::Lost; pending=0; return Event::Crash;
        }
        head=(head+Capacity-1)%Capacity; body[head]=next;
        if (!eating) return Event::None;
        ++length; score+=10; best=std::max(best,score);
        if (length==Capacity) { state=State::Won; food={-1,-1}; return Event::Win; }
        placeFood(); return Event::Food;
    }
};

struct Maze {
    static constexpr int Columns=21, Rows=11, Cells=Columns*Rows;
    static constexpr int RoomsX=10, RoomsY=5, RoomCount=RoomsX*RoomsY;
    static constexpr float Radius=0.28f;
    uint8_t walls[Cells]={}, trail[Cells]={};
    float x=1.5f,y=1.5f;
    unsigned level=1;
    uint32_t elapsedMs=0;
    State state=State::Ready;
    Random random;
    bool wall(int cx,int cy) const { return cx<0 || cy<0 || cx>=Columns || cy>=Rows || walls[cy*Columns+cx]; }
    bool fits(float px,float py) const {
        if (!std::isfinite(px) || !std::isfinite(py)) return false;
        return !wall(int(std::floor(px-Radius)),int(std::floor(py-Radius))) &&
               !wall(int(std::floor(px+Radius)),int(std::floor(py-Radius))) &&
               !wall(int(std::floor(px-Radius)),int(std::floor(py+Radius))) &&
               !wall(int(std::floor(px+Radius)),int(std::floor(py+Radius)));
    }
    void reset(uint32_t seed,unsigned nextLevel=1) {
        random.seed(seed); level=nextLevel; x=y=1.5f; elapsedMs=0; state=State::Ready;
        std::memset(walls,1,sizeof(walls)); std::memset(trail,0,sizeof(trail));
        // Iterative randomized depth-first search: every room is reachable and
        // the stack is bounded, regardless of the generated path length.
        uint8_t visited[RoomCount]={}, stack[RoomCount]={};
        int depth=1; visited[0]=1; walls[Columns+1]=0;
        while (depth) {
            const int room=stack[depth-1], rx=room%RoomsX, ry=room/RoomsX;
            Direction choices[4]; int count=0;
            for (int i=0;i<4;++i) {
                const auto d=static_cast<Direction>(i);
                const int nx=rx+dx(d),ny=ry+dy(d);
                if (nx>=0 && ny>=0 && nx<RoomsX && ny<RoomsY && !visited[ny*RoomsX+nx]) choices[count++]=d;
            }
            if (!count) { --depth; continue; }
            const auto d=choices[random.next()%count];
            const int nx=rx+dx(d),ny=ry+dy(d),next=ny*RoomsX+nx;
            walls[(2*ry+1+dy(d))*Columns+2*rx+1+dx(d)]=0;
            walls[(2*ny+1)*Columns+2*nx+1]=0;
            visited[next]=1; stack[depth++]=next;
        }
        trail[Columns+1]=1;
    }
    Event move(float mx,float my) {
        if (state!=State::Playing || !std::isfinite(mx) || !std::isfinite(my)) return Event::None;
        mx=std::max(-4.0f,std::min(4.0f,mx)); my=std::max(-4.0f,std::min(4.0f,my));
        const int steps=std::max(1,int(std::ceil(std::max(std::fabs(mx),std::fabs(my))/0.08f)));
        const float sx=mx/steps,sy=my/steps;
        bool blocked=false;
        for (int i=0;i<steps;++i) {
            if (fits(x+sx,y)) x+=sx; else if (sx) blocked=true;
            if (fits(x,y+sy)) y+=sy; else if (sy) blocked=true;
            trail[int(y)*Columns+int(x)]=1;
            if (std::fabs(x-(Columns-1.5f))<0.35f && std::fabs(y-(Rows-1.5f))<0.35f) {
                state=State::Won; return Event::Win;
            }
        }
        return blocked ? Event::Wall : Event::None;
    }
};

// Relative optical motion, not absolute touch coordinates. Rotation is only
// applied to the sensor; screen arrows always follow the displayed direction.
inline Cell rotateMotion(int x,int y,uint8_t rotation) {
    switch (rotation&3) {
        case 1:return {-y,x}; case 2:return {-x,-y}; case 3:return {y,-x}; default:return {x,y};
    }
}
// This module's sensor axes are exchanged relative to the landscape screen.
// A rotation alone cannot correct a reflected mapping. Apply the mounting
// transform first, then the user's ROT adjustment; raw serial values stay raw.
inline Cell sensorMotion(int x,int y,uint8_t rotation) {
    return rotateMotion(y,x,rotation);
}
struct MotionDirection {
    int x=0,y=0; uint32_t last=0;
    void reset() { x=y=0; last=0; }
    Direction feed(int mx,int my,uint32_t now) {
        if (now-last>100) x=y=0;
        last=now; x+=mx; y+=my;
        if (std::max(std::abs(x),std::abs(y))<6) return Direction::None;
        const auto d=direction(x,y); x=y=0; return d;
    }
};
} // namespace FactoryGameRules
