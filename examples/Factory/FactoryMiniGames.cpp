#include "FactoryMiniGames.h"
#include "FactoryMiniGamesUI.h"
#include "FactoryPAW350.h"
#include "FactoryGameHardware.h"
#include "FactoryAudio.h"
#include "FactorySoundSequence.h"

namespace FactoryMiniGames {
namespace {
using namespace FactoryGameRules;
Arduino_GFX *screen=nullptr;
Snake snake;
Maze maze;
MotionDirection opticalDirection;
HardwareGate hardware;
FactoryAudio audio;
FactorySoundSequence<FactoryAudio> sounds;
bool active=false,playingMaze=false,dirty=false,firstFrame=false,boardDrag=false;
bool feedbackReady=false;
uint8_t sensorRotation=0; // ROT is relative to the corrected sensor-to-screen axes.
Direction held=Direction::None;
int touchX=0,touchY=0;
uint32_t lastStep=0,lastTick=0,lastFrame=0,lastWall=0,motorUntil=0;
constexpr uint8_t MotorChannel=2;

State &state() { return playingMaze ? maze.state : snake.state; }
void drawScene() { FactoryMiniGamesUI::draw(factorySurface(),playingMaze,snake,maze,hardware.ready(),sensorRotation,held); }
void render() {
    factorySurface().present(*screen,drawScene,firstFrame);
    dirty=firstFrame=false; lastFrame=millis();
}
void startFeedback() {
    if (feedbackReady) return;
    ledcSetup(MotorChannel,12000,8); ledcAttachPin(VIBRATINO_MOTOR_PWM,MotorChannel); ledcWrite(MotorChannel,0);
    audio.begin(); digitalWrite(MAX98357A_SD_MODE,HIGH); feedbackReady=true;
}
void stopFeedback() {
    if (!feedbackReady) return;
    sounds.stop(audio); digitalWrite(MAX98357A_SD_MODE,LOW); audio.end();
    ledcWrite(MotorChannel,0); ledcDetachPin(VIBRATINO_MOTOR_PWM);
    pinMode(VIBRATINO_MOTOR_PWM,OUTPUT); digitalWrite(VIBRATINO_MOTOR_PWM,LOW);
    motorUntil=0; feedbackReady=false;
}
void feedback(Event event,uint32_t now) {
    if (!hardware.ready() || !feedbackReady || event==Event::None) return;
    if (event==Event::Wall && now-lastWall<180) return;
    lastWall=now;
    sounds.stop(audio);
    if (event==Event::Win) {
        for (uint8_t note : {72,76,79,84}) sounds.append(note,85,20,72);
    } else if (event==Event::Crash) {
        sounds.append(52,85,20); sounds.append(40,160,0);
    } else sounds.append(event==Event::Food ? 84 : 48,event==Event::Food ? 65 : 25,0,60);
    sounds.play(audio,now);
    ledcWrite(MotorChannel,event==Event::Crash ? 170 : 95);
    motorUntil=now+(event==Event::Crash ? 100 : event==Event::Win ? 80 : 25);
}
void action() {
    if (!hardware.ready()) return;
    if (state()==State::Playing) state()=State::Paused;
    else {
        startFeedback();
        if (state()==State::Won || state()==State::Lost) {
            if (playingMaze) maze.reset(esp_random(),maze.level+1);
            else snake.reset(esp_random());
        }
        state()=State::Playing;
    }
    snake.pending=0; opticalDirection.reset(); held=Direction::None; boardDrag=false;
    lastStep=lastTick=millis(); dirty=true;
}
void moveMaze(float x,float y,uint32_t now) {
    if (!hardware.ready()) return;
    const float oldX=maze.x,oldY=maze.y;
    const auto event=maze.move(x,y);
    if (oldX!=maze.x || oldY!=maze.y || event==Event::Win) dirty=true;
    feedback(event,now);
}
}

void open(Game game,Arduino_GFX &display) {
    close(); screen=&display; playingMaze=game==Game::Maze;
    screen->setRotation(1);
    if (playingMaze) maze.reset(esp_random()); else snake.reset(esp_random());
    held=Direction::None; boardDrag=false; opticalDirection.reset();
    digitalWrite(RT9080_EN,HIGH);
    FactoryPAW350::open();
    int mx=0,my=0;
    FactoryPAW350::poll(millis(),mx,my); // Check before displaying any game controls.
    hardware.reset(FactoryPAW350::available());
    active=true; dirty=firstFrame=true;
    motorUntil=0; lastWall=millis()-180;
    render(); lastStep=lastTick=millis();
}
void close() {
    if (!active) return;
    FactoryPAW350::close();
    stopFeedback(); hardware.reset();
    active=false; screen=nullptr; held=Direction::None; boardDrag=false; motorUntil=0;
}
void touch(bool down,int16_t x,int16_t y,bool pressed) {
    if (!active || !hardware.touch(down)) return;
    if (!down) {
        if (held!=Direction::None) dirty=true;
        held=Direction::None; boardDrag=false; return;
    }
    if (pressed && FactoryMiniGamesUI::rotationAt(x,y)) {
        sensorRotation=(sensorRotation+1)&3; opticalDirection.reset();
        held=Direction::None; boardDrag=false; dirty=true; return;
    }
    if (pressed && FactoryMiniGamesUI::actionAt(x,y)) { action(); return; }
    const Direction control=FactoryMiniGamesUI::controlAt(x,y);
    if (control!=held) {
        held=control; dirty=true;
        if (!playingMaze) snake.turn(control);
    }
    if (control!=Direction::None) { boardDrag=false; return; }
    if (pressed && FactoryMiniGamesUI::boardAt(x,y)) {
        if (state()!=State::Playing) action();
        boardDrag=true; touchX=x; touchY=y; return;
    }
    if (!boardDrag || state()!=State::Playing) return;
    if (!FactoryMiniGamesUI::boardAt(x,y)) { boardDrag=false; return; }
    const int mx=x-touchX,my=y-touchY;
    if (playingMaze) {
        moveMaze(mx*0.055f,my*0.055f,millis()); touchX=x; touchY=y;
    } else if (std::max(std::abs(mx),std::abs(my))>=12) {
        snake.turn(direction(mx,my)); touchX=x; touchY=y;
    }
}
void tick() {
    if (!active) return;
    const uint32_t now=millis(),elapsed=now-lastTick;
    lastTick=now;
    if (feedbackReady) sounds.tick(audio,now);
    if (motorUntil && static_cast<int32_t>(now-motorUntil)>=0) { ledcWrite(MotorChannel,0); motorUntil=0; }

    int mx=0,my=0;
    const bool motion=FactoryPAW350::poll(now,mx,my);
    if (hardware.update(FactoryPAW350::available(),state())) {
        dirty=true; opticalDirection.reset();
        snake.pending=0; held=Direction::None; boardDrag=false;
        if (!hardware.ready()) stopFeedback();
    }
    if (hardware.ready() && state()==State::Playing) {
        if (playingMaze) {
            const uint32_t oldSeconds=maze.elapsedMs/1000;
            maze.elapsedMs+=elapsed;
            if (oldSeconds!=maze.elapsedMs/1000) dirty=true;
            if (held!=Direction::None) {
                const float distance=std::min(uint32_t(40),elapsed)*0.004f;
                moveMaze(dx(held)*distance,dy(held)*distance,now);
            }
        }
        if (motion) {
            const auto delta=sensorMotion(mx,my,sensorRotation);
            if (playingMaze) moveMaze(delta.x*0.035f,delta.y*0.035f,now);
            else snake.turn(opticalDirection.feed(delta.x,delta.y,now));
        }
        if (!playingMaze && now-lastStep>=snake.interval()) {
            lastStep=now; feedback(snake.step(),now); dirty=true;
        }
    }
    if (dirty && now-lastFrame>=25) render();
}
void drawIcon(Arduino_GFX &display,bool maze,int16_t x,int16_t y) { FactoryMiniGamesUI::icon(display,maze,x,y); }
} // namespace FactoryMiniGames
