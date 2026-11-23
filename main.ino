#include <Wire.h>
#include <U8g2lib.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Update.h>
#include <LittleFS.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <vector>

#define EEPROM_SIZE 160
const char* FW_VERSION = "v2.5.6";
const char* OTA_URL    = "https://raw.githubusercontent.com/Platonchu/esp32console/main/firmware.bin";
const char* NOTES_FILE = "/notes.json";
const char* SONGS_FILE = "/songs.json";

const size_t MAX_NOTES        = 30;
const size_t MAX_NOTE_TOPIC_W = 40;
const size_t MAX_NOTE_BODY_W  = 2500;
const size_t MAX_SONGS        = 20;
const size_t MAX_SONG_TITLE_W = 32;
const size_t MAX_SONG_STEPS   = 240;

const float WEATHER_LAT = 53.75f;
const float WEATHER_LON = 27.65f;

const uint16_t WEB_PORT = 80;
WiFiServer probeServer(WEB_PORT);
WebServer server(WEB_PORT);
bool probeStarted = false;
bool webServerStarted = false;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 13, 32);

struct NoteItem {
  String topic;
  String body;
};

struct SongStep {
  int freq;
  int dur;
};

struct SongItem {
  String title;
  std::vector<SongStep> steps;
};

struct ToneStep {
  int freq;
  int dur;
  int gap;
};

struct Achievement {
  const char* title;
  const char* desc;
  bool unlocked;
};

struct Point {
  int8_t x, y;
};

struct DoodlePlatform {
  float x;
  float y;
  uint8_t w;
};

struct ClickParticle {
  float x;
  float y;
  float vy;
  int val;
  uint8_t life;
};

enum GameState : uint8_t {
  ST_SPLASH,
  ST_MAIN_MENU,
  ST_GAME_SELECT,
  ST_SLOT_GAME,
  ST_SPINNING,
  ST_WIN_ANIM,
  ST_LOSE_ANIM,
  ST_NO_MONEY,
  ST_SNAKE_GAME,
  ST_SNAKE_OVER,
  ST_TETRIS_GAME,
  ST_TETRIS_OVER,
  ST_BOTTLE_GAME,
  ST_FLAPPY_GAME,
  ST_FLAPPY_OVER,
  ST_PONG_GAME,
  ST_PONG_OVER,
  ST_DINO_GAME,
  ST_DINO_OVER,
  ST_DOODLE_GAME,
  ST_DOODLE_OVER,
  ST_CLICKER_GAME,
  ST_WEB_MESSAGE,
  ST_COIN_KEYBOARD,
  ST_ACHIEVEMENTS_MENU,
  ST_RECORDS_SCREEN,
  ST_INFO_SCREEN,
  ST_CREDITS_MENU,
  ST_SETTINGS_MENU,
  ST_RESET_STEP_1,
  ST_RESET_STEP_2,
  ST_RESET_STEP_3,
  ST_WIFI_MANAGEMENT,
  ST_WIFI_CONNECTED_INFO,
  ST_WIFI_SCAN,
  ST_WIFI_LIST,
  ST_WIFI_KEYBOARD,
  ST_WIFI_CONNECTING,
  ST_WIFI_ERROR,
  ST_TEST_CONFIRM,
  ST_TEST_MELODY,
  ST_TEST_MODE,
  ST_CLOCK_MENU,
  ST_NET_CLOCK,
  ST_STOPWATCH,
  ST_TIMER_SET,
  ST_TIMER_RUNNING,
  ST_TIMER_DONE,
  ST_MUSIC_MENU,
  ST_METRONOME,
  ST_CALCULATOR,
  ST_PIANO_MODE,
  ST_RECOVERY_MENU,
  ST_OTA_WAIT,
  ST_OTA_UPDATING,
  ST_OTA_DONE,
  ST_NOTES_LIST,
  ST_NOTE_VIEW
};

const uint8_t BTN_UP_PIN     = 14;
const uint8_t BTN_DOWN_PIN   = 27;
const uint8_t BTN_LEFT_PIN   = 26;
const uint8_t BTN_RIGHT_PIN  = 25;
const uint8_t BTN_SELECT_PIN = 33;
const uint8_t BUZZER_PIN     = 12;

const uint8_t ICON_COUNT = 6;
const long MAX_COINS     = 500000;
const long GOD_COINS_VAL = 999999999L;
const long FIXED_SPIN_COST = 10;

bool godMode      = false;
bool disabledMode = false;
volatile uint32_t stateRev = 1;

void markDirty() {
  stateRev++;
}

const long GMT_OFFSET_SEC     = 3 * 3600;
const int DAYLIGHT_OFFSET_SEC = 0;

const uint8_t ADDR_COINS         = 0;
const uint8_t ADDR_SPINS         = 4;
const uint8_t ADDR_MAX_WIN       = 8;
const uint8_t ADDR_FLAG          = 12;
const uint8_t ADDR_SSID          = 16;
const uint8_t ADDR_PASS          = 48;
const uint8_t ADDR_FLAPPY_REC    = 80;
const uint8_t ADDR_TETRIS_REC    = 84;
const uint8_t ADDR_SNAKE_REC     = 88;
const uint8_t ADDR_BOTTLE_REC    = 92;
const uint8_t ADDR_ACHIEVEMENTS  = 96;
const uint8_t ADDR_PONG_REC      = 100;
const uint8_t ADDR_DINO_REC      = 104;
const uint8_t ADDR_DOODLE_REC    = 108;
const uint8_t ADDR_CLICKER_COUNT = 112;
const uint8_t ADDR_CLICKER_PWR   = 116;
const uint8_t ADDR_CLICKER_AUTO  = 120;

std::vector<NoteItem> notesList;
std::vector<SongItem> songsList;
bool fsReady      = false;
String fsInitMsg  = "Не инициализировано";
int noteCursor    = 0;
int activeNoteIdx = -1;
int noteScrollLine = 0;

GameState gameState = ST_SPLASH;
GameState prevObservedState = ST_SPLASH;
int8_t menuCursor   = 0;
int wifiNetCount    = 0;
bool wifiScanActive = false;
unsigned long wifiScanStartMs = 0;
uint8_t wifiScanRetries = 0;
uint8_t wifiConnectAttempts = 0;
unsigned long lastBackgroundWifiCheckMs = 0;

int kbRow = 0;
int kbCol = 0;
uint8_t kbPage = 0;
char inputPassword[33] = "";
char inputCoinStr[10]  = "";
char targetSsid[32]    = "";
char currentSsid[32]   = "";
char currentPass[32]   = "";
String ipAddressStr    = "ОФФЛАЙН";
String lastWebStatus   = "Готово к работе";
String webBannerText   = "";
unsigned long webBannerUntilMs = 0;
bool ntpConfigured     = false;

char cachedTimeBuf[16] = "--:--:--";
char cachedDateBuf[24] = "--.--.----";
bool cachedTimeValid   = false;
unsigned long lastClockPollMs = 0;

bool weatherValid       = false;
float weatherTempC      = 0.0f;
String weatherCondition = "Нет данных";
unsigned long lastWeatherMs = 0;
const unsigned long WEATHER_REFRESH_MS = 15UL * 60UL * 1000UL;

bool otaArmed          = false;
bool otaStartFailed    = false;
bool otaWriteFailed    = false;
size_t otaUploadedBytes = 0;
int otaProgressPct     = 0;
unsigned long lastOtaUiMs = 0;
unsigned long restartAtMs = 0;

const ToneStep WIN_SEQ[] = {
  {659, 90, 20}, {784, 90, 20}, {988, 90, 20},
  {1318, 90, 20}, {1046, 90, 20}, {1318, 90, 0}
};
const ToneStep LOSE_SEQ[] = {
  {392, 130, 30}, {349, 130, 30}, {330, 130, 30}, {262, 260, 0}
};
const ToneStep JACKPOT_SEQ[] = {
  {523, 80, 25}, {659, 80, 25}, {784, 80, 25}, {1046, 120, 25},
  {784, 80, 25}, {1046, 80, 25}, {1318, 240, 0}
};
const ToneStep JINGLE_SEQ[] = {
  {659, 140, 35}, {659, 140, 35}, {659, 280, 35},
  {659, 140, 35}, {659, 140, 35}, {659, 280, 35},
  {659, 140, 35}, {784, 140, 35}, {523, 140, 35},
  {587, 140, 35}, {659, 320, 0}
};
const ToneStep PING_SEQ[] = {
  {1046, 90, 30}, {1318, 90, 30}, {1568, 160, 40}, {2093, 240, 0}
};

const ToneStep* activeSeq = nullptr;
uint8_t activeSeqLen = 0;
uint8_t activeSeqIdx = 0;
unsigned long nextToneAtMs = 0;
bool returnToTestModeAfterSeq = false;
bool testMelodyStarted = false;

bool songPlaying = false;
int activeSongIdx = -1;
size_t activeSongStepIdx = 0;
unsigned long nextSongStepMs = 0;
String activeSongStepLabel = "";

const int MIN_BPM = 20;
const int MAX_BPM = 300;
int metroBpm = 120;
bool metroRunning = false;
int metroBeat = 0;
unsigned long nextMetroMs = 0;

const int ARCH_LOGO_W = 46;
const int ARCH_LOGO_H = 38;
float archX = 22.0f;
float archY = 12.0f;
float archVX = 0.85f;
float archVY = 0.65f;
unsigned long archTimer = 0;

const char CALC_CHARS[] = "0123456789+-*/.";
const int CALC_CHAR_COUNT = sizeof(CALC_CHARS) - 1;
const size_t MAX_CALC_EXPR = 40;
char calcExpr[42] = "12+8*3";
int calcCharIdx = 0;
bool calcHasResult = false;
bool calcError = false;
double calcResult = 0.0;
String calcErrMsg = "";

int pianoSongCursor = 0;
int lastPlayedFreq = 0;
String lastPlayedNoteName = "Ля4";

unsigned long stopwatchStart   = 0;
unsigned long stopwatchElapsed = 0;
bool stopwatchRunning          = false;

int timerHoursSet         = 0;
int timerMinutesSet       = 5;
int timerSecondsSet       = 0;
uint8_t timerFieldCursor  = 0;
unsigned long timerTargetMs = 0;
unsigned long timerRemainingMs = 0;
bool timerRunning         = false;
uint8_t timerAlarmStep    = 0;
unsigned long timerAlarmTime = 0;

const char KB_PAGES[3][5][10] = {
  {
    {'a','b','c','d','e','f','g','h','i','j'},
    {'k','l','m','n','o','p','q','r','s','t'},
    {'u','v','w','x','y','z','0','1','2','3'},
    {'4','5','6','7','8','9','-','_','.','@'},
    {'<',' ','O','^',' ','X',' ',' ',' ',' '}
  },
  {
    {'A','B','C','D','E','F','G','H','I','J'},
    {'K','L','M','N','O','P','Q','R','S','T'},
    {'U','V','W','X','Y','Z','0','1','2','3'},
    {'4','5','6','7','8','9','-','_','.','@'},
    {'<',' ','O','^',' ','X',' ',' ',' ',' '}
  },
  {
    {'!','#','$','%','&','*','(',')','+','='},
    {'?','/','\\',':',';',',','<','>','[',']'},
    {'{','}','~','|','`','\'','"','0','1','2'},
    {'3','4','5','6','7','8','9','-','_','@'},
    {'<',' ','O','^',' ','X',' ',' ',' ',' '}
  }
};

const char NUM_KB_LAYOUT[5][3] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'0', '<', 'X'},
  {'O', ' ', ' '}
};

const uint8_t ACH_COUNT = 7;
Achievement achievements[ACH_COUNT] = {
  {"Новичок",      "Первый спин",         false},
  {"Лудоман",      "50 спинов",           false},
  {"Джекпот!",     "Три главных",         false},
  {"Магнат",       "1000+ монет",         false},
  {"Змеелов",     "50 очков в Змейке",   false},
  {"Архитектор",   "500 очков Тетрис",    false},
  {"Тапатель",     "500 нажатий счётчика",false}
};

int activeAchPopup = -1;
unsigned long achPopupTimer = 0;

int8_t slot1 = 1, slot2 = 0, slot3 = 2;
int8_t prevSlot1 = 0, prevSlot2 = 1, prevSlot3 = 3;
int8_t targetSlot1 = 1, targetSlot2 = 1, targetSlot3 = 1;
long coins = 150;
long displayCoins = 150;
long currentWin = 0;
long maxWin = 0;
long totalSpins = 0;

long clickerCount = 0;
int clickerPower  = 1;
int clickerAuto   = 0;
int clickerCombo  = 1;
unsigned long lastClickMs = 0;
unsigned long lastAutoClickMs = 0;
uint8_t clickBtnAnim = 0;
ClickParticle clickParticles[6];

Point snake[30];
int snakeLen = 3;
int8_t dx = 1, dy = 0;
Point food;
unsigned long snakeTimer = 0;
int snakeScore = 0, snakeRecord = 0;

#define TETRIS_W 10
#define TETRIS_H 20
uint8_t tetrisBoard[TETRIS_H][TETRIS_W];
int tetrisX = 3, tetrisY = 0;
int tetrisCurrShape = 0, tetrisNextShape = 0, tetrisCurrRot = 0;
int tetrisScore = 0, tetrisLines = 0, tetrisLevel = 1, tetrisRecord = 0;
unsigned long tetrisTimer = 0;

const byte TETRIS_SHAPES[7][4][4] = {
  {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
  {{0,0,0,0},{0,1,1,0},{0,1,1,0},{0,0,0,0}},
  {{0,0,0,0},{0,1,0,0},{1,1,1,0},{0,0,0,0}},
  {{0,0,0,0},{0,1,1,0},{1,1,0,0},{0,0,0,0}},
  {{0,0,0,0},{1,1,0,0},{0,1,1,0},{0,0,0,0}},
  {{0,0,0,0},{1,0,0,0},{1,1,1,0},{0,0,0,0}},
  {{0,0,0,0},{0,0,1,0},{1,1,1,0},{0,0,0,0}}
};

uint8_t bottleState = 0;
bool bottleLanded = false;
unsigned long bottleAnimTime = 0;
int bottleScore = 0, bottleRecord = 0;

float flappyY = 32.0f, flappyVelocity = 0.0f;
const float FLAPPY_GRAVITY = 0.34f;
const float FLAPPY_JUMP    = -3.1f;
float pipeX = 128.0f;
int pipeGapY = 28;
const int PIPE_W = 14;
const int PIPE_GAP_H = 27;
int flappyScore = 0, flappyRecord = 0;
unsigned long flappyTimer = 0;

float paddleX = 50.0f;
const int PADDLE_W = 28;
const int PADDLE_H = 4;
float ballX = 64.0f, ballY = 18.0f;
float ballVX = 1.8f, ballVY = 1.6f;
int pongScore = 0, pongRecord = 0;
unsigned long pongTimer = 0;

const int DINO_GROUND_Y = 54;
const int DINO_X        = 16;
float dinoY             = DINO_GROUND_Y - 12;
float dinoVY            = 0.0f;
bool dinoJumping        = false;
bool dinoDucking        = false;
const float DINO_GRAV   = 0.65f;
const float DINO_JUMP_V = -6.4f;
float cactusX           = 128.0f;
int cactusW             = 6;
int cactusH             = 12;
float dinoSpeed         = 2.6f;
int dinoScore           = 0;
int dinoRecord          = 0;
unsigned long dinoTimer = 0;

#define DOODLE_PLAT_COUNT 7
DoodlePlatform doodlePlats[DOODLE_PLAT_COUNT];
float doodleX = 60.0f;
float doodleY = 44.0f;
float doodleVX = 0.0f;
float doodleVY = -4.2f;
const float DOODLE_GRAV = 0.28f;
const float DOODLE_JUMP_V = -4.3f;
int doodleScore = 0;
int doodleRecord = 0;
unsigned long doodleTimer = 0;

bool autoSpin = false;
bool soundEnabled = true;
bool screenSleeping = false;
bool spinResolved = false;
bool ignoreNextIpDismiss = false;

unsigned long stateTimer = 0;
unsigned long spinStartTime = 0;
unsigned long spinLastTickMs = 0;
unsigned long idleTimer = 0;
unsigned long lastFrameTime = 0;
unsigned long lastBtnActionMs = 0;
const unsigned long BTN_COOLDOWN_MS = 105;
unsigned long testExitLockUntil = 0;

unsigned long selectPressStartMs = 0;
bool selectLongPressHandled = false;
const unsigned long LONG_PRESS_MS = 550;

float reelOffset1 = 0, reelOffset2 = 0, reelOffset3 = 0;
bool reel1Stopped = false, reel2Stopped = false, reel3Stopped = false;
float bounce1 = 0, bounce2 = 0, bounce3 = 0;

bool upPressed = false, downPressed = false, leftPressed = false, rightPressed = false, selectPressed = false;

const char WEB_PAGE_HTML[] PROGMEM = R"=====(<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP32 Console Hub</title><style>
*{box-sizing:border-box;}
:root{color-scheme:dark;--bg:#080c15;--card:#111827;--line:rgba(148,163,184,.18);--accent:#3b82f6;--teal:#14b8a6;--gold:#f59e0b;}
html{scroll-behavior:smooth;}
body{
  font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;
  margin:0;min-height:100vh;padding:clamp(12px,3vw,24px);
  background:
    radial-gradient(ellipse at 12% 0%,rgba(59,130,246,.24),transparent 44%),
    radial-gradient(ellipse at 92% 15%,rgba(20,184,166,.16),transparent 40%),
    radial-gradient(ellipse at 50% 100%,rgba(245,158,11,.10),transparent 45%),
    var(--bg);
  color:#e8eef9;overflow-x:hidden;
}
.wrap{max-width:860px;margin:auto;position:relative;}
.card{
  position:relative;overflow:hidden;
  background:linear-gradient(145deg,rgba(23,33,51,.96),rgba(12,18,30,.98));
  padding:clamp(14px,2.6vw,20px);border-radius:18px;margin-bottom:14px;
  border:1px solid var(--line);
  box-shadow:0 14px 36px rgba(0,0,0,.32),inset 0 1px 0 rgba(255,255,255,.04);
}
.heroCard{
  background:linear-gradient(125deg,rgba(24,43,75,.98),rgba(14,24,41,.98));
  border-color:rgba(96,165,250,.32);
}
.brandHead{display:flex;align-items:center;gap:12px;margin-bottom:13px;}
.brandMark{
  width:46px;height:46px;flex:0 0 46px;display:grid;place-items:center;border-radius:15px;
  color:#dbeafe;font-size:24px;background:linear-gradient(145deg,#2563eb,#0d9488);
  border:1px solid rgba(191,219,254,.35);
}
.brandCopy{min-width:0;flex:1;}
h1{margin:0;font-size:clamp(20px,4vw,25px);display:flex;align-items:center;gap:8px;flex-wrap:wrap;}
.badge{font-size:11px;background:rgba(15,23,42,.8);color:#7dd3fc;border:1px solid rgba(56,189,248,.35);padding:3px 9px;border-radius:999px;}
.timerBadge{font-size:11px;background:rgba(20,184,166,.16);color:#5eead4;border:1px solid rgba(45,212,191,.4);padding:3px 9px;border-radius:999px;}
.brandSub{margin-top:3px;font-size:9px;letter-spacing:2.1px;color:#8ba3c4;font-weight:800;}
.liveMark{display:flex;align-items:center;gap:7px;color:#6ee7b7;font-size:10px;font-weight:800;letter-spacing:1.2px;white-space:nowrap;}
.liveMark span{width:8px;height:8px;border-radius:50%;background:#34d399;}
h3{margin:0 0 11px;font-size:15px;color:#e2e8f0;}
.meta{display:flex;flex-wrap:wrap;gap:7px;margin-top:10px;}
.pill{background:rgba(8,15,27,.65);border:1px solid rgba(100,116,139,.26);padding:7px 11px;border-radius:999px;font-size:13px;color:#b8c7dc;}
.pill b{color:#93c5fd;}
.status{margin-top:10px;padding:9px 12px;border:1px solid rgba(100,116,139,.2);border-radius:11px;background:rgba(5,10,18,.38);font-size:13px;color:#94a3b8;}
.tabs{
  position:sticky;top:8px;z-index:8;display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:6px;
  margin-bottom:14px;padding:6px;border:1px solid rgba(100,116,139,.22);border-radius:15px;
  background:rgba(10,15,25,.94);
}
.tabBtn{min-width:0;padding:10px 8px;border-radius:10px;border:1px solid transparent;background:transparent;color:#cbd5e1;font-weight:600;cursor:pointer;}
.tabBtn.active{background:linear-gradient(135deg,#2563eb,#1d4ed8);border-color:rgba(147,197,253,.35);color:#fff;}
.tabBtn.hiddenTab{display:none;}
.pane{display:none;}
.pane.active{display:block;}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:9px;}
.recGrid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:10px;}
.recBox label{display:block;font-size:12px;color:#94a3b8;margin-bottom:4px;}
button,a.btn{
  display:inline-flex;align-items:center;justify-content:center;text-decoration:none;width:100%;min-height:43px;
  padding:10px 12px;font-size:13px;font-weight:600;border-radius:11px;border:1px solid transparent;
  background:#2563eb;color:#fff;cursor:pointer;user-select:none;-webkit-tap-highlight-color:transparent;
  transition:transform .08s ease,filter .15s ease;
}
button:hover,a.btn:hover{filter:brightness(1.07);}
button:active,button.pressed,a.btn:active{transform:scale(.97);}
button:disabled{opacity:.55;cursor:not-allowed;}
button.sec,a.btn.sec{background:linear-gradient(180deg,#202b3e,#182131);border-color:#34435c;color:#e2e8f0;}
button.gold{background:linear-gradient(135deg,#f59e0b,#fbbf24);border-color:#fcd34d;color:#1e1304;}
button.teal{background:linear-gradient(135deg,#0f766e,#0d9488);border-color:#2dd4bf;color:#f0fdfa;}
button.plus{width:auto;min-height:36px;padding:6px 12px;font-size:15px;border-radius:9px;}
button.del{width:auto;min-height:34px;padding:6px 10px;font-size:12px;background:#2a1920;border-color:#5c2836;color:#fda4af;}
button.playBtn{width:auto;min-height:34px;padding:6px 11px;font-size:12px;background:#166534;border-color:#22c55e;color:#dcfce7;}
button.editBtn{width:auto;min-height:34px;padding:6px 10px;font-size:12px;background:#1e293b;border-color:#38bdf8;color:#bae6fd;}

.clickerCard{background:linear-gradient(145deg,#16263a,#0d1522);border-color:rgba(45,212,191,.28);}
.cStat{background:rgba(7,12,22,.68);border:1px solid rgba(100,116,139,.25);border-radius:12px;padding:12px;text-align:center;margin-bottom:12px;}
.cStat span{display:block;font-size:12px;color:#94a3b8;margin-bottom:4px;}
.cStat b{font-size:24px;color:#5eead4;}
.tapBtn{
  min-height:62px;font-size:18px;font-weight:800;border-radius:16px;
  background:linear-gradient(135deg,#0d9488,#2563eb);border:1px solid rgba(153,246,228,.45);
}
.slotCard{background:linear-gradient(145deg,#18243a,#0e1522);border-color:rgba(96,165,250,.3);}
.slotHeading{display:flex;align-items:center;justify-content:space-between;gap:12px;}
.slotHeading h3{margin:2px 0 0;font-size:16px;}
.eyebrow{font-size:9px;font-weight:800;letter-spacing:2px;color:#7dd3fc;}
.reels{display:flex;justify-content:center;gap:14px;margin:16px 0;padding:14px 8px;border-radius:16px;border:1px solid rgba(96,165,250,.2);background:rgba(4,9,19,.72);}
.reel{width:72px;height:72px;display:flex;align-items:center;justify-content:center;font-size:36px;background:#111a2c;border:1px solid rgba(125,211,252,.35);border-radius:16px;}

.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;}
.octRow{display:flex;gap:6px;margin-bottom:10px;flex-wrap:wrap;}
.octBtn{flex:1;min-width:68px;min-height:36px;padding:7px 6px;border-radius:10px;border:1px solid #2e3950;background:#151a25;color:#cbd5e1;font-weight:600;font-size:12px;}
.octBtn.active{background:#0284c7;border-color:#38bdf8;color:#fff;}
.pianoScroll{overflow-x:auto;background:#090c11;border:1px solid #263147;border-radius:14px;padding:12px;}
.pianoStage{position:relative;height:142px;min-width:560px;user-select:none;}
.whiteRow{display:flex;height:138px;gap:2px;}
.wKey{position:relative;flex:1;height:100%;min-height:100%;padding:0 2px 8px;border-radius:0 0 8px 8px;background:linear-gradient(180deg,#f8fafc,#e2e8f0);border:1px solid #94a3b8;color:#0f172a;font-size:11px;font-weight:700;display:flex;flex-direction:column;justify-content:flex-end;align-items:center;}
.wKey:active,.wKey.pressed{transform:translateY(2px);background:#bae6fd;}
.bKey{position:absolute;top:0;width:28px;height:84px;min-height:84px;margin-left:-14px;z-index:2;padding:0 1px 6px;border-radius:0 0 6px 6px;background:linear-gradient(180deg,#1e293b,#090d16);border:1px solid #475569;color:#e2e8f0;font-size:10px;font-weight:700;display:flex;flex-direction:column;justify-content:flex-end;align-items:center;}
.bKey:active,.bKey.pressed{transform:translateY(2px);background:#0284c7;color:#fff;}
.pauseBar{margin-top:10px;height:44px;background:#1e293b;border:1px solid #64748b;color:#f8fafc;font-size:13px;font-weight:700;border-radius:10px;}
.pianoInfo{margin-top:10px;font-size:13px;color:#93c5fd;text-align:center;}
.seqBox,.backupBox,.noteItem{background:rgba(7,12,21,.74);border:1px solid #2d3b53;border-radius:13px;padding:10px 12px;margin-top:8px;}
.seqBox{min-height:60px;font-size:13px;line-height:1.5;}
.chip{display:inline-flex;align-items:center;gap:4px;background:#182030;border:1px solid #2e3b55;padding:3px 8px;border-radius:8px;margin:2px;font-size:12px;cursor:pointer;}
.chip.sel{border-color:#f59e0b;background:#2a2212;color:#fde68a;}
.backupBox{display:none;}.backupBox.open{display:block;}
.headRow{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;}.headRow h3{margin:0;}
input,textarea,select{width:100%;padding:10px 12px;font-size:14px;font-family:inherit;border-radius:11px;border:1px solid #35445e;background:#0a111e;color:#fff;margin-bottom:8px;}
textarea{min-height:105px;resize:vertical;}
.quick{display:flex;gap:6px;margin-top:8px;}
.quick button{padding:7px 10px;min-height:36px;font-size:12px;background:#1a2130;border-color:#2c374d;}
.noteTop{display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap;}
.noteTopic{font-weight:700;font-size:14px;color:#93c5fd;}
.noteBody{margin-top:6px;font-size:13px;color:#cbd5e1;white-space:pre-wrap;line-height:1.35;}
.emptyScroll{height:1600px;}
.secretZone{display:flex;flex-direction:column;gap:10px;}
button.secretGod{background:#5c1d24;border-color:#9f1239;color:#ffe4e6;}
button.secretOff{background:#3b2b10;border-color:#a16207;color:#fef08a;}
@media(max-width:520px){
  .tabs{grid-template-columns:repeat(2,minmax(0,1fr));}
  .grid{grid-template-columns:repeat(2,minmax(0,1fr));}
  .emptyScroll{height:850px;}
}
</style></head><body>
<div class='wrap'>
<div class='card heroCard'>
  <div class='brandHead'>
    <div class='brandMark'>✦</div>
    <div class='brandCopy'>
      <h1>ESP32 Console <span class='badge' id='wVer'></span> <span class='timerBadge' id='wTimerBadge' style='display:none;'>⏱ 00:00</span></h1>
      <div class='brandSub'>PERSONAL MINI CONSOLE HUB</div>
    </div>
    <div class='liveMark'><span></span> ONLINE</div>
  </div>
  <div class='meta'>
    <div class='pill'>Режим: <b id='wState'></b></div>
    <div class='pill'>IP: <b id='wIp'></b></div>
    <div class='pill'>Монет: <b id='wCoins'></b></div>
    <div class='pill'>Счётчик: <b id='wClicksTop'>0</b></div>
    <div class='pill'>Спинов: <b id='wSpins'></b></div>
  </div>
  <div class='status' id='wStatus'></div>
</div>

<div class='tabs'>
  <button type='button' class='tabBtn active' id='tabGamesBtn' onclick="switchTab('games')">Игры и пульт</button>
  <button type='button' class='tabBtn' id='tabPianoBtn' onclick="switchTab('piano')">Музыка</button>
  <button type='button' class='tabBtn' id='tabNotesBtn' onclick="switchTab('notes')">Шпаргалки</button>
  <button type='button' class='tabBtn hiddenTab' id='tabRecordsBtn' onclick="switchTab('records')">Рекорды</button>
</div>

<div class='pane active' id='paneGames'>
  <div class='card clickerCard'>
    <div class='slotHeading'>
      <div><div class='eyebrow'>SIMPLE COUNTER / +1</div><h3>Счётчик (кликер)</h3></div>
      <button type='button' class='sec' style='width:auto;min-height:36px;padding:6px 12px;' onclick="sendMode('clicker')">Открыть на экране</button>
    </div>
    <div class='cStat' style='margin-top:10px;'>
      <span>Текущее значение</span>
      <b id='cTotal'>0</b>
    </div>
    <div class='row' style='gap:8px;'>
      <button type='button' class='tapBtn' id='tapBtn' onclick='doWebClick()' style='flex:2;'>+1</button>
      <button type='button' class='sec' onclick='resetWebClick()' style='flex:1;min-height:62px;'>Сбросить</button>
    </div>
  </div>

  <div class='card'>
    <h3>Фоновый таймер (работает при выходе из меню)</h3>
    <div class='quick' style='margin-top:0;margin-bottom:8px;'>
      <button type='button' onclick='startWebTimer(60)'>1 мин</button>
      <button type='button' onclick='startWebTimer(300)'>5 мин</button>
      <button type='button' onclick='startWebTimer(600)'>10 мин</button>
      <button type='button' class='sec' onclick='stopWebTimer()'>Стоп</button>
    </div>
    <div class='row'>
      <input type='number' id='timerSecInput' min='1' max='86400' value='30' placeholder='Секунд...' style='margin:0;flex:1;min-width:130px;'>
      <button type='button' class='teal' onclick='startCustomTimer()' style='width:auto;padding:10px 16px;'>Запустить</button>
    </div>
  </div>

  <div class='card'>
    <h3>Управление экраном приставки</h3>
    <div class='grid'>
      <button class='sec' onclick="sendMode('menu')">Главное меню</button>
      <button onclick="sendMode('slots')">Слоты</button>
      <button onclick="sendMode('snake')">Змейка</button>
      <button onclick="sendMode('tetris')">Тетрис</button>
      <button onclick="sendMode('bottle')">Баттл Флип</button>
      <button onclick="sendMode('flappy')">Flappy Bird</button>
      <button onclick="sendMode('pong')">Понг</button>
      <button onclick="sendMode('dino')">Динозаврик</button>
      <button onclick="sendMode('doodle')">Doodle Jump</button>
      <button class='teal' onclick="sendMode('clicker')">Счётчик</button>
      <button class='sec' onclick="sendMode('records')">Рекорды</button>
      <button class='sec' onclick="sendMode('clock')">Часы и погода</button>
      <button class='sec' onclick="sendMode('music')">Музыка</button>
      <button class='sec' onclick="sendMode('metro')">Метроном</button>
      <button class='sec' onclick="sendMode('calc')">Калькулятор</button>
      <button class='sec' onclick="sendMode('piano')">Плеер песен</button>
      <button class='sec' onclick="sendMode('notes')">Шпаргалки</button>
      <a class='btn sec' href='/update'>Загрузить .bin</a>
    </div>
  </div>

  <div class='card'>
    <h3>Утилиты и приколюхи</h3>
    <div class='row' style='margin-bottom:8px;'>
      <input type='text' id='bannerInput' maxlength='48' placeholder='Текст на экран приставки...' style='margin:0;flex:1;min-width:160px;'>
      <button type='button' onclick='sendBanner()' style='width:auto;padding:10px 16px;'>На экран</button>
    </div>
    <div class='row'>
      <button type='button' class='teal' onclick='pingBoard()' style='flex:1;'>🔊 Найти консоль</button>
      <button type='button' class='sec' onclick='sendPrankJoke()' style='flex:1;'>🎲 Прикол на экран</button>
    </div>
  </div>

  <div class='card slotCard'>
    <div class='slotHeading'>
      <div><div class='eyebrow'>ARCADE / SLOTS</div><h3>Слоты по Wi-Fi</h3></div>
    </div>
    <div class='reels'>
      <div class='reel' id='r1'>🚀</div>
      <div class='reel' id='r2'>🍏</div>
      <div class='reel' id='r3'>🌙</div>
    </div>
    <button class='gold' id='spinBtn' onclick='doSpin()'>Крутить спин (10 монет)</button>
  </div>

  <div class='card'>
    <h3>Начислить монеты</h3>
    <div class='row'>
      <input type='number' id='coinInput' min='1' max='100000' value='100' style='margin:0;flex:1;min-width:130px;'>
      <button onclick='addCoins()' style='width:auto;padding:10px 18px;'>Начислить</button>
    </div>
    <div class='quick'>
      <button type='button' onclick='quickCoins(50)'>+50</button>
      <button type='button' onclick='quickCoins(500)'>+500</button>
      <button type='button' onclick='quickCoins(5000)'>+5000</button>
    </div>
  </div>

  <div class='emptyScroll'></div>

  <div class='card secretZone'>
    <button type='button' class='secretGod' onclick='toggleGod()'>Режим бога</button>
    <button type='button' class='secretOff' onclick='toggleDisable()'>Отключить</button>
  </div>
</div>

<div class='pane' id='panePiano'>
  <div class='card'>
    <h3>Музыка: пианино и редактор песен (2 октавы)</h3>
    <label for='songTitle' style='display:block;font-size:12px;color:#94a3b8;margin-bottom:4px;'>Название песни</label>
    <input type='text' id='songTitle' maxlength='32' placeholder='Например: Ноктюрн в До миноре'>
    <div class='octRow'>
      <button type='button' class='octBtn' id='octSpan2' onclick='setOctSpan(2)'>Окт 2-3</button>
      <button type='button' class='octBtn active' id='octSpan3' onclick='setOctSpan(3)'>Окт 3-4</button>
      <button type='button' class='octBtn' id='octSpan4' onclick='setOctSpan(4)'>Окт 4-5</button>
      <button type='button' class='octBtn' id='octSpan5' onclick='setOctSpan(5)'>Окт 5-6</button>
    </div>
    <div class='row' style='margin-bottom:10px;'>
      <select id='durSelect' style='margin:0;flex:1;min-width:150px;'>
        <option value='130'>1/8 Короткая (130 мс)</option>
        <option value='260' selected>1/4 Обычная (260 мс)</option>
        <option value='520'>1/2 Длинная (520 мс)</option>
      </select>
      <button type='button' class='sec' onclick='repeatLastStep()' style='width:auto;padding:10px 12px;'>Повторить</button>
      <button type='button' class='sec' onclick='exportSongsJson()' style='width:auto;padding:10px 12px;'>Бэкап JSON</button>
      <button type='button' class='sec' onclick='toggleBackupBox()' style='width:auto;padding:10px 12px;'>Загрузить бэкап</button>
    </div>
    <div class='backupBox' id='backupBox'>
      <input type='file' id='backupFileInput' accept='.json,application/json' onchange='onBackupFilePicked(this)'>
      <textarea id='backupJsonText' placeholder='[{"title":"Мелодия","steps":[{"freq":440,"dur":260}]}]'></textarea>
      <div class='row'>
        <button type='button' class='teal' onclick='importBackupToBoard()' style='flex:1;'>Залить бэкап на консоль</button>
        <button type='button' class='sec' onclick='toggleBackupBox()' style='width:auto;padding:10px 14px;'>Закрыть</button>
      </div>
    </div>
    <div class='pianoScroll'><div class='pianoStage' id='pianoStage'></div></div>
    <button type='button' class='pauseBar' onpointerdown='pressPauseKey(this)'>ПАУЗА (тишина / ритм)</button>
    <div class='seqBox' id='seqPreview'>Нажимай белые и чёрные клавиши или ПАУЗУ — шаги запишутся сюда.</div>
    <div class='row' style='margin-bottom:8px;'>
      <button type='button' class='sec' onclick='moveDraftStep(-1)' style='flex:1;'>&#8592; Влево</button>
      <button type='button' class='sec' onclick='moveDraftStep(1)' style='flex:1;'>Вправо &#8594;</button>
      <button type='button' class='sec' onclick='deleteSelectedDraftStep()' style='flex:1;'>Удалить шаг</button>
      <button type='button' class='sec' onclick='clearSongDraft()' style='flex:1;'>Очистить</button>
    </div>
    <div class='row'>
      <button type='button' class='gold' id='saveSongBtn' onclick='uploadSongToBoard(false)' style='flex:1;'>Сохранить на консоль</button>
      <button type='button' class='teal' id='savePlaySongBtn' onclick='uploadSongToBoard(true)' style='flex:1;'>Сохранить и слушать</button>
    </div>
    <div class='pianoInfo' id='pianoInfo'>Последний шаг: Ля4</div>
  </div>
  <div class='card'>
    <h3>Песни на консоли (/songs.json)</h3>
    <div id='songsListBox'>Загрузка...</div>
  </div>
</div>

<div class='pane' id='paneNotes'>
  <div class='card'>
    <div class='headRow'>
      <h3>Новая шпаргалка</h3>
      <button type='button' class='plus' onclick='focusNewNote()'>+</button>
    </div>
    <input type='text' id='noteTopic' maxlength='40' placeholder='Тема (например: Бел лит)'>
    <textarea id='noteBody' maxlength='2500' placeholder='Вставь текст шпаргалки сюда...'></textarea>
    <button type='button' onclick='uploadNote()'>Загрузить</button>
  </div>
  <div class='card'>
    <h3>Сохранённые темы на консоли</h3>
    <div id='notesListBox'>Загрузка...</div>
  </div>
</div>

<div class='pane' id='paneRecords'>
  <div class='card'>
    <h3>Редактор рекордов (Режим бога)</h3>
    <div class='recGrid'>
      <div class='recBox'><label>Змейка</label><input type='number' id='recSnakeInp' min='0' max='99999' value='0'></div>
      <div class='recBox'><label>Тетрис</label><input type='number' id='recTetrisInp' min='0' max='99999' value='0'></div>
      <div class='recBox'><label>Flappy Bird</label><input type='number' id='recFlappyInp' min='0' max='99999' value='0'></div>
      <div class='recBox'><label>Понг</label><input type='number' id='recPongInp' min='0' max='99999' value='0'></div>
      <div class='recBox'><label>Динозаврик</label><input type='number' id='recDinoInp' min='0' max='99999' value='0'></div>
      <div class='recBox'><label>Баттл Флип</label><input type='number' id='recBottleInp' min='0' max='99999' value='0'></div>
      <div class='recBox'><label>Doodle Jump</label><input type='number' id='recDoodleInp' min='0' max='99999' value='0'></div>
    </div>
    <button type='button' class='gold' onclick='saveRecordsWeb()'>Сохранить рекорды</button>
  </div>
</div>
</div>

<script>
const syms = ['🍏','🍆','🌙','🍇','✖️','🚀'];
const semitoneNames = ['До','До#','Ре','Ре#','Ми','Фа','Фа#','Соль','Соль#','Ля','Ля#','Си'];
const isBlackSemi = [false,true,false,true,false,false,true,false,true,false,true,false];
const jokes = ['НЕ ТРОГАЙ МЕНЯ!', 'ТАПАЙ БОЛЬШЕ!', 'АХ ТЫ Ж ХИТРЕЦ', 'ПРОВЕРКА СВЯЗИ OK', 'РЕЖИМ ЧИЛЛА'];
let animTimer = null, baseOctave = 3, draftSteps = [], selDraftIdx = -1, latestSongs = [], activeTab = 'games', lastRev = -1, godActive = false;

function syncGodUi(isGod) {
  godActive = Boolean(isGod);
  const recBtn = document.getElementById('tabRecordsBtn');
  if (recBtn) recBtn.classList.toggle('hiddenTab', !godActive);
  if (!godActive && activeTab === 'records') switchTab('games');
}

function setStatusMsg(msg, isErr) {
  const el = document.getElementById('wStatus');
  if (!el) return;
  el.textContent = msg;
  el.style.color = isErr ? '#fca5a5' : '#94a3b8';
}

function switchTab(t) {
  if (t === 'records' && !godActive) t = 'games';
  activeTab = t;
  ['Games','Piano','Notes','Records'].forEach(name => {
    const k = name.toLowerCase();
    const b = document.getElementById('tab' + name + 'Btn');
    const p = document.getElementById('pane' + name);
    if (b) b.classList.toggle('active', k === t);
    if (p) p.classList.toggle('active', k === t);
  });
  if (t === 'piano') { buildPianoKeyboard(); loadSongsList(); }
  if (t === 'notes') { loadNotesList(); }
}

function midiToFreqJs(m) { return Math.max(65, Math.min(2400, Math.round(440 * Math.pow(2, (m - 69) / 12)))); }
function midiToLabelJs(m) { return semitoneNames[((m % 12) + 12) % 12] + (Math.floor(m / 12) - 1); }

function setOctSpan(o) {
  baseOctave = o;
  [2,3,4,5].forEach(x => document.getElementById('octSpan' + x).classList.toggle('active', x === o));
  buildPianoKeyboard();
}

function buildPianoKeyboard() {
  const stage = document.getElementById('pianoStage');
  if (!stage) return;
  const startMidi = (baseOctave + 1) * 12, endMidi = startMidi + 24, whites = [];
  for (let m = startMidi; m <= endMidi; m++) if (!isBlackSemi[((m % 12) + 12) % 12]) whites.push({midi: m, label: midiToLabelJs(m)});
  const totalW = whites.length;
  let htmlKeys = `<div class='whiteRow'>` + whites.map(w => `<button type='button' class='wKey' onpointerdown='pressMidiKey(${w.midi}, this)'><span>${w.label}</span></button>`).join('') + `</div>`;
  for (let m = startMidi; m < endMidi; m++) {
    if (isBlackSemi[((m % 12) + 12) % 12]) {
      let leftWhites = 0;
      for (let k = startMidi; k < m; k++) if (!isBlackSemi[((k % 12) + 12) % 12]) leftWhites++;
      htmlKeys += `<button type='button' class='bKey' style='left:${((leftWhites / totalW) * 100).toFixed(3)}%' onpointerdown='event.stopPropagation();pressMidiKey(${m}, this)'><span>${midiToLabelJs(m)}</span></button>`;
    }
  }
  stage.innerHTML = htmlKeys;
}

function flashKey(el) { if (!el) return; el.classList.add('pressed'); setTimeout(() => el.classList.remove('pressed'), 120); }
function escHtml(s) { return String(s || '').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }

function renderDraftSteps() {
  const box = document.getElementById('seqPreview');
  if (!box) return;
  if (draftSteps.length === 0) { selDraftIdx = -1; box.textContent = 'Нажимай клавиши или ПАУЗУ — шаги запишутся сюда.'; return; }
  if (selDraftIdx < 0 || selDraftIdx >= draftSteps.length) selDraftIdx = draftSteps.length - 1;
  box.innerHTML = `<div style='font-size:11px;color:#94a3b8;margin-bottom:4px;'>Шагов: ${draftSteps.length}</div>` +
    draftSteps.map((s, idx) => `<span class='chip${idx === selDraftIdx ? ' sel' : ''}' onclick='pickDraftStep(${idx})'>${idx+1}. ${escHtml(s.label)}</span>`).join('');
}

function pickDraftStep(idx) { if (idx >= 0 && idx < draftSteps.length) { selDraftIdx = idx; renderDraftSteps(); } }
function moveDraftStep(dir) {
  if (selDraftIdx < 0 || draftSteps.length < 2) return;
  const ni = selDraftIdx + dir;
  if (ni < 0 || ni >= draftSteps.length) return;
  const tmp = draftSteps[selDraftIdx]; draftSteps[selDraftIdx] = draftSteps[ni]; draftSteps[ni] = tmp; selDraftIdx = ni; renderDraftSteps();
}
function deleteSelectedDraftStep() { if (selDraftIdx >= 0) { draftSteps.splice(selDraftIdx, 1); selDraftIdx = draftSteps.length - 1; renderDraftSteps(); } }
function clearSongDraft() { draftSteps = []; selDraftIdx = -1; renderDraftSteps(); }

async function pressMidiKey(midiNote, el) {
  flashKey(el);
  const dur = parseInt(document.getElementById('durSelect').value, 10) || 260;
  const freq = midiToFreqJs(midiNote), label = midiToLabelJs(midiNote);
  draftSteps.push({freq, dur, label}); selDraftIdx = draftSteps.length - 1; renderDraftSteps();
  try { const res = await fetch('/piano', {method:'POST', body: new URLSearchParams({midi: midiNote, freq, dur})}); if (res.ok) applyData(await res.json(), false); } catch (e) {}
}

async function pressPauseKey(el) {
  flashKey(el);
  const dur = parseInt(document.getElementById('durSelect').value, 10) || 260;
  draftSteps.push({freq: 0, dur, label: 'Пауза'}); selDraftIdx = draftSteps.length - 1; renderDraftSteps();
  try { const res = await fetch('/piano', {method:'POST', body: new URLSearchParams({pause: '1', dur})}); if (res.ok) applyData(await res.json(), false); } catch (e) {}
}

async function repeatLastStep() {
  if (draftSteps.length === 0) return;
  const last = draftSteps[draftSteps.length - 1];
  draftSteps.push({...last}); selDraftIdx = draftSteps.length - 1; renderDraftSteps();
  try {
    const body = (last.freq === 0) ? new URLSearchParams({pause: '1', dur: last.dur || 260}) : new URLSearchParams({freq: last.freq, dur: last.dur || 260});
    const res = await fetch('/piano', {method:'POST', body}); if (res.ok) applyData(await res.json(), false);
  } catch (e) {}
}

function exportSongsJson() {
  const a = document.createElement('a');
  a.href = 'data:application/json;charset=utf-8,' + encodeURIComponent(JSON.stringify(latestSongs, null, 2));
  a.download = 'esp32_songs_backup.json'; document.body.appendChild(a); a.click(); a.remove();
}
function toggleBackupBox() { document.getElementById('backupBox').classList.toggle('open'); }
function onBackupFilePicked(inp) {
  const f = inp.files && inp.files[0]; if (!f) return;
  const r = new FileReader(); r.onload = () => { document.getElementById('backupJsonText').value = String(r.result || ''); }; r.readAsText(f, 'utf-8');
}
async function importBackupToBoard() {
  const raw = document.getElementById('backupJsonText').value.trim(); if (!raw) return;
  try { const res = await fetch('/songs/import', {method:'POST', body: new URLSearchParams({json: raw})}); if (res.ok) { applyData(await res.json(), true); document.getElementById('backupBox').classList.remove('open'); } } catch (e) {}
}
function loadSongIntoEditor(idx) {
  const s = latestSongs[idx]; if (!s || !Array.isArray(s.steps)) return;
  document.getElementById('songTitle').value = s.title || '';
  draftSteps = s.steps.map(x => ({freq: x.freq, dur: x.dur, label: x.label || (x.freq > 0 ? x.freq + 'Гц' : 'Пауза')}));
  selDraftIdx = draftSteps.length > 0 ? 0 : -1; switchTab('piano'); renderDraftSteps();
}
async function uploadSongToBoard(playAfter) {
  const title = document.getElementById('songTitle').value.trim();
  if (!title || draftSteps.length === 0) return;
  try {
    const res = await fetch('/songs/add', {method:'POST', body: new URLSearchParams({title, seq: draftSteps.map(s => s.freq + ':' + s.dur).join(','), play: playAfter ? '1' : '0'})});
    if (res.ok) { const d = await res.json(); applyData(d, true); document.getElementById('songTitle').value = ''; clearSongDraft(); }
  } catch (e) {}
}
async function playSongOnBoard(idx) { try { const res = await fetch('/songs/play', {method:'POST', body: new URLSearchParams({idx})}); if (res.ok) applyData(await res.json(), false); } catch (e) {} }
async function deleteSongOnBoard(idx) { try { const res = await fetch('/songs/delete', {method:'POST', body: new URLSearchParams({idx})}); if (res.ok) applyData(await res.json(), true); } catch (e) {} }
function focusNewNote() { document.getElementById('noteTopic').value = ''; document.getElementById('noteBody').value = ''; document.getElementById('noteTopic').focus(); }

function renderSongs(list) {
  latestSongs = Array.isArray(list) ? list : [];
  const box = document.getElementById('songsListBox');
  if (!box) return;
  if (latestSongs.length === 0) { box.innerHTML = "<div style='color:#94a3b8;font-size:13px;'>Пока песен нет.</div>"; return; }
  box.innerHTML = latestSongs.map((s, i) => `<div class='noteItem'><div class='noteTop'><span class='noteTopic'>${escHtml(s.title)} (${s.count} шагов)</span><div class='row' style='gap:6px;'><button type='button' class='playBtn' onclick='playSongOnBoard(${i})'>Слушать</button><button type='button' class='editBtn' onclick='loadSongIntoEditor(${i})'>В пианино</button><button type='button' class='del' onclick='deleteSongOnBoard(${i})'>Удалить</button></div></div></div>`).join('');
}

function renderNotes(list) {
  const box = document.getElementById('notesListBox');
  if (!box) return;
  if (!Array.isArray(list) || list.length === 0) { box.innerHTML = "<div style='color:#94a3b8;font-size:13px;'>Пока тем нет.</div>"; return; }
  box.innerHTML = list.map((n, i) => `<div class='noteItem'><div class='noteTop'><span class='noteTopic'>${escHtml(n.topic)}</span><button type='button' class='del' onclick='deleteNote(${i})'>Удалить</button></div><div class='noteBody'>${escHtml(n.body)}</div></div>`).join('');
}

async function loadNotesList() { try { const res = await fetch('/notes'); if (res.ok) renderNotes(await res.json()); } catch (e) {} }
async function loadSongsList() { try { const res = await fetch('/songs'); if (res.ok) renderSongs(await res.json()); } catch (e) {} }

function syncRecordsInputs(d) {
  const active = document.activeElement;
  if (active && active.id && active.id.startsWith('rec')) return;
  if (typeof d.recSnake === 'number') document.getElementById('recSnakeInp').value = d.recSnake;
  if (typeof d.recTetris === 'number') document.getElementById('recTetrisInp').value = d.recTetris;
  if (typeof d.recFlappy === 'number') document.getElementById('recFlappyInp').value = d.recFlappy;
  if (typeof d.recPong === 'number') document.getElementById('recPongInp').value = d.recPong;
  if (typeof d.recDino === 'number') document.getElementById('recDinoInp').value = d.recDino;
  if (typeof d.recBottle === 'number') document.getElementById('recBottleInp').value = d.recBottle;
  if (typeof d.recDoodle === 'number') document.getElementById('recDoodleInp').value = d.recDoodle;
}

async function saveRecordsWeb() {
  const body = new URLSearchParams({ snake: document.getElementById('recSnakeInp').value || 0, tetris: document.getElementById('recTetrisInp').value || 0, flappy: document.getElementById('recFlappyInp').value || 0, pong: document.getElementById('recPongInp').value || 0, dino: document.getElementById('recDinoInp').value || 0, bottle: document.getElementById('recBottleInp').value || 0, doodle: document.getElementById('recDoodleInp').value || 0 });
  try { const res = await fetch('/records/set', {method:'POST', body}); if (res.ok) applyData(await res.json(), false); } catch (e) {}
}

function formatSecBadge(sec) {
  const s = Math.max(0, Number(sec) || 0);
  const m = Math.floor(s / 60);
  const r = s % 60;
  return '⏱ ' + String(m).padStart(2, '0') + ':' + String(r).padStart(2, '0');
}

function applyData(d, reloadLists) {
  if (typeof d.rev === 'number') lastRev = d.rev;
  if (typeof d.ver === 'string') document.getElementById('wVer').textContent = d.ver;
  if (typeof d.ip === 'string') document.getElementById('wIp').textContent = d.ip;
  if (typeof d.god === 'boolean') syncGodUi(d.god);
  document.getElementById('wState').textContent = d.state;
  document.getElementById('wCoins').textContent = d.god ? '∞ (БОГ)' : d.coins;
  document.getElementById('wSpins').textContent = d.totalSpins;
  if (typeof d.clicks === 'number') {
    document.getElementById('wClicksTop').textContent = d.clicks;
    document.getElementById('cTotal').textContent = d.clicks;
  }
  const tBadge = document.getElementById('wTimerBadge');
  if (tBadge) {
    if (d.timerRunning && typeof d.timerSec === 'number' && d.timerSec > 0) {
      tBadge.style.display = 'inline-block';
      tBadge.textContent = formatSecBadge(d.timerSec);
    } else {
      tBadge.style.display = 'none';
    }
  }
  syncRecordsInputs(d);
  setStatusMsg(d.status || '', Boolean(d.status && d.status.toLowerCase().includes('ошиб')));
  if (d.pianoNote) document.getElementById('pianoInfo').textContent = d.pianoFreq > 0 ? ('Последняя нота: ' + d.pianoNote) : 'Последний шаг: Пауза';
  if (reloadLists) { if (activeTab === 'piano') loadSongsList(); if (activeTab === 'notes') loadNotesList(); }
  if (!d.spinning) {
    if (animTimer) { clearInterval(animTimer); animTimer = null; }
    document.getElementById('r1').textContent = d.s1; document.getElementById('r2').textContent = d.s2; document.getElementById('r3').textContent = d.s3;
    document.getElementById('spinBtn').disabled = false;
  } else {
    document.getElementById('spinBtn').disabled = true;
    if (!animTimer) animTimer = setInterval(() => { ['r1','r2','r3'].forEach(id => document.getElementById(id).textContent = syms[Math.floor(Math.random()*syms.length)]); }, 85);
  }
}

async function refreshStatus(force) {
  if (document.hidden && !force) return;
  try { const res = await fetch('/status'); if (!res.ok) return; const d = await res.json(); if (!force && d.rev === lastRev && !d.timerRunning) return; applyData(d, true); } catch (e) {}
}

async function doWebClick() { try { const res = await fetch('/clicker/tap', {method:'POST'}); if (res.ok) applyData(await res.json(), false); } catch (e) {} }
async function resetWebClick() { try { const res = await fetch('/clicker/reset', {method:'POST'}); if (res.ok) applyData(await res.json(), false); } catch (e) {} }
async function startWebTimer(sec) { try { const res = await fetch('/timer/set', {method:'POST', body: new URLSearchParams({sec})}); if (res.ok) applyData(await res.json(), false); } catch (e) {} }
async function startCustomTimer() { const v = parseInt(document.getElementById('timerSecInput').value, 10) || 10; startWebTimer(v); }
async function stopWebTimer() { try { const res = await fetch('/timer/stop', {method:'POST'}); if (res.ok) applyData(await res.json(), false); } catch (e) {} }
async function pingBoard() { try { const res = await fetch('/ping', {method:'POST'}); if (res.ok) applyData(await res.json(), false); } catch (e) {} }
async function sendBanner() {
  const txt = document.getElementById('bannerInput').value.trim(); if (!txt) return;
  try { const res = await fetch('/banner', {method:'POST', body: new URLSearchParams({text: txt})}); if (res.ok) { document.getElementById('bannerInput').value = ''; applyData(await res.json(), false); } } catch (e) {}
}
async function sendPrankJoke() {
  const txt = jokes[Math.floor(Math.random() * jokes.length)];
  try { const res = await fetch('/banner', {method:'POST', body: new URLSearchParams({text: txt})}); if (res.ok) applyData(await res.json(), false); } catch (e) {}
}
async function toggleGod() { try { const res = await fetch('/god', {method:'POST'}); if (res.ok) applyData(await res.json(), false); } catch (e) {} }
async function toggleDisable() { try { const res = await fetch('/disable', {method:'POST'}); if (res.ok) applyData(await res.json(), false); } catch (e) {} }
async function sendMode(m) { try { const res = await fetch('/mode', {method:'POST', body: new URLSearchParams({m})}); if (res.ok) applyData(await res.json(), false); } catch (e) {} }
async function doSpin() { try { const res = await fetch('/spin', {method:'POST'}); if (res.ok) applyData(await res.json(), false); } catch (e) {} }
async function addCoins(customVal) { const val = customVal || document.getElementById('coinInput').value; try { const res = await fetch('/coins', {method:'POST', body: new URLSearchParams({amount: val})}); if (res.ok) applyData(await res.json(), false); } catch (e) {} }
function quickCoins(v) { document.getElementById('coinInput').value = v; addCoins(v); }
async function uploadNote() { const topic = document.getElementById('noteTopic').value.trim(); const bodyText = document.getElementById('noteBody').value.trim(); if (!topic) return; try { const res = await fetch('/notes/add', {method:'POST', body: new URLSearchParams({topic, body: bodyText})}); if (res.ok) { document.getElementById('noteTopic').value = ''; document.getElementById('noteBody').value = ''; applyData(await res.json(), true); } } catch (e) {} }
async function deleteNote(idx) { try { const res = await fetch('/notes/delete', {method:'POST', body: new URLSearchParams({idx})}); if (res.ok) applyData(await res.json(), true); } catch (e) {} }

buildPianoKeyboard(); setInterval(() => refreshStatus(false), 1500); refreshStatus(true);
</script></body></html>)=====";

const char OTA_PAGE_HTML[] PROGMEM = R"=====(<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP32 OTA Update</title><style>
body{font-family:system-ui,-apple-system,sans-serif;margin:0;padding:18px;background:#0d0f14;color:#f1f5f9;}
.card{max-width:480px;margin:auto;background:#161b24;padding:18px;border-radius:14px;border:1px solid #283042;}
h2{margin:0 0 8px;font-size:19px;} p{color:#94a3b8;font-size:13px;line-height:1.45;}
input[type=file]{width:100%;margin:14px 0;padding:10px;background:#0b0e14;border:1px solid #333d52;border-radius:10px;color:#e2e8f0;}
button,a{display:inline-block;text-align:center;text-decoration:none;width:100%;padding:11px;margin-top:8px;border-radius:10px;border:0;font-weight:600;font-size:14px;cursor:pointer;}
button{background:#2563eb;color:#fff;} a{background:#1f2634;color:#cbd5e1;} #prog{margin-top:12px;font-size:13px;color:#60a5fa;}
</style></head><body><div class='card'>
  <h2>Обновление ПО (OTA)</h2>
  <p>Выбери скомпилированный файл <b>.bin</b> и нажми «Прошить».</p>
  <form id='otaForm' method='POST' action='/update' enctype='multipart/form-data'>
    <input type='file' name='firmware' accept='.bin' required>
    <button type='submit'>Прошить ESP32</button>
  </form>
  <div id='prog'></div>
  <a href='/'>Назад в панель</a>
</div>
<script>
const f = document.getElementById('otaForm'); const p = document.getElementById('prog');
f.addEventListener('submit', e => { e.preventDefault(); const file = f.firmware.files[0]; if (!file) return; const xhr = new XMLHttpRequest(); xhr.open('POST', '/update', true); xhr.upload.onprogress = ev => { if (ev.lengthComputable) p.textContent = 'Загрузка: ' + Math.round((ev.loaded / ev.total) * 100) + '%'; }; xhr.onload = () => { p.textContent = xhr.responseText || 'Готово! Перезагрузка...'; }; xhr.onerror = () => { p.textContent = 'Ошибка передачи файла.'; }; const fd = new FormData(); fd.append('firmware', file, file.name); xhr.send(fd); });
</script></body></html>)=====";

void loadSettings();
void saveGameData();
void resetSaveData();
void unlockAchievement(uint8_t id);
void applyGodMode(bool enable);
void applyDisabledMode(bool enable);
void startSpin();
void handleSpinning();
void checkWin();
void handleButtons();
void renderDisplay();
void drawHeaderBar(const char* title, const char* badge = nullptr);
void drawFooterHint(const char* leftHint, const char* rightHint = nullptr);
void drawMenuIcon(int menuType, int idx, int x, int y, bool inverted);
void drawMenuRow(int idx, int curIdx, int yPos, const char* label, int menuType = -1);
void drawScrollIndicator(int curIdx, int totalItems);
void drawSymbolAt(int type, int cx, int cy);
void drawBottleGraphic(int cx, int cy, float angle);
void drawArchLogo(int x, int y);
void startArchScreensaver();
void updateArchScreensaver();
void wakeScreen();
void beep(int freq, int duration);
void performClickerTap(bool withSound);
void resetClickerCounter(bool withSound);
void updateClicker(unsigned long now);
int midiToFreq(int midiNote);
String midiToLabel(int midiNote);
void playMidiNote(int midiNote, int durMs = 200);
void triggerPauseStep(int durMs = 240);
void startSongPlayback(int songIdx);
void stopSongPlayback();
void updateSongPlayback(unsigned long now);
void stopMetronome(bool goToMenu = false);
void setMetronomeBpm(int nextBpm);
void updateMetronome(unsigned long now);
void startToneSeq(const ToneStep* seq, uint8_t len, bool returnToTest = false);
void updateToneSeq(unsigned long now);
void playWinSound();
void playLoseSound();
void playJackpotSound();
void playJingleBells();
void startSnake();
void updateSnake();
void spawnFood();
void startTetris();
void updateTetris();
void spawnTetrisPiece();
void startFlappy();
void updateFlappy();
void startPong();
void updatePong();
void startDino();
void updateDino();
void spawnCactus();
void startDoodle();
void updateDoodle();
void syncNtpTime(bool force = false);
void fetchWeatherIfNeeded(bool force = false);
String wmoCodeToCondition(int code);
void runUrlOtaUpdate();
void scanWifiNetworksSafe(bool allowRetry = true);
void pollWifiScanAsync(unsigned long now);
void maintainWeakWifiKeepalive(unsigned long now);
void evaluateCalcExpression(const char* expr);
void appendCalcChar(char ch);
void backspaceCalcExpr();
void getRotatedShape(int shape, int rot, byte outMat[4][4]);
bool checkTetrisCollision(int px, int py, int rot);
void mergeTetrisPiece();
void clearTetrisLines();
void adjustTimerField(int delta);
unsigned long getCurrentStopwatchMs();
unsigned long getCurrentTimerRemainingMs();
void ensureWebProbeStarted();
void activateWebServerOnDemand();
void serviceWebServer(unsigned long now);
void setupWebServerRoutes();
int getMaxMenuItem(GameState st);
String escapeJsonStr(const String& s);
void initStorage();
void loadNotesFromFs();
void saveNotesToFs();
void addOrUpdateNote(const String& topic, const String& body);
void deleteNoteByIndex(int idx);
void buildNoteLines(const String& text, std::vector<String>& linesOut, size_t maxCharsPerLine = 20);
void loadSongsFromFs();
bool saveSongsToFs(String* errOut = nullptr);
int addOrUpdateSong(const String& title, const std::vector<SongStep>& steps, String* errOut = nullptr);
void deleteSongByIndex(int idx);
int importSongsFromJson(const String& rawJson, String* errOut = nullptr);
bool isAnyActiveGameState(GameState st);
void exitCurrentGameToSelect();

bool isAnyActiveGameState(GameState st) {
  return (st == ST_SLOT_GAME || st == ST_SNAKE_GAME || st == ST_TETRIS_GAME ||
          st == ST_BOTTLE_GAME || st == ST_FLAPPY_GAME || st == ST_PONG_GAME ||
          st == ST_DINO_GAME || st == ST_DOODLE_GAME || st == ST_CLICKER_GAME);
}

void exitCurrentGameToSelect() {
  int targetCursor = 0;
  if (gameState == ST_SLOT_GAME || gameState == ST_NO_MONEY) { autoSpin = false; targetCursor = 0; }
  else if (gameState == ST_SNAKE_GAME || gameState == ST_SNAKE_OVER) targetCursor = 1;
  else if (gameState == ST_TETRIS_GAME || gameState == ST_TETRIS_OVER) targetCursor = 2;
  else if (gameState == ST_BOTTLE_GAME) { bottleState = 0; bottleLanded = false; targetCursor = 3; }
  else if (gameState == ST_FLAPPY_GAME || gameState == ST_FLAPPY_OVER) targetCursor = 4;
  else if (gameState == ST_PONG_GAME || gameState == ST_PONG_OVER) targetCursor = 5;
  else if (gameState == ST_DINO_GAME || gameState == ST_DINO_OVER) targetCursor = 6;
  else if (gameState == ST_DOODLE_GAME || gameState == ST_DOODLE_OVER) targetCursor = 7;
  else if (gameState == ST_CLICKER_GAME) { saveGameData(); targetCursor = 8; }

  gameState = ST_GAME_SELECT;
  menuCursor = targetCursor;
  beep(500, 45);
  markDirty();
}

void performClickerTap(bool withSound) {
  clickerCombo = 1;
  clickerPower = 1;
  clickerAuto = 0;
  lastClickMs = millis();

  clickerCount += 1;
  if (clickerCount > 999999999L) clickerCount = 999999999L;
  clickBtnAnim = 3;

  for (int i = 0; i < 6; i++) {
    if (clickParticles[i].life == 0) {
      clickParticles[i].x = (float)random(44, 80);
      clickParticles[i].y = 33.0f;
      clickParticles[i].vy = -1.1f;
      clickParticles[i].val = 1;
      clickParticles[i].life = 12;
      break;
    }
  }

  if (clickerCount >= 500) unlockAchievement(6);
  if (clickerCount % 25 == 0) saveGameData();
  if (withSound) beep(850, 20);
  markDirty();
}

void resetClickerCounter(bool withSound) {
  clickerCount = 0;
  clickBtnAnim = 0;
  for (int i = 0; i < 6; i++) clickParticles[i].life = 0;
  saveGameData();
  if (withSound) beep(450, 50);
  markDirty();
}

void updateClicker(unsigned long now) {
  (void)now;
  clickerCombo = 1;
  clickerPower = 1;
  clickerAuto = 0;
  if (clickBtnAnim > 0) clickBtnAnim--;
  for (int i = 0; i < 6; i++) {
    if (clickParticles[i].life > 0) {
      clickParticles[i].y += clickParticles[i].vy;
      clickParticles[i].life--;
    }
  }
}

void applyGodMode(bool enable) {
  godMode = enable;
  if (enable) {
    disabledMode = false;
    coins = GOD_COINS_VAL;
    displayCoins = GOD_COINS_VAL;
    for (uint8_t i = 0; i < ACH_COUNT; i++) achievements[i].unlocked = true;
    if (gameState == ST_SNAKE_OVER) gameState = ST_SNAKE_GAME;
    if (gameState == ST_TETRIS_OVER) gameState = ST_TETRIS_GAME;
    if (gameState == ST_FLAPPY_OVER) startFlappy();
    if (gameState == ST_PONG_OVER) { ballY = 54.0f; ballVY = -fabsf(ballVY); gameState = ST_PONG_GAME; }
    if (gameState == ST_DINO_OVER) { dinoY = DINO_GROUND_Y - 12; dinoVY = 0.0f; dinoJumping = false; gameState = ST_DINO_GAME; }
    if (gameState == ST_DOODLE_OVER) { doodleY = 38.0f; doodleVY = DOODLE_JUMP_V; gameState = ST_DOODLE_GAME; }
    lastWebStatus = "Режим бога включён";
    saveGameData();
    beep(1400, 110);
  } else {
    if (coins >= GOD_COINS_VAL) coins = 150;
    displayCoins = coins;
    lastWebStatus = "Режим бога выключен";
    saveGameData();
    beep(600, 80);
  }
  markDirty();
}

void applyDisabledMode(bool enable) {
  disabledMode = enable;
  if (enable) {
    godMode = false;
    autoSpin = false;
    stopSongPlayback();
    stopMetronome(false);
    noTone(BUZZER_PIN);
    lastWebStatus = "Приставка отключена";
    beep(320, 140);
  } else {
    lastWebStatus = "Приставка включена";
    beep(950, 90);
  }
  markDirty();
}

static bool isUtf8Continuation(uint8_t b) { return (b & 0xC0) == 0x80; }

static String utf8Truncate(const String& s, size_t maxChars) {
  String out;
  size_t chars = 0, i = 0, n = s.length();
  while (i < n && chars < maxChars) {
    uint8_t b = (uint8_t)s[i];
    size_t seqLen = 1;
    if ((b & 0xE0) == 0xC0 && i + 1 < n && isUtf8Continuation((uint8_t)s[i + 1])) seqLen = 2;
    else if ((b & 0xF0) == 0xE0 && i + 2 < n && isUtf8Continuation((uint8_t)s[i + 1]) && isUtf8Continuation((uint8_t)s[i + 2])) seqLen = 3;
    else if ((b & 0xF8) == 0xF0 && i + 3 < n && isUtf8Continuation((uint8_t)s[i + 1]) && isUtf8Continuation((uint8_t)s[i + 2]) && isUtf8Continuation((uint8_t)s[i + 3])) seqLen = 4;
    for (size_t k = 0; k < seqLen; k++) out += s[i + k];
    i += seqLen;
    chars++;
  }
  return out;
}

static String sanitizeNoteText(const String& raw) {
  String s = raw;
  s.replace("\r\n", "\n"); s.replace('\r', '\n');
  s.replace("\xE2\x80\x94", "-"); s.replace("\xE2\x80\x93", "-");
  s.replace("\xE2\x80\xA6", "..."); s.replace("\xC2\xA0", " ");
  return s;
}

static String jsonUnescape(const String& raw) {
  String out; out.reserve(raw.length());
  for (size_t i = 0; i < raw.length(); i++) {
    char c = raw[i];
    if (c == '\\' && i + 1 < raw.length()) {
      char n = raw[++i];
      if (n == 'n') out += '\n'; else if (n == 'r') out += '\r'; else if (n == 't') out += ' '; else out += n;
    } else out += c;
  }
  return out;
}

static bool extractJsonStringField(const String& obj, const char* key, String& valOut) {
  String pat = String("\"") + key + "\"";
  int kPos = obj.indexOf(pat); if (kPos < 0) return false;
  int colonPos = obj.indexOf(':', kPos + pat.length()); if (colonPos < 0) return false;
  int qStart = obj.indexOf('"', colonPos + 1); if (qStart < 0) return false;
  String raw; bool escaped = false;
  for (int i = qStart + 1; i < (int)obj.length(); i++) {
    char c = obj[i];
    if (escaped) { raw += c; escaped = false; }
    else if (c == '\\') escaped = true;
    else if (c == '"') { valOut = jsonUnescape(raw); return true; }
    else raw += c;
  }
  return false;
}

static int findMatchingBracket(const String& s, int openPos, char openCh, char closeCh) {
  if (openPos < 0 || openPos >= (int)s.length() || s[openPos] != openCh) return -1;
  int depth = 0; bool inStr = false, esc = false;
  for (int i = openPos; i < (int)s.length(); i++) {
    char c = s[i];
    if (esc) { esc = false; continue; }
    if (c == '\\') { esc = true; continue; }
    if (c == '"') { inStr = !inStr; continue; }
    if (!inStr) {
      if (c == openCh) depth++;
      else if (c == closeCh) { depth--; if (depth == 0) return i; }
    }
  }
  return -1;
}

static int extractIntAfterKey(const String& obj, const char* key, bool& okOut) {
  okOut = false;
  String pat = String("\"") + key + "\"";
  int kPos = obj.indexOf(pat); if (kPos < 0) return 0;
  int colonPos = obj.indexOf(':', kPos + pat.length()); if (colonPos < 0) return 0;
  int i = colonPos + 1;
  while (i < (int)obj.length() && (obj[i] == ' ' || obj[i] == '\t' || obj[i] == '\n' || obj[i] == '\r')) i++;
  String num;
  if (i < (int)obj.length() && obj[i] == '-') { num += '-'; i++; }
  while (i < (int)obj.length() && obj[i] >= '0' && obj[i] <= '9') num += obj[i++];
  if (num.length() == 0 || num == "-") return 0;
  okOut = true;
  return num.toInt();
}

void saveNotesToFs() {
  if (!fsReady) return;
  File f = LittleFS.open(NOTES_FILE, "w"); if (!f) return;
  f.print("[");
  for (size_t i = 0; i < notesList.size(); i++) {
    if (i > 0) f.print(",");
    f.print("{\"t\":\"" + escapeJsonStr(notesList[i].topic) + "\",\"b\":\"" + escapeJsonStr(notesList[i].body) + "\"}");
  }
  f.print("]"); f.flush(); f.close();
}

void loadNotesFromFs() {
  notesList.clear();
  if (!fsReady || !LittleFS.exists(NOTES_FILE)) return;
  File f = LittleFS.open(NOTES_FILE, "r"); if (!f) return;
  String content = f.readString(); f.close();
  int pos = 0;
  while (pos < (int)content.length() && notesList.size() < MAX_NOTES) {
    int objStart = content.indexOf('{', pos); if (objStart < 0) break;
    int objEnd = findMatchingBracket(content, objStart, '{', '}'); if (objEnd < 0) break;
    String obj = content.substring(objStart, objEnd + 1);
    String tVal, bVal;
    if (extractJsonStringField(obj, "t", tVal) && extractJsonStringField(obj, "b", bVal)) {
      tVal = utf8Truncate(sanitizeNoteText(tVal), MAX_NOTE_TOPIC_W); tVal.trim();
      bVal = utf8Truncate(sanitizeNoteText(bVal), MAX_NOTE_BODY_W); bVal.trim();
      if (tVal.length() > 0) notesList.push_back({tVal, bVal});
    }
    pos = objEnd + 1;
  }
}

bool saveSongsToFs(String* errOut) {
  if (!fsReady) { if (errOut) *errOut = "LittleFS не смонтирован"; return false; }
  File f = LittleFS.open(SONGS_FILE, "w");
  if (!f) { if (errOut) *errOut = "Ошибка записи /songs.json"; return false; }
  f.print("[");
  for (size_t i = 0; i < songsList.size(); i++) {
    if (i > 0) f.print(",");
    f.print("{\"title\":\"" + escapeJsonStr(songsList[i].title) + "\",\"steps\":[");
    for (size_t j = 0; j < songsList[i].steps.size(); j++) {
      if (j > 0) f.print(",");
      f.print("[" + String(songsList[i].steps[j].freq) + "," + String(songsList[i].steps[j].dur) + "]");
    }
    f.print("]}");
  }
  f.print("]"); f.flush(); f.close();
  return true;
}

static bool parseSongObject(const String& obj, SongItem& outSong) {
  String titleVal;
  if (!extractJsonStringField(obj, "title", titleVal)) return false;
  titleVal = utf8Truncate(sanitizeNoteText(titleVal), MAX_SONG_TITLE_W); titleVal.trim();
  if (titleVal.length() == 0) return false;
  int keyPos = obj.indexOf("\"steps\""); if (keyPos < 0) return false;
  int arrStart = obj.indexOf('[', keyPos); if (arrStart < 0) return false;
  int arrEnd = findMatchingBracket(obj, arrStart, '[', ']'); if (arrEnd <= arrStart) return false;
  String arrStr = obj.substring(arrStart + 1, arrEnd);
  std::vector<SongStep> parsedSteps;
  int p = 0;
  while (p < (int)arrStr.length() && parsedSteps.size() < MAX_SONG_STEPS) {
    int pairStart = arrStr.indexOf('[', p);
    if (pairStart >= 0) {
      int pairEnd = findMatchingBracket(arrStr, pairStart, '[', ']'); if (pairEnd < 0) break;
      String pairStr = arrStr.substring(pairStart + 1, pairEnd);
      int comma = pairStr.indexOf(',');
      if (comma > 0) {
        int fr = pairStr.substring(0, comma).toInt(), du = pairStr.substring(comma + 1).toInt();
        if (fr >= 0 && fr <= 4200 && du >= 30 && du <= 5000) parsedSteps.push_back({fr, du});
      }
      p = pairEnd + 1; continue;
    }
    int elemStart = arrStr.indexOf('{', p);
    if (elemStart >= 0) {
      int elemEnd = findMatchingBracket(arrStr, elemStart, '{', '}'); if (elemEnd < 0) break;
      String elemStr = arrStr.substring(elemStart, elemEnd + 1);
      bool okF = false, okD = false;
      int fr = extractIntAfterKey(elemStr, "freq", okF), du = extractIntAfterKey(elemStr, "dur", okD);
      if (okF && okD && fr >= 0 && fr <= 4200 && du >= 30 && du <= 5000) parsedSteps.push_back({fr, du});
      p = elemEnd + 1; continue;
    }
    break;
  }
  if (parsedSteps.empty()) return false;
  outSong.title = titleVal; outSong.steps = parsedSteps; return true;
}

void loadSongsFromFs() {
  songsList.clear();
  if (!fsReady || !LittleFS.exists(SONGS_FILE)) return;
  File f = LittleFS.open(SONGS_FILE, "r"); if (!f) return;
  String content = f.readString(); f.close();
  int pos = 0;
  while (pos < (int)content.length() && songsList.size() < MAX_SONGS) {
    int objStart = content.indexOf('{', pos); if (objStart < 0) break;
    int objEnd = findMatchingBracket(content, objStart, '{', '}'); if (objEnd < 0) break;
    SongItem item;
    if (parseSongObject(content.substring(objStart, objEnd + 1), item)) songsList.push_back(item);
    pos = objEnd + 1;
  }
}

void initStorage() {
  fsReady = LittleFS.begin(false);
  if (!fsReady) { fsReady = LittleFS.begin(true); fsInitMsg = fsReady ? "LittleFS отформатирован" : "Ошибка LittleFS"; }
  else fsInitMsg = "LittleFS готов";
  if (fsReady) { loadNotesFromFs(); loadSongsFromFs(); }
}

void addOrUpdateNote(const String& topic, const String& body) {
  String cleanTopic = sanitizeNoteText(topic); cleanTopic.trim();
  if (cleanTopic.length() == 0) cleanTopic = "Без темы";
  cleanTopic = utf8Truncate(cleanTopic, MAX_NOTE_TOPIC_W);
  String cleanBody = sanitizeNoteText(body); cleanBody.trim();
  cleanBody = utf8Truncate(cleanBody, MAX_NOTE_BODY_W);
  for (size_t i = 0; i < notesList.size(); i++) {
    if (notesList[i].topic.equalsIgnoreCase(cleanTopic)) {
      notesList[i].topic = cleanTopic; notesList[i].body = cleanBody; noteCursor = (int)i;
      saveNotesToFs(); markDirty(); return;
    }
  }
  if (notesList.size() >= MAX_NOTES) notesList.erase(notesList.begin());
  notesList.push_back({cleanTopic, cleanBody});
  noteCursor = (int)notesList.size() - 1;
  saveNotesToFs(); markDirty();
}

void deleteNoteByIndex(int idx) {
  if (idx < 0 || idx >= (int)notesList.size()) return;
  if (activeNoteIdx == idx) { activeNoteIdx = -1; if (gameState == ST_NOTE_VIEW) gameState = ST_NOTES_LIST; }
  else if (activeNoteIdx > idx) activeNoteIdx--;
  notesList.erase(notesList.begin() + idx);
  if (noteCursor >= (int)notesList.size()) noteCursor = max(0, (int)notesList.size() - 1);
  saveNotesToFs(); markDirty();
}

int addOrUpdateSong(const String& title, const std::vector<SongStep>& steps, String* errOut) {
  String cleanTitle = sanitizeNoteText(title); cleanTitle.trim();
  if (cleanTitle.length() == 0) cleanTitle = "Без названия";
  cleanTitle = utf8Truncate(cleanTitle, MAX_SONG_TITLE_W);
  std::vector<SongStep> validSteps;
  for (size_t i = 0; i < steps.size() && validSteps.size() < MAX_SONG_STEPS; i++) {
    validSteps.push_back({constrain(steps[i].freq, 0, 4200), constrain(steps[i].dur, 30, 5000)});
  }
  if (validSteps.empty()) { if (errOut) *errOut = "Список шагов пуст"; return -1; }
  int targetIdx = -1;
  for (size_t i = 0; i < songsList.size(); i++) {
    if (songsList[i].title.equalsIgnoreCase(cleanTitle)) { songsList[i].title = cleanTitle; songsList[i].steps = validSteps; targetIdx = (int)i; break; }
  }
  if (targetIdx < 0) {
    if (songsList.size() >= MAX_SONGS) songsList.erase(songsList.begin());
    songsList.push_back({cleanTitle, validSteps}); targetIdx = (int)songsList.size() - 1;
  }
  String fsErr;
  if (!saveSongsToFs(&fsErr)) { if (errOut) *errOut = fsErr; return -1; }
  pianoSongCursor = targetIdx; markDirty(); return targetIdx;
}

void deleteSongByIndex(int idx) {
  if (idx < 0 || idx >= (int)songsList.size()) return;
  if (songPlaying && activeSongIdx == idx) stopSongPlayback();
  else if (songPlaying && activeSongIdx > idx) activeSongIdx--;
  songsList.erase(songsList.begin() + idx);
  if (pianoSongCursor >= (int)songsList.size()) pianoSongCursor = max(0, (int)songsList.size() - 1);
  saveSongsToFs(); markDirty();
}

int importSongsFromJson(const String& rawJson, String* errOut) {
  String s = sanitizeNoteText(rawJson); s.trim();
  if (s.length() == 0) { if (errOut) *errOut = "Файл бэкапа пуст"; return 0; }
  std::vector<SongItem> imported;
  int pos = 0;
  while (pos < (int)s.length() && imported.size() < MAX_SONGS) {
    int objStart = s.indexOf('{', pos); if (objStart < 0) break;
    int objEnd = findMatchingBracket(s, objStart, '{', '}'); if (objEnd < 0) break;
    SongItem item;
    if (parseSongObject(s.substring(objStart, objEnd + 1), item)) imported.push_back(item);
    pos = objEnd + 1;
  }
  if (imported.empty()) { if (errOut) *errOut = "Не найдено ни одной валидной песни"; return 0; }
  for (size_t k = 0; k < imported.size(); k++) {
    bool merged = false;
    for (size_t i = 0; i < songsList.size(); i++) {
      if (songsList[i].title.equalsIgnoreCase(imported[k].title)) { songsList[i] = imported[k]; merged = true; break; }
    }
    if (!merged) {
      if (songsList.size() >= MAX_SONGS) songsList.erase(songsList.begin());
      songsList.push_back(imported[k]);
    }
  }
  String fsErr;
  if (!saveSongsToFs(&fsErr)) { if (errOut) *errOut = fsErr; return 0; }
  markDirty(); return (int)imported.size();
}

void buildNoteLines(const String& text, std::vector<String>& linesOut, size_t maxCharsPerLine) {
  linesOut.clear();
  if (maxCharsPerLine < 6) maxCharsPerLine = 6;
  String normalized = sanitizeNoteText(text);
  int start = 0;
  while (start <= (int)normalized.length()) {
    int nl = normalized.indexOf('\n', start);
    String para = (nl >= 0) ? normalized.substring(start, nl) : normalized.substring(start);
    if (para.length() == 0) linesOut.push_back("");
    else {
      std::vector<String> glyphs;
      size_t i = 0;
      while (i < para.length()) {
        uint8_t b = (uint8_t)para[i];
        size_t seqLen = 1;
        if ((b & 0xE0) == 0xC0 && i + 1 < para.length() && isUtf8Continuation((uint8_t)para[i + 1])) seqLen = 2;
        else if ((b & 0xF0) == 0xE0 && i + 2 < para.length() && isUtf8Continuation((uint8_t)para[i + 1]) && isUtf8Continuation((uint8_t)para[i + 2])) seqLen = 3;
        else if ((b & 0xF8) == 0xF0 && i + 3 < para.length() && isUtf8Continuation((uint8_t)para[i + 1]) && isUtf8Continuation((uint8_t)para[i + 2]) && isUtf8Continuation((uint8_t)para[i + 3])) seqLen = 4;
        glyphs.push_back(para.substring(i, i + seqLen));
        i += seqLen;
      }
      size_t pos = 0;
      while (pos < glyphs.size()) {
        size_t take = min(maxCharsPerLine, glyphs.size() - pos), cut = take;
        if (pos + take < glyphs.size()) {
          for (size_t back = take; back > 4; back--) if (glyphs[pos + back - 1] == " ") { cut = back - 1; break; }
        }
        String line;
        for (size_t k = 0; k < cut; k++) line += glyphs[pos + k];
        line.trim(); linesOut.push_back(line); pos += cut;
        while (pos < glyphs.size() && glyphs[pos] == " ") pos++;
      }
    }
    if (nl < 0) break;
    start = nl + 1;
  }
  if (linesOut.empty()) linesOut.push_back("(пусто)");
}

int getMaxMenuItem(GameState st) {
  switch (st) {
    case ST_MAIN_MENU:         return 8;
    case ST_GAME_SELECT:       return 9;
    case ST_SETTINGS_MENU:     return 6;
    case ST_WIFI_MANAGEMENT:   return 4;
    case ST_ACHIEVEMENTS_MENU: return ACH_COUNT - 1;
    case ST_CLOCK_MENU:        return 3;
    case ST_MUSIC_MENU:        return 2;
    case ST_RECOVERY_MENU:     return 3;
    case ST_NOTES_LIST:        return (notesList.empty()) ? 0 : ((int)notesList.size() - 1);
    default:                   return 0;
  }
}

String escapeJsonStr(const String& s) {
  String out; out.reserve(s.length() + 12);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\\') out += "\\\\"; else if (c == '"') out += "\\\"";
    else if (c == '\n') out += "\\n"; else if (c == '\r') out += "\\r"; else out += c;
  }
  return out;
}

void wakeScreen() { screenSleeping = false; u8g2.setContrast(220); idleTimer = millis(); markDirty(); }

void startArchScreensaver() {
  screenSleeping = true; u8g2.setContrast(150);
  archX = (float)random(4, 128 - ARCH_LOGO_W - 4); archY = (float)random(4, 64 - ARCH_LOGO_H - 4);
  archVX = (random(0, 2) == 0) ? 0.85f : -0.85f; archVY = (random(0, 2) == 0) ? 0.65f : -0.65f;
  archTimer = millis(); markDirty();
}

void updateArchScreensaver() {
  if (!screenSleeping) return;
  unsigned long now = millis();
  if ((unsigned long)(now - archTimer) < 28) return;
  archTimer = now; archX += archVX; archY += archVY;
  if (archX <= 2.0f) { archX = 2.0f; archVX = -archVX; }
  else if (archX >= (float)(126 - ARCH_LOGO_W)) { archX = (float)(126 - ARCH_LOGO_W); archVX = -archVX; }
  if (archY <= 2.0f) { archY = 2.0f; archVY = -archVY; }
  else if (archY >= (float)(62 - ARCH_LOGO_H)) { archY = (float)(62 - ARCH_LOGO_H); archVY = -archVY; }
}

void drawArchLogo(int x, int y) {
  const int cx = x + 22, topY = y + 2, botY = y + 26;
  int prevLx = cx, prevLy = topY;
  for (int i = 1; i <= 15; i++) {
    float t = (float)i / 15.0f;
    int lx = cx - (int)roundf(powf(t, 1.5f) * 16.0f), ly = topY + (int)roundf(t * (botY - topY));
    u8g2.drawLine(prevLx, prevLy, lx, ly);
    if (i >= 4) u8g2.drawLine(prevLx + 1, prevLy, lx + 1, ly);
    prevLx = lx; prevLy = ly;
  }
  int prevRx = cx + 1, prevRy = topY + 6;
  for (int i = 1; i <= 11; i++) {
    float t = (float)i / 11.0f;
    int rx = (cx + 1) + (int)roundf(powf(t, 1.4f) * 15.0f), ry = (topY + 6) + (int)roundf(t * (botY - (topY + 6)));
    u8g2.drawLine(prevRx, prevRy, rx, ry);
    if (i >= 3) u8g2.drawLine(prevRx + 1, prevRy, rx + 1, ry);
    prevRx = rx; prevRy = ry;
  }
  u8g2.drawLine(cx - 2, y + 19, cx + 13, y + 24);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(x + 1, y + 36, "ESP32 CONSOLE");
}

struct CalcParser {
  const char* s; size_t pos; bool err; const char* errText;
  void skipSpaces() { while (s[pos] == ' ' || s[pos] == '\t') pos++; }
  double parseExpr() {
    double val = parseTerm();
    while (!err) {
      skipSpaces(); char op = s[pos];
      if (op == '+' || op == '-') { pos++; double rhs = parseTerm(); if (err) return 0.0; val = (op == '+') ? val + rhs : val - rhs; }
      else break;
    }
    return val;
  }
  double parseTerm() {
    double val = parseFactor();
    while (!err) {
      skipSpaces(); char op = s[pos];
      if (op == '*' || op == '/') {
        pos++; double rhs = parseFactor(); if (err) return 0.0;
        if (op == '*') val *= rhs;
        else { if (fabs(rhs) < 1e-12) { err = true; errText = "Деление на 0"; return 0.0; } val /= rhs; }
      } else break;
    }
    return val;
  }
  double parseFactor() {
    skipSpaces(); char c = s[pos];
    if (c == '+') { pos++; return parseFactor(); }
    if (c == '-') { pos++; return -parseFactor(); }
    if (c == '(') { pos++; double v = parseExpr(); skipSpaces(); if (s[pos] != ')') { err = true; errText = "Нет скобки"; return 0.0; } pos++; return v; }
    if ((c >= '0' && c <= '9') || c == '.') {
      char numBuf[32]; size_t nIdx = 0; bool hasDot = false, hasDigit = false;
      while ((s[pos] >= '0' && s[pos] <= '9') || s[pos] == '.') {
        if (s[pos] == '.') { if (hasDot) { err = true; errText = "Лишняя точка"; return 0.0; } hasDot = true; } else hasDigit = true;
        if (nIdx >= sizeof(numBuf) - 1) { err = true; errText = "Длинное число"; return 0.0; }
        numBuf[nIdx++] = s[pos++];
      }
      if (!hasDigit) { err = true; errText = "Не число"; return 0.0; }
      numBuf[nIdx] = '\0'; return ::strtod(numBuf, nullptr);
    }
    if (c == '\0') { err = true; errText = "Незакончено"; return 0.0; }
    err = true; errText = "Синтаксис"; return 0.0;
  }
};

static bool isCalcOp(char c) { return (c == '+' || c == '-' || c == '*' || c == '/'); }

void evaluateCalcExpression(const char* expr) {
  calcError = false; calcHasResult = false; calcErrMsg = ""; calcResult = 0.0;
  if (!expr || expr[0] == '\0') return;
  CalcParser p{expr, 0, false, ""};
  double res = p.parseExpr(); p.skipSpaces();
  if (!p.err && p.s[p.pos] != '\0') { p.err = true; p.errText = "Лишний знак"; }
  if (p.err || isnan(res) || isinf(res)) { calcError = true; calcErrMsg = (p.err && p.errText) ? String(p.errText) : String("Ошибка"); }
  else { calcResult = res; calcHasResult = true; }
}

void appendCalcChar(char ch) {
  size_t len = strlen(calcExpr); if (len >= MAX_CALC_EXPR) return;
  if (isCalcOp(ch) && len > 0 && isCalcOp(calcExpr[len - 1])) {
    if (ch == '-' && calcExpr[len - 1] != '-') { calcExpr[len] = ch; calcExpr[len + 1] = '\0'; }
    else calcExpr[len - 1] = ch;
    evaluateCalcExpression(calcExpr); return;
  }
  calcExpr[len] = ch; calcExpr[len + 1] = '\0'; evaluateCalcExpression(calcExpr);
}

void backspaceCalcExpr() { size_t len = strlen(calcExpr); if (len > 0) { calcExpr[len - 1] = '\0'; evaluateCalcExpression(calcExpr); } }

int midiToFreq(int midiNote) {
  midiNote = constrain(midiNote, 36, 96);
  return constrain((int)roundf(440.0f * powf(2.0f, ((float)midiNote - 69.0f) / 12.0f)), 65, 2400);
}

String midiToLabel(int midiNote) {
  const char* names[12] = {"До", "До#", "Ре", "Ре#", "Ми", "Фа", "Фа#", "Соль", "Соль#", "Ля", "Ля#", "Си"};
  return String(names[((midiNote % 12) + 12) % 12]) + String((midiNote / 12) - 1);
}

String freqToLabel(int freq) {
  if (freq <= 0) return "Пауза";
  return midiToLabel((int)roundf(69.0f + 12.0f * log2f((float)freq / 440.0f)));
}

void playMidiNote(int midiNote, int durMs) {
  stopSongPlayback(); metroRunning = false;
  midiNote = constrain(midiNote, 36, 96); durMs = constrain(durMs, 40, 2000);
  int freq = midiToFreq(midiNote);
  lastPlayedFreq = freq; lastPlayedNoteName = midiToLabel(midiNote);
  if (soundEnabled) tone(BUZZER_PIN, freq, durMs);
  markDirty();
}

void triggerPauseStep(int durMs) {
  stopSongPlayback(); metroRunning = false; lastPlayedFreq = 0; lastPlayedNoteName = "Пауза";
  noTone(BUZZER_PIN); (void)durMs; markDirty();
}

void stopSongPlayback() {
  if (songPlaying) { songPlaying = false; activeSongStepLabel = ""; noTone(BUZZER_PIN); markDirty(); }
  else noTone(BUZZER_PIN);
}

static void applySongStepAt(size_t idx, unsigned long baseMs) {
  const SongStep& st = songsList[activeSongIdx].steps[idx];
  unsigned long safeDur = (unsigned long)max(60, st.dur);
  noTone(BUZZER_PIN);
  if (st.freq > 0) { activeSongStepLabel = String(st.freq) + " Гц"; lastPlayedFreq = st.freq; if (soundEnabled) tone(BUZZER_PIN, st.freq, (unsigned int)safeDur); }
  else { activeSongStepLabel = "Пауза"; lastPlayedFreq = 0; }
  nextSongStepMs = baseMs + safeDur + 35UL; markDirty();
}

void startSongPlayback(int songIdx) {
  if (songIdx < 0 || songIdx >= (int)songsList.size() || songsList[songIdx].steps.empty()) return;
  metroRunning = false; noTone(BUZZER_PIN); activeSeq = nullptr; activeSeqLen = 0; activeSeqIdx = 0;
  pianoSongCursor = songIdx; activeSongIdx = songIdx; activeSongStepIdx = 0; songPlaying = true;
  applySongStepAt(0, millis());
}

void updateSongPlayback(unsigned long now) {
  if (!songPlaying) return;
  if (activeSongIdx < 0 || activeSongIdx >= (int)songsList.size()) { stopSongPlayback(); return; }
  if ((long)(now - nextSongStepMs) < 0) return;
  activeSongStepIdx++;
  if (activeSongStepIdx >= songsList[activeSongIdx].steps.size()) { stopSongPlayback(); return; }
  applySongStepAt(activeSongStepIdx, now);
}

void stopMetronome(bool goToMenu) {
  metroRunning = false; metroBeat = 0; noTone(BUZZER_PIN);
  if (goToMenu) { gameState = ST_MUSIC_MENU; menuCursor = 1; }
  markDirty();
}

void setMetronomeBpm(int nextBpm) {
  metroBpm = constrain(nextBpm, MIN_BPM, MAX_BPM);
  nextMetroMs = millis() + max(180UL, 60000UL / (unsigned long)metroBpm);
  markDirty();
}

void updateMetronome(unsigned long now) {
  if (!metroRunning || gameState != ST_METRONOME) return;
  if ((long)(now - nextMetroMs) < 0) return;
  nextMetroMs = now + max(180UL, 60000UL / (unsigned long)constrain(metroBpm, MIN_BPM, MAX_BPM));
  metroBeat = (metroBeat + 1) % 4; noTone(BUZZER_PIN);
  if (soundEnabled) tone(BUZZER_PIN, (metroBeat == 0) ? 1480 : 980, 45);
  markDirty();
}

void scanWifiNetworksSafe(bool allowRetry) {
  wifiScanActive = true; wifiScanStartMs = millis();
  if (allowRetry) wifiScanRetries = 0;
  gameState = ST_WIFI_SCAN; markDirty();
  WiFi.scanDelete();
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  else if (!(WiFi.getMode() & WIFI_STA)) WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false); WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.scanNetworks(true, true);
}

void pollWifiScanAsync(unsigned long now) {
  if (!wifiScanActive) return;
  int res = WiFi.scanComplete();
  if (res == WIFI_SCAN_RUNNING) {
    if ((unsigned long)(now - wifiScanStartMs) > 10000UL) {
      WiFi.scanDelete();
      if (wifiScanRetries < 2) { wifiScanRetries++; wifiScanStartMs = now; WiFi.scanNetworks(true, true); return; }
      wifiScanActive = false; wifiNetCount = 0; menuCursor = 0;
      if (gameState == ST_WIFI_SCAN) gameState = ST_WIFI_LIST;
      markDirty();
    }
    return;
  }
  if (res <= 0 && wifiScanRetries < 2) { WiFi.scanDelete(); wifiScanRetries++; wifiScanStartMs = now; WiFi.scanNetworks(true, true); return; }
  wifiScanActive = false; wifiNetCount = max(0, res); menuCursor = 0;
  if (gameState == ST_WIFI_SCAN) gameState = ST_WIFI_LIST;
  markDirty();
}

void maintainWeakWifiKeepalive(unsigned long now) {
  if (strlen(currentSsid) == 0 || wifiScanActive) return;
  if ((unsigned long)(now - lastBackgroundWifiCheckMs) < 7000UL) return;
  lastBackgroundWifiCheckMs = now;
  if (WiFi.status() != WL_CONNECTED && gameState != ST_WIFI_CONNECTING && gameState != ST_WIFI_ERROR) {
    WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.begin(currentSsid, currentPass);
  }
}

void syncNtpTime(bool force) {
  if (WiFi.status() == WL_CONNECTED && (!ntpConfigured || force)) {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
    ntpConfigured = true;
    lastClockPollMs = 0;
  }
}

void updateClockCache(unsigned long now) {
  if (gameState != ST_NET_CLOCK) return;
  if ((unsigned long)(now - lastClockPollMs) < 250UL) return;
  lastClockPollMs = now;
  struct tm timeinfo;
  if (WiFi.status() == WL_CONNECTED && getLocalTime(&timeinfo, 5)) {
    snprintf(cachedTimeBuf, sizeof(cachedTimeBuf), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    snprintf(cachedDateBuf, sizeof(cachedDateBuf), "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
    cachedTimeValid = true;
  } else {
    cachedTimeValid = false;
  }
}

String wmoCodeToCondition(int code) {
  if (code == 0) return "Ясно";
  if (code == 1 || code == 2) return "Малооблачно";
  if (code == 3) return "Облачно";
  if (code == 45 || code == 48) return "Туман";
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return "Дождь";
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "Снег";
  if (code >= 95) return "Гроза";
  return "Переменно";
}

void fetchWeatherIfNeeded(bool force) {
  if (WiFi.status() != WL_CONNECTED) return;
  unsigned long now = millis();
  if (!force && lastWeatherMs > 0 && ((unsigned long)(now - lastWeatherMs) < WEATHER_REFRESH_MS)) return;
  lastWeatherMs = now;
  WiFiClientSecure client; client.setInsecure(); HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(WEATHER_LAT, 2) +
               "&longitude=" + String(WEATHER_LON, 2) + "&current=temperature_2m,weather_code";
  if (!http.begin(client, url)) return;
  http.setTimeout(1800);
  if (http.GET() == HTTP_CODE_OK) {
    String payload = http.getString();
    int curPos = payload.indexOf("\"current\"");
    String slice = (curPos >= 0) ? payload.substring(curPos) : payload;
    int tPos = slice.indexOf("\"temperature_2m\""), cPos = slice.indexOf("\"weather_code\"");
    if (tPos >= 0 && cPos >= 0) {
      int tColon = slice.indexOf(':', tPos), cColon = slice.indexOf(':', cPos);
      if (tColon >= 0 && cColon >= 0) {
        String tStr = slice.substring(tColon + 1, tColon + 10), cStr = slice.substring(cColon + 1, cColon + 8);
        tStr.trim(); cStr.trim();
        weatherTempC = tStr.toFloat(); weatherCondition = wmoCodeToCondition(cStr.toInt()); weatherValid = true;
      }
    }
  }
  http.end();
}

void runUrlOtaUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    lastWebStatus = "Нет Wi-Fi для OTA"; markDirty();
    if (gameState != ST_RECOVERY_MENU) gameState = ST_SETTINGS_MENU;
    return;
  }
  stopSongPlayback(); stopMetronome(false);
  GameState prevState = gameState;
  gameState = ST_OTA_UPDATING; otaProgressPct = 5; lastOtaUiMs = 0; lastWebStatus = "Скачивание обновления...";
  markDirty(); renderDisplay();

  WiFiClientSecure client; client.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.onProgress([](int cur, int total) {
    if (total > 0) {
      int nextPct = constrain((int)(((long long)cur * 100LL) / (long long)total), 0, 100);
      unsigned long m = millis();
      if (nextPct != otaProgressPct && ((unsigned long)(m - lastOtaUiMs) >= 150 || nextPct == 100)) {
        otaProgressPct = nextPct; lastOtaUiMs = m; renderDisplay();
      }
    }
  });

  t_httpUpdate_return ret = httpUpdate.update(client, OTA_URL);
  if (ret == HTTP_UPDATE_OK) {
    otaProgressPct = 100; lastWebStatus = "OTA готово! Ребут..."; gameState = ST_OTA_DONE; restartAtMs = millis() + 450; markDirty(); renderDisplay();
  } else {
    lastWebStatus = (ret == HTTP_UPDATE_NO_UPDATES) ? "Обновлений нет" : ("OTA ошибка: " + httpUpdate.getLastErrorString());
    gameState = (prevState == ST_RECOVERY_MENU) ? ST_RECOVERY_MENU : ST_SETTINGS_MENU; markDirty();
  }
}

const char* stateToName(GameState st) {
  if (disabledMode) return "ОТКЛЮЧЕНО";
  if (godMode) return "РЕЖИМ БОГА";
  switch (st) {
    case ST_MAIN_MENU: return "Главное меню";
    case ST_GAME_SELECT: return "Выбор игры";
    case ST_SLOT_GAME: case ST_SPINNING: case ST_WIN_ANIM: case ST_LOSE_ANIM: case ST_NO_MONEY: return "Слоты";
    case ST_SNAKE_GAME: case ST_SNAKE_OVER: return "Змейка";
    case ST_TETRIS_GAME: case ST_TETRIS_OVER: return "Тетрис";
    case ST_BOTTLE_GAME: return "Баттл Флип";
    case ST_FLAPPY_GAME: case ST_FLAPPY_OVER: return "Flappy Bird";
    case ST_PONG_GAME: case ST_PONG_OVER: return "Понг";
    case ST_DINO_GAME: case ST_DINO_OVER: return "Динозаврик";
    case ST_DOODLE_GAME: case ST_DOODLE_OVER: return "Doodle Jump";
    case ST_CLICKER_GAME: return "Счётчик";
    case ST_WEB_MESSAGE: return "Сообщение";
    case ST_RECORDS_SCREEN: return "Рекорды";
    case ST_CALCULATOR: return "Калькулятор";
    case ST_MUSIC_MENU: case ST_PIANO_MODE: return "Музыка / Песни";
    case ST_METRONOME: return "Музыка / Метроном";
    case ST_NOTES_LIST: case ST_NOTE_VIEW: return "Шпаргалки";
    case ST_RECOVERY_MENU: case ST_OTA_WAIT: case ST_OTA_UPDATING: case ST_OTA_DONE: return "Recovery / OTA";
    case ST_CLOCK_MENU: case ST_NET_CLOCK: case ST_STOPWATCH: case ST_TIMER_SET: case ST_TIMER_RUNNING: case ST_TIMER_DONE: return "Часы";
    default: return "Меню / Система";
  }
}

const char* symbolToChar(int s) {
  switch (s) {
    case 0: return "🍏"; case 1: return "🍆"; case 2: return "🌙";
    case 3: return "🍇"; case 4: return "✖️"; default: return "🚀";
  }
}

String buildNotesJsonArray() {
  String out = "[";
  for (size_t i = 0; i < notesList.size(); i++) {
    if (i > 0) out += ",";
    out += "{\"id\":" + String(i) + ",\"topic\":\"" + escapeJsonStr(notesList[i].topic) + "\",\"body\":\"" + escapeJsonStr(notesList[i].body) + "\"}";
  }
  return out + "]";
}

String buildSongsJsonArray() {
  String out = "[";
  for (size_t i = 0; i < songsList.size(); i++) {
    if (i > 0) out += ",";
    out += "{\"id\":" + String(i) + ",\"title\":\"" + escapeJsonStr(songsList[i].title) + "\",\"count\":" + String(songsList[i].steps.size()) + ",\"steps\":[";
    for (size_t j = 0; j < songsList[i].steps.size(); j++) {
      if (j > 0) out += ",";
      out += "{\"freq\":" + String(songsList[i].steps[j].freq) + ",\"dur\":" + String(songsList[i].steps[j].dur) + ",\"label\":\"" + escapeJsonStr(freqToLabel(songsList[i].steps[j].freq)) + "\"}";
    }
    out += "]}";
  }
  return out + "]";
}

String buildStatusJson() {
  unsigned long remSec = (getCurrentTimerRemainingMs() + 999UL) / 1000UL;
  String json = "{";
  json += "\"rev\":" + String(stateRev) + ",";
  json += "\"ver\":\"" + escapeJsonStr(String(FW_VERSION)) + "\",";
  json += "\"ip\":\"" + escapeJsonStr(ipAddressStr) + "\",";
  json += "\"state\":\"" + escapeJsonStr(String(stateToName(gameState))) + "\",";
  json += "\"god\":" + String(godMode ? "true" : "false") + ",";
  json += "\"spinning\":" + String(gameState == ST_SPINNING ? "true" : "false") + ",";
  json += "\"timerRunning\":" + String(timerRunning ? "true" : "false") + ",";
  json += "\"timerSec\":" + String(remSec) + ",";
  json += "\"coins\":" + String(coins) + ",";
  json += "\"totalSpins\":" + String(totalSpins) + ",";
  json += "\"clicks\":" + String(clickerCount) + ",";
  json += "\"clickPower\":1,";
  json += "\"clickAuto\":0,";
  json += "\"s1\":\"" + String(symbolToChar(slot1)) + "\",";
  json += "\"s2\":\"" + String(symbolToChar(slot2)) + "\",";
  json += "\"s3\":\"" + String(symbolToChar(slot3)) + "\",";
  json += "\"pianoNote\":\"" + escapeJsonStr(lastPlayedNoteName) + "\",";
  json += "\"pianoFreq\":" + String(lastPlayedFreq) + ",";
  json += "\"recSnake\":" + String(snakeRecord) + ",";
  json += "\"recTetris\":" + String(tetrisRecord) + ",";
  json += "\"recFlappy\":" + String(flappyRecord) + ",";
  json += "\"recPong\":" + String(pongRecord) + ",";
  json += "\"recDino\":" + String(dinoRecord) + ",";
  json += "\"recBottle\":" + String(bottleRecord) + ",";
  json += "\"recDoodle\":" + String(doodleRecord) + ",";
  json += "\"status\":\"" + escapeJsonStr(lastWebStatus) + "\"";
  return json + "}";
}

void handleRootWeb() { server.send_P(200, "text/html", WEB_PAGE_HTML); }
void handleStatusWeb() { server.send(200, "application/json", buildStatusJson()); }
void handleGodWeb() { wakeScreen(); applyGodMode(!godMode); server.send(200, "application/json", buildStatusJson()); }
void handleDisableWeb() { wakeScreen(); applyDisabledMode(!disabledMode); server.send(200, "application/json", buildStatusJson()); }

void handleClickerTapWeb() {
  if (disabledMode) { lastWebStatus = "Приставка отключена"; server.send(200, "application/json", buildStatusJson()); return; }
  wakeScreen();
  performClickerTap(true);
  lastWebStatus = "+1 (" + String(clickerCount) + ")";
  server.send(200, "application/json", buildStatusJson());
}

void handleClickerResetWeb() {
  wakeScreen();
  resetClickerCounter(true);
  lastWebStatus = "Счётчик сброшен (0)";
  server.send(200, "application/json", buildStatusJson());
}

void handleTimerSetWeb() {
  if (disabledMode) { lastWebStatus = "Приставка отключена"; server.send(200, "application/json", buildStatusJson()); return; }
  long sec = server.hasArg("sec") ? server.arg("sec").toInt() : 60;
  sec = constrain(sec, 1L, 86400L);
  unsigned long ms = (unsigned long)sec * 1000UL;
  timerRemainingMs = ms;
  timerTargetMs = millis() + ms;
  timerRunning = true;
  timerAlarmStep = 0;
  beep(1100, 60);
  lastWebStatus = "Таймер запущен: " + String(sec) + " сек";
  markDirty();
  server.send(200, "application/json", buildStatusJson());
}

void handleTimerStopWeb() {
  timerRunning = false;
  timerRemainingMs = 0;
  timerAlarmStep = 0;
  beep(450, 45);
  lastWebStatus = "Таймер остановлен";
  markDirty();
  server.send(200, "application/json", buildStatusJson());
}

void handlePingWeb() {
  wakeScreen();
  startToneSeq(PING_SEQ, sizeof(PING_SEQ) / sizeof(PING_SEQ[0]), false);
  webBannerText = "Я ТУТ! СИГНАЛ С ТЕЛЕФОНА";
  webBannerUntilMs = millis() + 3000UL;
  gameState = ST_WEB_MESSAGE;
  lastWebStatus = "Сигнал поиска отправлен на консоль!";
  markDirty();
  server.send(200, "application/json", buildStatusJson());
}

void handleBannerWeb() {
  if (server.hasArg("text")) {
    String t = sanitizeNoteText(server.arg("text")); t.trim();
    if (t.length() > 0) {
      wakeScreen();
      webBannerText = utf8Truncate(t, 44);
      webBannerUntilMs = millis() + 6000UL;
      gameState = ST_WEB_MESSAGE;
      beep(1150, 70);
      lastWebStatus = "Сообщение выведено на экран";
      markDirty();
    }
  }
  server.send(200, "application/json", buildStatusJson());
}

void handleRecordsSetWeb() {
  if (!godMode) { lastWebStatus = "Ошибка: доступен только в режиме бога"; markDirty(); server.send(200, "application/json", buildStatusJson()); return; }
  if (server.hasArg("snake"))  snakeRecord  = constrain(server.arg("snake").toInt(),  0, 99999);
  if (server.hasArg("tetris")) tetrisRecord = constrain(server.arg("tetris").toInt(), 0, 99999);
  if (server.hasArg("flappy")) flappyRecord = constrain(server.arg("flappy").toInt(), 0, 99999);
  if (server.hasArg("pong"))   pongRecord   = constrain(server.arg("pong").toInt(),   0, 99999);
  if (server.hasArg("dino"))   dinoRecord   = constrain(server.arg("dino").toInt(),   0, 99999);
  if (server.hasArg("bottle")) bottleRecord = constrain(server.arg("bottle").toInt(), 0, 99999);
  if (server.hasArg("doodle")) doodleRecord = constrain(server.arg("doodle").toInt(), 0, 99999);
  saveGameData(); beep(1250, 50); lastWebStatus = "Рекорды сохранены"; markDirty();
  server.send(200, "application/json", buildStatusJson());
}

void handlePianoWeb() {
  if (disabledMode) { lastWebStatus = "Приставка отключена"; server.send(200, "application/json", buildStatusJson()); return; }
  int dur = server.hasArg("dur") ? constrain(server.arg("dur").toInt(), 40, 2500) : 260;
  if (server.hasArg("pause") && server.arg("pause") == "1") { triggerPauseStep(dur); lastWebStatus = "Пианино: Пауза"; }
  else if (server.hasArg("midi")) { playMidiNote(constrain(server.arg("midi").toInt(), 36, 96), dur); lastWebStatus = "Пианино: " + lastPlayedNoteName; }
  else if (server.hasArg("freq")) {
    stopSongPlayback(); stopMetronome(false);
    int freq = constrain(server.arg("freq").toInt(), 65, 2400);
    lastPlayedFreq = freq; lastPlayedNoteName = freqToLabel(freq);
    if (soundEnabled) tone(BUZZER_PIN, freq, dur);
    lastWebStatus = "Пианино: " + lastPlayedNoteName;
  }
  markDirty();
  server.send(200, "application/json", buildStatusJson());
}

void handleSongsAddWeb() {
  String title = sanitizeNoteText(server.hasArg("title") ? server.arg("title") : ""); title.trim();
  String seq = server.hasArg("seq") ? server.arg("seq") : "";
  bool playAfter = server.hasArg("play") && server.arg("play") == "1";
  if (title.length() == 0) { lastWebStatus = "Ошибка: укажи название"; markDirty(); server.send(200, "application/json", buildStatusJson()); return; }
  std::vector<SongStep> steps;
  int pos = 0;
  while (pos <= (int)seq.length() && steps.size() < MAX_SONG_STEPS) {
    int comma = seq.indexOf(',', pos);
    String token = (comma >= 0) ? seq.substring(pos, comma) : seq.substring(pos);
    token.trim(); int colon = token.indexOf(':');
    if (colon > 0) {
      int fr = token.substring(0, colon).toInt(), du = token.substring(colon + 1).toInt();
      if (fr >= 0 && fr <= 4200 && du >= 30 && du <= 5000) steps.push_back({fr, du});
    }
    if (comma < 0) break;
    pos = comma + 1;
  }
  if (!steps.empty()) {
    String errMsg; int savedIdx = addOrUpdateSong(title, steps, &errMsg);
    if (savedIdx >= 0 && playAfter) { disabledMode = false; wakeScreen(); gameState = ST_PIANO_MODE; startSongPlayback(savedIdx); lastWebStatus = "Играет: " + title; }
    else if (savedIdx >= 0) { beep(1200, 50); lastWebStatus = "Сохранено: " + title; }
    else lastWebStatus = "Ошибка: " + errMsg;
  }
  markDirty();
  server.send(200, "application/json", buildStatusJson());
}

void handleSongsImportWeb() {
  String rawJson = server.hasArg("json") ? server.arg("json") : (server.hasArg("plain") ? server.arg("plain") : "");
  String errMsg; int count = importSongsFromJson(rawJson, &errMsg);
  lastWebStatus = (count > 0) ? ("Импортировано песен: " + String(count)) : ("Ошибка импорта: " + errMsg);
  if (count > 0) beep(1250, 60);
  markDirty();
  server.send(200, "application/json", buildStatusJson());
}

void handleSongsPlayWeb() {
  if (!disabledMode && server.hasArg("idx")) {
    int idx = server.arg("idx").toInt();
    if (idx >= 0 && idx < (int)songsList.size()) { wakeScreen(); gameState = ST_PIANO_MODE; startSongPlayback(idx); lastWebStatus = "Играет: " + songsList[idx].title; markDirty(); }
  }
  server.send(200, "application/json", buildStatusJson());
}

void handleSongsDeleteWeb() {
  if (server.hasArg("idx")) { deleteSongByIndex(server.arg("idx").toInt()); beep(650, 35); lastWebStatus = "Песня удалена"; markDirty(); }
  server.send(200, "application/json", buildStatusJson());
}

void handleModeWeb() {
  if (disabledMode) { lastWebStatus = "Приставка отключена"; server.send(200, "application/json", buildStatusJson()); return; }
  if (server.hasArg("m")) {
    String m = server.arg("m"); wakeScreen();
    if (m != "piano") stopSongPlayback();
    if (m != "metro") stopMetronome(false);
    if (m == "menu")         { gameState = ST_MAIN_MENU; menuCursor = 0; lastWebStatus = "Открыто Главное меню"; }
    else if (m == "slots")   { gameState = ST_SLOT_GAME; lastWebStatus = "Включены Слоты"; }
    else if (m == "snake")   { startSnake(); lastWebStatus = "Включена Змейка"; }
    else if (m == "tetris")  { startTetris(); lastWebStatus = "Включен Тетрис"; }
    else if (m == "bottle")  { gameState = ST_BOTTLE_GAME; bottleState = 0; bottleLanded = false; lastWebStatus = "Включен Баттл Флип"; }
    else if (m == "flappy")  { startFlappy(); lastWebStatus = "Включен Flappy Bird"; }
    else if (m == "pong")    { startPong(); lastWebStatus = "Включен Понг"; }
    else if (m == "dino")    { startDino(); lastWebStatus = "Включен Динозаврик"; }
    else if (m == "doodle")  { startDoodle(); lastWebStatus = "Включен Doodle Jump"; }
    else if (m == "clicker") { gameState = ST_CLICKER_GAME; lastWebStatus = "Открыт счётчик"; }
    else if (m == "records") { gameState = ST_RECORDS_SCREEN; lastWebStatus = "Открыты Рекорды"; }
    else if (m == "clock")   { syncNtpTime(false); fetchWeatherIfNeeded(false); gameState = ST_NET_CLOCK; lastWebStatus = "Открыты Часы"; }
    else if (m == "music")   { gameState = ST_MUSIC_MENU; menuCursor = 0; lastWebStatus = "Открыто меню Музыка"; }
    else if (m == "metro")   { metroRunning = true; metroBeat = 0; setMetronomeBpm(metroBpm); gameState = ST_METRONOME; lastWebStatus = "Включен Метроном"; }
    else if (m == "calc")    { evaluateCalcExpression(calcExpr); gameState = ST_CALCULATOR; lastWebStatus = "Открыт Калькулятор"; }
    else if (m == "piano")   { gameState = ST_PIANO_MODE; lastWebStatus = "Открыт плеер песен"; }
    else if (m == "notes")   { gameState = ST_NOTES_LIST; noteCursor = 0; lastWebStatus = "Открыты Шпаргалки"; }
    markDirty();
  }
  server.send(200, "application/json", buildStatusJson());
}

void handleNotesAddWeb() {
  String t = sanitizeNoteText(server.hasArg("topic") ? server.arg("topic") : ""); t.trim();
  String b = server.hasArg("body") ? server.arg("body") : "";
  if (t.length() > 0) { addOrUpdateNote(t, b); beep(1150, 45); lastWebStatus = "Загружена шпаргалка: " + t; }
  markDirty(); server.send(200, "application/json", buildStatusJson());
}

void handleNotesDeleteWeb() {
  if (server.hasArg("idx")) { deleteNoteByIndex(server.arg("idx").toInt()); beep(650, 35); lastWebStatus = "Шпаргалка удалена"; markDirty(); }
  server.send(200, "application/json", buildStatusJson());
}

void handleCoinsWeb() {
  if (godMode) { coins = GOD_COINS_VAL; displayCoins = GOD_COINS_VAL; lastWebStatus = "Режим бога включён"; markDirty(); server.send(200, "application/json", buildStatusJson()); return; }
  if (server.hasArg("amount")) {
    long addVal = server.arg("amount").toInt();
    if (addVal > 0) { coins += addVal; if (coins > MAX_COINS) coins = MAX_COINS; saveGameData(); beep(1250, 60); lastWebStatus = "Добавлено +" + String(addVal) + " монет"; markDirty(); }
  }
  server.send(200, "application/json", buildStatusJson());
}

void handleSpinWeb() {
  if (disabledMode) { lastWebStatus = "Приставка отключена"; server.send(200, "application/json", buildStatusJson()); return; }
  wakeScreen(); stopSongPlayback(); stopMetronome(false); gameState = ST_SLOT_GAME;
  if (!godMode && coins < FIXED_SPIN_COST) { autoSpin = false; gameState = ST_NO_MONEY; stateTimer = millis(); lastWebStatus = "Не хватает монет"; }
  else { startSpin(); lastWebStatus = "Крутится спин..."; }
  markDirty(); server.send(200, "application/json", buildStatusJson());
}

void handleOtaUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    disabledMode = false; wakeScreen(); stopSongPlayback(); stopMetronome(false);
    otaArmed = true; otaStartFailed = false; otaWriteFailed = false; otaUploadedBytes = 0; otaProgressPct = 0; lastOtaUiMs = 0;
    gameState = ST_OTA_UPDATING; lastWebStatus = "Прошивка OTA..."; markDirty(); renderDisplay();
    if (!Update.begin((upload.totalSize > 0) ? upload.totalSize : UPDATE_SIZE_UNKNOWN)) { otaStartFailed = true; lastWebStatus = "Ошибка старта OTA"; }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (otaStartFailed || otaWriteFailed) return;
    size_t written = Update.write(upload.buf, upload.currentSize);
    if (written != upload.currentSize) { otaWriteFailed = true; lastWebStatus = "Ошибка записи блока"; }
    else {
      otaUploadedBytes += written;
      if (upload.totalSize > 0) otaProgressPct = constrain((int)(((unsigned long long)otaUploadedBytes * 100ULL) / upload.totalSize), 0, 99);
    }
    unsigned long m = millis();
    if ((unsigned long)(m - lastOtaUiMs) >= 160) { lastOtaUiMs = m; renderDisplay(); }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!otaStartFailed && !otaWriteFailed && Update.end(true)) {
      otaProgressPct = 100; gameState = ST_OTA_DONE; stateTimer = millis(); restartAtMs = millis() + 550; lastWebStatus = "OTA готово! Ребут...";
    } else { Update.abort(); lastWebStatus = "Ошибка завершения OTA"; gameState = ST_RECOVERY_MENU; }
    markDirty(); renderDisplay();
  }
}

void handleOtaFinish() {
  bool ok = !otaStartFailed && !otaWriteFailed && !Update.hasError();
  server.send(200, "text/plain", ok ? "Успешно! Приставка перезагружается..." : "Ошибка прошивки.");
  if (ok) restartAtMs = millis() + 450;
}

void setupWebServerRoutes() {
  server.on("/", HTTP_GET, handleRootWeb);
  server.on("/status", HTTP_GET, handleStatusWeb);
  server.on("/notes", HTTP_GET, []() { server.send(200, "application/json", buildNotesJsonArray()); });
  server.on("/songs", HTTP_GET, []() { server.send(200, "application/json", buildSongsJsonArray()); });
  server.on("/clicker/tap", HTTP_POST, handleClickerTapWeb);
  server.on("/clicker/reset", HTTP_POST, handleClickerResetWeb);
  server.on("/timer/set", HTTP_POST, handleTimerSetWeb);
  server.on("/timer/stop", HTTP_POST, handleTimerStopWeb);
  server.on("/ping", HTTP_POST, handlePingWeb);
  server.on("/banner", HTTP_POST, handleBannerWeb);
  server.on("/god", HTTP_POST, handleGodWeb);
  server.on("/disable", HTTP_POST, handleDisableWeb);
  server.on("/records/set", HTTP_POST, handleRecordsSetWeb);
  server.on("/mode", HTTP_POST, handleModeWeb);
  server.on("/piano", HTTP_POST, handlePianoWeb);
  server.on("/songs/add", HTTP_POST, handleSongsAddWeb);
  server.on("/songs/import", HTTP_POST, handleSongsImportWeb);
  server.on("/songs/play", HTTP_POST, handleSongsPlayWeb);
  server.on("/songs/delete", HTTP_POST, handleSongsDeleteWeb);
  server.on("/notes/add", HTTP_POST, handleNotesAddWeb);
  server.on("/notes/delete", HTTP_POST, handleNotesDeleteWeb);
  server.on("/coins", HTTP_POST, handleCoinsWeb);
  server.on("/spin", HTTP_POST, handleSpinWeb);
  server.on("/update", HTTP_GET, []() { server.send_P(200, "text/html", OTA_PAGE_HTML); });
  server.on("/update", HTTP_POST, handleOtaFinish, handleOtaUpload);
}

void ensureWebProbeStarted() {
  if (WiFi.status() == WL_CONNECTED && !probeStarted && !webServerStarted) {
    probeServer.begin(WEB_PORT); probeServer.setNoDelay(true); probeStarted = true;
  } else if (WiFi.status() != WL_CONNECTED) {
    if (probeStarted) { probeServer.stop(); probeStarted = false; }
    if (webServerStarted) { server.stop(); webServerStarted = false; }
  }
}

void activateWebServerOnDemand() {
  if (webServerStarted) return;
  if (probeStarted) { probeServer.stop(); probeStarted = false; }
  setupWebServerRoutes(); server.begin(WEB_PORT); webServerStarted = true;
  lastWebStatus = "Веб-панель подключена"; markDirty();
}

void serviceWebServer(unsigned long now) {
  ensureWebProbeStarted();
  if (!webServerStarted) {
    if (probeStarted) {
      WiFiClient incoming = probeServer.available();
      if (incoming) {
        activateWebServerOnDemand();
        unsigned long waitStart = millis();
        while (!incoming.available() && ((unsigned long)(millis() - waitStart) < 100)) yield();
        server.handleClient();
        if (incoming.connected()) incoming.stop();
      }
    }
    return;
  }
  (void)now; server.handleClient();
}

unsigned long getCurrentStopwatchMs() { return stopwatchRunning ? (stopwatchElapsed + (millis() - stopwatchStart)) : stopwatchElapsed; }

unsigned long getCurrentTimerRemainingMs() {
  if (!timerRunning) return timerRemainingMs;
  unsigned long now = millis();
  return ((long)(now - timerTargetMs) >= 0) ? 0 : (timerTargetMs - now);
}

void adjustTimerField(int delta) {
  if (timerFieldCursor == 0) timerHoursSet = (timerHoursSet + delta + 24) % 24;
  else if (timerFieldCursor == 1) timerMinutesSet = (timerMinutesSet + delta + 60) % 60;
  else timerSecondsSet = (timerSecondsSet + delta + 60) % 60;
}

void resetSaveData() {
  godMode = false; disabledMode = false;
  coins = 150; displayCoins = 150; totalSpins = 0; maxWin = 0;
  clickerCount = 0; clickerPower = 1; clickerAuto = 0; clickerCombo = 1;
  flappyRecord = 0; tetrisRecord = 0; snakeRecord = 0; bottleRecord = 0; pongRecord = 0; dinoRecord = 0; doodleRecord = 0;
  for (int i = 0; i < ACH_COUNT; i++) achievements[i].unlocked = false;
  saveGameData(); markDirty();
}

void loadSettings() {
  if (EEPROM.read(ADDR_FLAG) == 0x55) {
    EEPROM.get(ADDR_COINS, coins);
    EEPROM.get(ADDR_SPINS, totalSpins);
    EEPROM.get(ADDR_MAX_WIN, maxWin);
    EEPROM.get(ADDR_FLAPPY_REC, flappyRecord);
    EEPROM.get(ADDR_TETRIS_REC, tetrisRecord);
    EEPROM.get(ADDR_SNAKE_REC, snakeRecord);
    EEPROM.get(ADDR_BOTTLE_REC, bottleRecord);
    uint8_t achMask = EEPROM.read(ADDR_ACHIEVEMENTS);
    for (int i = 0; i < ACH_COUNT; i++) achievements[i].unlocked = (achMask & (1 << i)) != 0;
    EEPROM.get(ADDR_PONG_REC, pongRecord);
    EEPROM.get(ADDR_DINO_REC, dinoRecord);
    EEPROM.get(ADDR_DOODLE_REC, doodleRecord);
    EEPROM.get(ADDR_CLICKER_COUNT, clickerCount);

    for (int i = 0; i < 32; i++) currentSsid[i] = EEPROM.read(ADDR_SSID + i);
    for (int i = 0; i < 32; i++) currentPass[i] = EEPROM.read(ADDR_PASS + i);
    currentSsid[31] = '\0'; currentPass[31] = '\0';

    if (coins < 0 || (coins > MAX_COINS && coins != GOD_COINS_VAL)) coins = 150;
    if (coins == GOD_COINS_VAL) godMode = true;
    if (clickerCount < 0 || clickerCount > 999999999L) clickerCount = 0;
    clickerPower = 1;
    clickerAuto = 0;
    if (flappyRecord < 0 || flappyRecord > 99999) flappyRecord = 0;
    if (tetrisRecord < 0 || tetrisRecord > 99999) tetrisRecord = 0;
    if (snakeRecord < 0 || snakeRecord > 99999) snakeRecord = 0;
    if (bottleRecord < 0 || bottleRecord > 99999) bottleRecord = 0;
    if (pongRecord < 0 || pongRecord > 99999) pongRecord = 0;
    if (dinoRecord < 0 || dinoRecord > 99999) dinoRecord = 0;
    if (doodleRecord < 0 || doodleRecord > 99999) doodleRecord = 0;
  } else {
    resetSaveData();
  }
  displayCoins = coins;
}

void saveGameData() {
  clickerPower = 1;
  clickerAuto = 0;
  EEPROM.put(ADDR_COINS, coins);
  EEPROM.put(ADDR_SPINS, totalSpins);
  EEPROM.put(ADDR_MAX_WIN, maxWin);
  EEPROM.put(ADDR_FLAPPY_REC, flappyRecord);
  EEPROM.put(ADDR_TETRIS_REC, tetrisRecord);
  EEPROM.put(ADDR_SNAKE_REC, snakeRecord);
  EEPROM.put(ADDR_BOTTLE_REC, bottleRecord);

  uint8_t achMask = 0;
  for (int i = 0; i < ACH_COUNT; i++) if (achievements[i].unlocked) achMask |= (1 << i);
  EEPROM.write(ADDR_ACHIEVEMENTS, achMask);
  EEPROM.write(ADDR_FLAG, 0x55);
  EEPROM.put(ADDR_PONG_REC, pongRecord);
  EEPROM.put(ADDR_DINO_REC, dinoRecord);
  EEPROM.put(ADDR_DOODLE_REC, doodleRecord);
  EEPROM.put(ADDR_CLICKER_COUNT, clickerCount);
  EEPROM.put(ADDR_CLICKER_PWR, clickerPower);
  EEPROM.put(ADDR_CLICKER_AUTO, clickerAuto);

  for (int i = 0; i < 32; i++) EEPROM.write(ADDR_SSID + i, currentSsid[i]);
  for (int i = 0; i < 32; i++) EEPROM.write(ADDR_PASS + i, currentPass[i]);
  EEPROM.commit();
}

void unlockAchievement(uint8_t id) {
  if (id >= ACH_COUNT || achievements[id].unlocked) return;
  achievements[id].unlocked = true;
  activeAchPopup = id;
  achPopupTimer = millis();
  if (soundEnabled) tone(BUZZER_PIN, 1450, 90);
  saveGameData();
  markDirty();
}

void setup() {
  setCpuFrequencyMhz(240);
  Serial.begin(115200);
  pinMode(BTN_UP_PIN, INPUT_PULLUP);
  pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_LEFT_PIN, INPUT_PULLUP);
  pinMode(BTN_RIGHT_PIN, INPUT_PULLUP);
  pinMode(BTN_SELECT_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  EEPROM.begin(EEPROM_SIZE);
  loadSettings();
  initStorage();
  evaluateCalcExpression(calcExpr);

  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setBusClock(800000);
  u8g2.setContrast(220);

  randomSeed(esp_random() ^ (micros() + analogRead(34)));

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setAutoReconnect(true);
  if (strlen(currentSsid) > 0) WiFi.begin(currentSsid, currentPass);

  bool recoveryBoot = (digitalRead(BTN_SELECT_PIN) == LOW);
  gameState = recoveryBoot ? ST_RECOVERY_MENU : ST_SPLASH;
  menuCursor = 0;
  prevObservedState = gameState;
  stateTimer = millis();
  idleTimer = millis();
  lastFrameTime = millis();
  lastAutoClickMs = millis();
  markDirty();
}

void loop() {
  unsigned long now = millis();

  if (restartAtMs > 0 && (long)(now - restartAtMs) >= 0) ESP.restart();

  pollWifiScanAsync(now);
  maintainWeakWifiKeepalive(now);
  updateToneSeq(now);
  updateSongPlayback(now);
  updateMetronome(now);
  updateClicker(now);
  updateClockCache(now);
  handleButtons();

  now = millis();
  serviceWebServer(now);

  if (disabledMode) {
    if (gameState != prevObservedState) { prevObservedState = gameState; markDirty(); }
    if ((unsigned long)(now - lastFrameTime) >= 40) { lastFrameTime = now; renderDisplay(); }
    yield(); return;
  }

  if (godMode && coins != GOD_COINS_VAL) { coins = GOD_COINS_VAL; displayCoins = GOD_COINS_VAL; }
  if (WiFi.status() == WL_CONNECTED) {
    ipAddressStr = WiFi.localIP().toString();
    if (!ntpConfigured && gameState == ST_NET_CLOCK) syncNtpTime(false);
  } else ipAddressStr = "ОФФЛАЙН";
  if (coins >= 1000) unlockAchievement(3);

  // Фоновый тик таймера — продолжает идти независимо от текущего экрана
  if (timerRunning) {
    if ((long)(now - timerTargetMs) >= 0) {
      timerRunning = false; timerRemainingMs = 0; gameState = ST_TIMER_DONE; stateTimer = now;
      timerAlarmStep = 1; timerAlarmTime = now;
      wakeScreen();
      if (soundEnabled) tone(BUZZER_PIN, 1000, 180);
      markDirty();
    } else {
      timerRemainingMs = timerTargetMs - now;
    }
  }

  if (gameState == ST_TIMER_DONE && soundEnabled && timerAlarmStep > 0 && timerAlarmStep < 3) {
    if ((unsigned long)(now - timerAlarmTime) >= 240) {
      timerAlarmTime = now; timerAlarmStep++;
      tone(BUZZER_PIN, (timerAlarmStep == 2) ? 1250 : 1500, 200);
      if (timerAlarmStep >= 3) timerAlarmStep = 0;
    }
  }

  if (gameState == ST_SPLASH && ((unsigned long)(now - stateTimer) > 2500)) { gameState = ST_MAIN_MENU; menuCursor = 0; }
  if (gameState == ST_WEB_MESSAGE && (long)(now - webBannerUntilMs) >= 0) { gameState = ST_MAIN_MENU; menuCursor = 0; }

  if (gameState == ST_SLOT_GAME) {
    if (autoSpin) { wakeScreen(); startSpin(); }
    else if (!screenSleeping && ((unsigned long)(now - idleTimer) > 30000)) startArchScreensaver();
  }

  if (screenSleeping) updateArchScreensaver();
  if (gameState == ST_SPINNING) handleSpinning();
  if (gameState == ST_SNAKE_GAME) updateSnake();
  if (gameState == ST_TETRIS_GAME) updateTetris();
  if (gameState == ST_FLAPPY_GAME) updateFlappy();
  if (gameState == ST_PONG_GAME) updatePong();
  if (gameState == ST_DINO_GAME) updateDino();
  if (gameState == ST_DOODLE_GAME) updateDoodle();

  if (gameState == ST_WIN_ANIM && ((unsigned long)(now - stateTimer) > 1300)) gameState = ST_SLOT_GAME;
  if (gameState == ST_LOSE_ANIM && ((unsigned long)(now - stateTimer) > 850)) gameState = ST_SLOT_GAME;
  if (gameState == ST_NO_MONEY && ((unsigned long)(now - stateTimer) > 1200)) gameState = ST_SLOT_GAME;
  if (gameState == ST_BOTTLE_GAME && bottleState == 2 && ((unsigned long)(now - stateTimer) > 1500)) { bottleState = 0; bottleLanded = false; markDirty(); }

  if (gameState == ST_TEST_MELODY) {
    if (!testMelodyStarted) {
      testMelodyStarted = true;
      if (soundEnabled) playJingleBells();
      else { testMelodyStarted = false; gameState = ST_TEST_MODE; }
    }
  } else testMelodyStarted = false;

  if (gameState == ST_WIFI_CONNECTING) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      wifiConnectAttempts = 0;
      ipAddressStr = WiFi.localIP().toString();
      syncNtpTime(true); fetchWeatherIfNeeded(true);
      lastWebStatus = "Wi-Fi подключён: " + String(currentSsid);
      gameState = ST_WIFI_CONNECTED_INFO; ignoreNextIpDismiss = true;
    } else if (st == WL_CONNECT_FAILED || (unsigned long)(now - stateTimer) > 8500UL) {
      wifiConnectAttempts++;
      if (wifiConnectAttempts >= 2) {
        wifiConnectAttempts = 0;
        WiFi.disconnect();
        lastWebStatus = "Ошибка подключения Wi-Fi";
        gameState = ST_WIFI_ERROR;
        stateTimer = now;
      } else {
        stateTimer = now;
        WiFi.disconnect();
        WiFi.begin(currentSsid, currentPass);
      }
    }
  }

  if (gameState == ST_WIFI_ERROR && ((unsigned long)(now - stateTimer) > 2200)) { gameState = ST_SETTINGS_MENU; menuCursor = 0; }

  if (godMode) displayCoins = GOD_COINS_VAL;
  else if (displayCoins != coins) {
    long diff = coins - displayCoins;
    long step = (abs(diff) > 25) ? diff / 2 : (diff > 0 ? 1 : -1);
    if (step == 0) step = (diff > 0 ? 1 : -1);
    displayCoins += step;
  }

  if (gameState != prevObservedState) { prevObservedState = gameState; markDirty(); }

  unsigned long frameInterval = (gameState == ST_SPINNING) ? 20UL : 22UL;
  if ((unsigned long)(now - lastFrameTime) >= frameInterval) { lastFrameTime = now; renderDisplay(); }

  yield();
}

void startDoodle() {
  doodleScore = 0; doodleX = 60.0f; doodleY = 42.0f; doodleVX = 0.0f; doodleVY = DOODLE_JUMP_V;
  doodlePlats[0] = {46.0f, 56.0f, 34};
  for (int i = 1; i < DOODLE_PLAT_COUNT; i++) {
    uint8_t pw = (uint8_t)random(22, 34);
    doodlePlats[i] = {(float)random(2, 128 - pw - 2), 56.0f - (float)i * 9.0f, pw};
  }
  gameState = ST_DOODLE_GAME; doodleTimer = millis(); markDirty();
}

void updateDoodle() {
  unsigned long now = millis();
  if ((unsigned long)(now - doodleTimer) < 22) return;
  doodleTimer = now;
  bool l = (digitalRead(BTN_LEFT_PIN) == LOW), r = (digitalRead(BTN_RIGHT_PIN) == LOW);
  if (l && !r) doodleVX = -2.8f; else if (r && !l) doodleVX = 2.8f; else doodleVX *= 0.72f;
  doodleX += doodleVX;
  if (doodleX < -6.0f) doodleX = 126.0f; if (doodleX > 128.0f) doodleX = -4.0f;
  float prevBottom = doodleY + 8.0f;
  doodleVY += DOODLE_GRAV; doodleY += doodleVY;
  float nextBottom = doodleY + 8.0f;
  if (doodleVY > 0.0f) {
    for (int i = 0; i < DOODLE_PLAT_COUNT; i++) {
      float platTop = doodlePlats[i].y;
      if (prevBottom <= platTop + 2.0f && nextBottom >= platTop && doodleX + 6.0f >= doodlePlats[i].x && doodleX <= doodlePlats[i].x + doodlePlats[i].w) {
        doodleY = platTop - 8.0f; doodleVY = DOODLE_JUMP_V; beep(1050, 22); break;
      }
    }
  }
  if (doodleY < 24.0f) {
    float shift = 24.0f - doodleY; doodleY = 24.0f; doodleScore += max(1, (int)roundf(shift));
    if (doodleScore > doodleRecord) { doodleRecord = doodleScore; if (!godMode) saveGameData(); }
    for (int i = 0; i < DOODLE_PLAT_COUNT; i++) {
      doodlePlats[i].y += shift;
      if (doodlePlats[i].y > 64.0f) {
        uint8_t pw = (uint8_t)random(22, 34); doodlePlats[i].w = pw;
        doodlePlats[i].x = (float)random(2, 128 - pw - 2); doodlePlats[i].y -= 66.0f;
      }
    }
  }
  if (doodleY > 64.0f) {
    if (godMode) { doodleY = 44.0f; doodleVY = DOODLE_JUMP_V * 1.15f; return; }
    beep(230, 220); if (doodleScore > doodleRecord) { doodleRecord = doodleScore; saveGameData(); }
    gameState = ST_DOODLE_OVER; stateTimer = now; markDirty();
  }
}

void spawnCactus() {
  cactusX = 128.0f + (float)random(0, 24);
  int t = random(0, 3);
  if (t == 0) { cactusW = 5; cactusH = 10; } else if (t == 1) { cactusW = 7; cactusH = 14; } else { cactusW = 10; cactusH = 11; }
}

void startDino() {
  dinoY = DINO_GROUND_Y - 12; dinoVY = 0.0f; dinoJumping = false; dinoDucking = false; dinoSpeed = 2.6f; dinoScore = 0;
  spawnCactus(); gameState = ST_DINO_GAME; dinoTimer = millis(); markDirty();
}

void updateDino() {
  unsigned long now = millis();
  if ((unsigned long)(now - dinoTimer) < 25) return;
  dinoTimer = now;
  dinoDucking = (!dinoJumping && digitalRead(BTN_DOWN_PIN) == LOW);
  if (dinoJumping) {
    dinoVY += DINO_GRAV; dinoY += dinoVY;
    if (dinoY >= DINO_GROUND_Y - 12) { dinoY = DINO_GROUND_Y - 12; dinoVY = 0.0f; dinoJumping = false; }
  } else dinoY = dinoDucking ? (DINO_GROUND_Y - 7) : (DINO_GROUND_Y - 12);
  cactusX -= dinoSpeed;
  if (cactusX < -cactusW) {
    dinoScore++; dinoSpeed = min(5.4f, 2.6f + dinoScore * 0.06f);
    if (dinoScore > dinoRecord) { dinoRecord = dinoScore; if (!godMode) saveGameData(); }
    beep(1150, 25); spawnCactus();
  }
  int dW = dinoDucking ? 10 : 8, dH = dinoDucking ? 7 : 12;
  if ((DINO_X + dW - 1 >= (int)cactusX) && (DINO_X + 1 <= (int)cactusX + cactusW) && ((int)dinoY + dH - 1 >= DINO_GROUND_Y - cactusH)) {
    if (godMode) { if (!dinoJumping) { dinoJumping = true; dinoVY = DINO_JUMP_V; } return; }
    beep(220, 240); if (dinoScore > dinoRecord) { dinoRecord = dinoScore; saveGameData(); }
    gameState = ST_DINO_OVER; stateTimer = now; markDirty();
  }
}

void startPong() {
  paddleX = (128 - PADDLE_W) / 2.0f; ballX = 64.0f; ballY = 18.0f;
  ballVX = (random(0, 2) == 0) ? 1.8f : -1.8f; ballVY = 1.6f; pongScore = 0;
  gameState = ST_PONG_GAME; pongTimer = millis(); markDirty();
}

void updatePong() {
  unsigned long now = millis();
  if ((unsigned long)(now - pongTimer) < 25) return;
  pongTimer = now;
  if (digitalRead(BTN_LEFT_PIN) == LOW) { paddleX -= 3.5f; if (paddleX < 0) paddleX = 0; }
  if (digitalRead(BTN_RIGHT_PIN) == LOW) { paddleX += 3.5f; if (paddleX > 128 - PADDLE_W) paddleX = 128 - PADDLE_W; }
  if (godMode) paddleX = constrain(ballX - PADDLE_W / 2.0f, 0.0f, (float)(128 - PADDLE_W));
  ballX += ballVX; ballY += ballVY;
  if (ballX <= 1) { ballX = 1; ballVX = -ballVX; beep(700, 20); }
  if (ballX >= 125) { ballX = 125; ballVX = -ballVX; beep(700, 20); }
  if (ballY <= 12) { ballY = 12; ballVY = -ballVY; beep(850, 20); }
  int paddleY = 58;
  if (ballVY > 0 && ballY + 3 >= paddleY && ballY <= paddleY + PADDLE_H && ballX + 3 >= paddleX && ballX <= paddleX + PADDLE_W) {
    ballY = paddleY - 3; ballVY = -ballVY;
    float hitOffset = ((ballX + 1.5f) - (paddleX + PADDLE_W / 2.0f)) / (PADDLE_W / 2.0f);
    ballVX = hitOffset * 2.4f;
    if (fabs(ballVX) < 0.8f) ballVX = (ballVX >= 0) ? 1.0f : -1.0f;
    pongScore++; if (pongScore > pongRecord) { pongRecord = pongScore; if (!godMode) saveGameData(); }
    beep(1200, 30);
  }
  if (ballY > 64) {
    if (godMode) { ballY = paddleY - 3; ballVY = -fabsf(ballVY); pongScore++; return; }
    beep(240, 240); gameState = ST_PONG_OVER; stateTimer = now; markDirty();
  }
}

void startFlappy() {
  flappyY = 32.0f; flappyVelocity = 0.0f; pipeX = 128.0f; pipeGapY = random(16, 38); flappyScore = 0;
  gameState = ST_FLAPPY_GAME; flappyTimer = millis(); markDirty();
}

void updateFlappy() {
  unsigned long now = millis();
  if ((unsigned long)(now - flappyTimer) < 36) return;
  flappyTimer = now;
  flappyVelocity += FLAPPY_GRAVITY; if (flappyVelocity > 3.6f) flappyVelocity = 3.6f;
  flappyY += flappyVelocity; pipeX -= 2.2f;
  if (pipeX < -PIPE_W) {
    pipeX = 128.0f; pipeGapY = random(16, 38); flappyScore++;
    if (flappyScore > flappyRecord) { flappyRecord = flappyScore; if (!godMode) saveGameData(); }
    beep(1200, 30);
  }
  if (godMode) {
    if (flappyY <= 2.0f) { flappyY = 2.0f; flappyVelocity = 0.4f; }
    if (flappyY >= 58.0f) { flappyY = 58.0f; flappyVelocity = FLAPPY_JUMP * 0.7f; }
    if (30 + 6 >= pipeX && 30 <= pipeX + PIPE_W) {
      float centerGap = pipeGapY + PIPE_GAP_H * 0.5f;
      if (flappyY < pipeGapY + 2 || flappyY > pipeGapY + PIPE_GAP_H - 6) flappyY += (flappyY < centerGap) ? 2.0f : -2.0f;
    }
    return;
  }
  if (flappyY <= 0 || flappyY >= 60 || (30 + 6 >= pipeX && 30 <= pipeX + PIPE_W && (flappyY < pipeGapY || flappyY > pipeGapY + PIPE_GAP_H))) {
    beep(250, 250); gameState = ST_FLAPPY_OVER; stateTimer = now; markDirty();
  }
}

void getRotatedShape(int shape, int rot, byte outMat[4][4]) {
  for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) outMat[r][c] = TETRIS_SHAPES[shape][r][c];
  rot = ((rot % 4) + 4) % 4;
  for (int i = 0; i < rot; i++) {
    byte temp[4][4];
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) temp[c][3 - r] = outMat[r][c];
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) outMat[r][c] = temp[r][c];
  }
}

void startTetris() {
  for (int r = 0; r < TETRIS_H; r++) for (int c = 0; c < TETRIS_W; c++) tetrisBoard[r][c] = 0;
  tetrisScore = 0; tetrisLines = 0; tetrisLevel = 1;
  tetrisCurrShape = random(0, 7); tetrisNextShape = random(0, 7); tetrisCurrRot = 0; tetrisX = 3; tetrisY = 0;
  gameState = ST_TETRIS_GAME; tetrisTimer = millis(); markDirty();
}

void spawnTetrisPiece() {
  tetrisCurrShape = tetrisNextShape; tetrisNextShape = random(0, 7); tetrisCurrRot = 0; tetrisX = 3; tetrisY = 0;
  if (checkTetrisCollision(tetrisX, tetrisY, tetrisCurrRot)) {
    if (godMode) { for (int c = 0; c < TETRIS_W; c++) tetrisBoard[0][c] = 0; tetrisY = 0; return; }
    beep(200, 300); if (tetrisScore > tetrisRecord) { tetrisRecord = tetrisScore; saveGameData(); }
    gameState = ST_TETRIS_OVER; stateTimer = millis(); markDirty();
  }
}

bool checkTetrisCollision(int px, int py, int rot) {
  byte mat[4][4]; getRotatedShape(tetrisCurrShape, rot, mat);
  for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
    if (!mat[r][c]) continue;
    int bx = px + c, by = py + r;
    if (bx < 0 || bx >= TETRIS_W || by >= TETRIS_H) return true;
    if (by >= 0 && tetrisBoard[by][bx]) return true;
  }
  return false;
}

void mergeTetrisPiece() {
  byte mat[4][4]; getRotatedShape(tetrisCurrShape, tetrisCurrRot, mat);
  for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) if (mat[r][c]) {
    int bx = tetrisX + c, by = tetrisY + r;
    if (by >= 0 && by < TETRIS_H && bx >= 0 && bx < TETRIS_W) tetrisBoard[by][bx] = 1;
  }
}

void clearTetrisLines() {
  int cleared = 0;
  for (int r = TETRIS_H - 1; r >= 0; r--) {
    bool full = true;
    for (int c = 0; c < TETRIS_W; c++) if (!tetrisBoard[r][c]) { full = false; break; }
    if (full) {
      cleared++;
      for (int mr = r; mr > 0; mr--) for (int c = 0; c < TETRIS_W; c++) tetrisBoard[mr][c] = tetrisBoard[mr - 1][c];
      for (int c = 0; c < TETRIS_W; c++) tetrisBoard[0][c] = 0;
      r++;
    }
  }
  if (cleared > 0) {
    tetrisLines += cleared; tetrisScore += cleared * 100 * tetrisLevel; tetrisLevel = 1 + tetrisLines / 4;
    if (tetrisScore >= 500) unlockAchievement(5);
    if (tetrisScore > tetrisRecord) { tetrisRecord = tetrisScore; if (!godMode) saveGameData(); }
    beep(1000, 60);
  }
}

void updateTetris() {
  unsigned long now = millis();
  long rawInt = 450L - (long)(tetrisLevel - 1) * 70L;
  if ((unsigned long)(now - tetrisTimer) < ((rawInt < 100L) ? 100UL : (unsigned long)rawInt)) return;
  tetrisTimer = now;
  if (!checkTetrisCollision(tetrisX, tetrisY + 1, tetrisCurrRot)) tetrisY++;
  else { mergeTetrisPiece(); clearTetrisLines(); spawnTetrisPiece(); }
}

void startSnake() {
  snakeLen = 3; snake[0] = {8, 4}; snake[1] = {7, 4}; snake[2] = {6, 4};
  dx = 1; dy = 0; snakeScore = 0; spawnFood(); gameState = ST_SNAKE_GAME; snakeTimer = millis(); markDirty();
}

void spawnFood() {
  for (int attempt = 0; attempt < 256; attempt++) {
    int8_t tx = (int8_t)random(0, 16), ty = (int8_t)random(0, 8);
    bool occupied = false;
    for (int i = 0; i < snakeLen; i++) if (snake[i].x == tx && snake[i].y == ty) { occupied = true; break; }
    if (!occupied) { food.x = tx; food.y = ty; return; }
  }
  food.x = 0; food.y = 0;
}

void updateSnake() {
  unsigned long now = millis();
  if ((unsigned long)(now - snakeTimer) < 160) return;
  snakeTimer = now;
  int8_t nextX = snake[0].x + dx, nextY = snake[0].y + dy;
  bool hit = (nextX < 0 || nextX >= 16 || nextY < 0 || nextY >= 8);
  for (int i = 1; i < snakeLen && !hit; i++) if (snake[i].x == nextX && snake[i].y == nextY) hit = true;
  if (hit) {
    if (godMode) { dx = -dx; dy = -dy; nextX = constrain((int)(snake[0].x + dx), 0, 15); nextY = constrain((int)(snake[0].y + dy), 0, 7); }
    else { beep(250, 200); if (snakeScore > snakeRecord) { snakeRecord = snakeScore; saveGameData(); } gameState = ST_SNAKE_OVER; stateTimer = now; markDirty(); return; }
  }
  for (int i = snakeLen - 1; i > 0; i--) snake[i] = snake[i - 1];
  snake[0].x = nextX; snake[0].y = nextY;
  if (snake[0].x == food.x && snake[0].y == food.y) {
    if (snakeLen < 30) snakeLen++;
    snakeScore += 10;
    if (snakeScore >= 50) unlockAchievement(4);
    if (snakeScore > snakeRecord) { snakeRecord = snakeScore; if (!godMode) saveGameData(); }
    beep(1200, 40); spawnFood();
  }
}

void startSpin() {
  unsigned long now = millis();
  long costNow = godMode ? 0L : FIXED_SPIN_COST;
  if (!godMode && coins < costNow) {
    autoSpin = false; gameState = ST_NO_MONEY; stateTimer = now; lastWebStatus = "Не хватает монет"; beep(140, 160); markDirty(); return;
  }
  beep(560, 25);
  if (!godMode) coins -= costNow;
  totalSpins++;
  unlockAchievement(0);
  if (totalSpins >= 50) unlockAchievement(1);
  if (!godMode) saveGameData();

  if (godMode || random(0, 100) < 26) {
    int sym = godMode ? 1 : ((random(0, 100) < 45) ? 1 : random(0, ICON_COUNT));
    targetSlot1 = sym; targetSlot2 = sym; targetSlot3 = sym;
  } else {
    targetSlot1 = random(0, ICON_COUNT); targetSlot2 = random(0, ICON_COUNT); targetSlot3 = random(0, ICON_COUNT);
    if (targetSlot1 == targetSlot2 && targetSlot2 == targetSlot3) targetSlot3 = (targetSlot3 + 1) % ICON_COUNT;
  }

  gameState = ST_SPINNING; spinStartTime = now; spinLastTickMs = now; spinResolved = false;
  reel1Stopped = false; reel2Stopped = false; reel3Stopped = false;
  reelOffset1 = 0.0f; reelOffset2 = 0.0f; reelOffset3 = 0.0f;
  bounce1 = 0.0f; bounce2 = 0.0f; bounce3 = 0.0f;
  markDirty();
}

void handleSpinning() {
  unsigned long now = millis();
  unsigned long elapsed = now - spinStartTime;
  const float stepPitch = 24.0f;

  if (elapsed < 750) {
    reelOffset1 += 5.5f;
    if (reelOffset1 >= stepPitch) { reelOffset1 -= stepPitch; prevSlot1 = slot1; slot1 = (slot1 + 1 + random(0, ICON_COUNT - 1)) % ICON_COUNT; }
  } else if (!reel1Stopped) { reel1Stopped = true; reelOffset1 = 0.0f; bounce1 = 1.5f; slot1 = targetSlot1; prevSlot1 = targetSlot1; beep(660, 18); }

  if (elapsed < 1150) {
    reelOffset2 += 6.0f;
    if (reelOffset2 >= stepPitch) { reelOffset2 -= stepPitch; prevSlot2 = slot2; slot2 = (slot2 + 1 + random(0, ICON_COUNT - 1)) % ICON_COUNT; }
  } else if (!reel2Stopped) { reel2Stopped = true; reelOffset2 = 0.0f; bounce2 = 1.5f; slot2 = targetSlot2; prevSlot2 = targetSlot2; beep(780, 18); }

  if (elapsed < 1550) {
    reelOffset3 += 6.5f;
    if (reelOffset3 >= stepPitch) { reelOffset3 -= stepPitch; prevSlot3 = slot3; slot3 = (slot3 + 1 + random(0, ICON_COUNT - 1)) % ICON_COUNT; }
  } else if (!reel3Stopped) { reel3Stopped = true; reelOffset3 = 0.0f; bounce3 = 1.5f; slot3 = targetSlot3; prevSlot3 = targetSlot3; beep(920, 22); }

  if (bounce1 > 0.2f) bounce1 *= 0.5f; else bounce1 = 0.0f;
  if (bounce2 > 0.2f) bounce2 *= 0.5f; else bounce2 = 0.0f;
  if (bounce3 > 0.2f) bounce3 *= 0.5f; else bounce3 = 0.0f;

  if (reel1Stopped && reel2Stopped && reel3Stopped && elapsed >= 1620 && !spinResolved) {
    spinResolved = true; slot1 = targetSlot1; slot2 = targetSlot2; slot3 = targetSlot3; checkWin();
  }
}

void checkWin() {
  slot1 = targetSlot1; slot2 = targetSlot2; slot3 = targetSlot3;
  if (slot1 == slot2 && slot2 == slot3) {
    int mult = (slot1 == 1) ? 50 : (slot1 == 5) ? 35 : (slot1 == 3) ? 25 : (slot1 == 4) ? 20 : (slot1 == 2) ? 15 : 6;
    currentWin = FIXED_SPIN_COST * mult;
    if (godMode) { coins = GOD_COINS_VAL; displayCoins = GOD_COINS_VAL; }
    else { coins += currentWin; if (coins > MAX_COINS) coins = MAX_COINS; }
    if (currentWin > maxWin) maxWin = currentWin;
    if (slot1 == 1 || slot1 == 5) unlockAchievement(2);
    if (!godMode) saveGameData();
    lastWebStatus = (slot1 == 1) ? ("ДЖЕКПОТ! +" + String(currentWin)) : ("Победа! Куш +" + String(currentWin));
    if (slot1 == 1 || slot1 == 5) playJackpotSound(); else playWinSound();
    gameState = ST_WIN_ANIM;
  } else {
    currentWin = 0; lastWebStatus = "Не повезло в этом спине"; playLoseSound(); gameState = ST_LOSE_ANIM;
  }
  stateTimer = millis(); markDirty();
}

void handleButtons() {
  bool u = (digitalRead(BTN_UP_PIN) == LOW);
  bool d = (digitalRead(BTN_DOWN_PIN) == LOW);
  bool l = (digitalRead(BTN_LEFT_PIN) == LOW);
  bool r = (digitalRead(BTN_RIGHT_PIN) == LOW);
  bool s = (digitalRead(BTN_SELECT_PIN) == LOW);

  bool anyNow = (u || d || l || r || s);
  bool anyPrev = (upPressed || downPressed || leftPressed || rightPressed || selectPressed);
  unsigned long now = millis();
  if (anyNow) idleTimer = now;

  if (disabledMode) { upPressed = u; downPressed = d; leftPressed = l; rightPressed = r; selectPressed = s; return; }

  if (screenSleeping && anyNow && !anyPrev) {
    wakeScreen(); upPressed = u; downPressed = d; leftPressed = l; rightPressed = r; selectPressed = s;
    selectPressStartMs = s ? now : 0; selectLongPressHandled = false; lastBtnActionMs = now;
    beep(900, 30); return;
  }

  if (gameState == ST_OTA_UPDATING || gameState == ST_OTA_DONE || gameState == ST_WIFI_SCAN) {
    upPressed = u; downPressed = d; leftPressed = l; rightPressed = r; selectPressed = s; return;
  }

  if (s && !selectPressed) { selectPressStartMs = now; selectLongPressHandled = false; }
  else if (!s) { selectPressStartMs = 0; selectLongPressHandled = false; }

  if (isAnyActiveGameState(gameState) && s && !selectLongPressHandled && ((unsigned long)(now - selectPressStartMs) >= LONG_PRESS_MS)) {
    selectLongPressHandled = true;
    selectPressed = true;
    lastBtnActionMs = now;
    exitCurrentGameToSelect();
    upPressed = u; downPressed = d; leftPressed = l; rightPressed = r;
    return;
  }

  if (gameState == ST_CALCULATOR && s && !selectLongPressHandled && ((unsigned long)(now - selectPressStartMs) >= LONG_PRESS_MS)) {
    selectLongPressHandled = true; lastBtnActionMs = now; calcExpr[0] = '\0'; calcHasResult = false; calcError = false; beep(600, 50);
  }

  if (gameState == ST_METRONOME && s && !selectLongPressHandled && ((unsigned long)(now - selectPressStartMs) >= LONG_PRESS_MS)) {
    selectLongPressHandled = true; selectPressed = true; lastBtnActionMs = now; stopMetronome(true); beep(500, 45);
    upPressed = u; downPressed = d; leftPressed = l; rightPressed = r; return;
  }

  bool canTrigger = ((unsigned long)(now - lastBtnActionMs) >= BTN_COOLDOWN_MS);

  if (gameState == ST_WEB_MESSAGE) {
    if (canTrigger && anyNow && !anyPrev) { lastBtnActionMs = now; gameState = ST_MAIN_MENU; menuCursor = 0; beep(700, 30); }
    upPressed = u; downPressed = d; leftPressed = l; rightPressed = r; selectPressed = s; return;
  }

  if (gameState == ST_OTA_WAIT) {
    if (canTrigger && l && !leftPressed) { leftPressed = true; lastBtnActionMs = now; otaArmed = false; gameState = ST_RECOVERY_MENU; menuCursor = 0; beep(500, 40); }
    else if (!l) leftPressed = false;
    upPressed = u; downPressed = d; rightPressed = r; selectPressed = s; return;
  }

  if (gameState == ST_TIMER_DONE) {
    if (canTrigger && (u || d || l || r || s)) { lastBtnActionMs = now; timerAlarmStep = 0; beep(800, 30); gameState = ST_CLOCK_MENU; menuCursor = 2; }
    upPressed = u; downPressed = d; leftPressed = l; rightPressed = r; selectPressed = s; return;
  }

  if (gameState == ST_TEST_MODE) {
    if (canTrigger && (long)(now - testExitLockUntil) >= 0 && s && u) { lastBtnActionMs = now; beep(1000, 50); gameState = ST_SETTINGS_MENU; menuCursor = 4; testExitLockUntil = now + 300; }
    upPressed = u; downPressed = d; leftPressed = l; rightPressed = r; selectPressed = s; return;
  }

  if (gameState == ST_TEST_CONFIRM) {
    if (canTrigger && s && !selectPressed) { selectPressed = true; lastBtnActionMs = now; gameState = ST_TEST_MELODY; } else if (!s) selectPressed = false;
    if (canTrigger && u && !upPressed) { upPressed = true; lastBtnActionMs = now; gameState = ST_SETTINGS_MENU; menuCursor = 4; } else if (!u) upPressed = false;
    downPressed = d; leftPressed = l; rightPressed = r; return;
  }

  if (gameState == ST_WIFI_CONNECTED_INFO) {
    if (!u && !d && !l && !r && !s) ignoreNextIpDismiss = false;
    if (canTrigger && !ignoreNextIpDismiss && (u || d || l || r || s)) { lastBtnActionMs = now; beep(800, 30); gameState = ST_WIFI_MANAGEMENT; menuCursor = 1; }
    upPressed = u; downPressed = d; leftPressed = l; rightPressed = r; selectPressed = s; return;
  }

  if (gameState >= ST_RESET_STEP_1 && gameState <= ST_RESET_STEP_3) {
    if (canTrigger && u && !upPressed) {
      upPressed = true; lastBtnActionMs = now;
      if (gameState == ST_RESET_STEP_1) { beep(900, 40); gameState = ST_RESET_STEP_2; }
      else if (gameState == ST_RESET_STEP_2) { beep(900, 40); gameState = ST_RESET_STEP_3; }
      else { resetSaveData(); beep(1200, 150); gameState = ST_SETTINGS_MENU; menuCursor = 3; }
    } else if (!u) upPressed = false;
    if (canTrigger && (d || l) && (!downPressed && !leftPressed)) {
      downPressed = leftPressed = true; lastBtnActionMs = now; beep(400, 40); gameState = ST_SETTINGS_MENU; menuCursor = 3;
    } else if (!d && !l) downPressed = leftPressed = false;
    rightPressed = r; selectPressed = s; return;
  }

  if (canTrigger && u && !upPressed) {
    upPressed = true; lastBtnActionMs = now; wakeScreen();
    if (gameState == ST_MAIN_MENU || gameState == ST_GAME_SELECT || gameState == ST_SETTINGS_MENU ||
        gameState == ST_WIFI_MANAGEMENT || gameState == ST_ACHIEVEMENTS_MENU || gameState == ST_CLOCK_MENU ||
        gameState == ST_MUSIC_MENU || gameState == ST_RECOVERY_MENU) {
      int maxItem = getMaxMenuItem(gameState);
      menuCursor = (menuCursor - 1 + (maxItem + 1)) % (maxItem + 1); beep(1000, 30);
    } else if (gameState == ST_NOTES_LIST) {
      if (!notesList.empty()) { noteCursor = (noteCursor - 1 + (int)notesList.size()) % (int)notesList.size(); beep(1000, 25); }
    } else if (gameState == ST_NOTE_VIEW) {
      if (noteScrollLine > 0) { noteScrollLine--; beep(950, 20); }
    } else if (gameState == ST_PIANO_MODE) {
      if (!songsList.empty()) { pianoSongCursor = (pianoSongCursor - 1 + (int)songsList.size()) % (int)songsList.size(); beep(950, 25); }
    } else if (gameState == ST_METRONOME) { setMetronomeBpm(metroBpm + 1); beep(1100, 20); }
    else if (gameState == ST_CALCULATOR) { calcCharIdx = (calcCharIdx + 1) % CALC_CHAR_COUNT; beep(1000, 20); }
    else if (gameState == ST_TIMER_SET) { adjustTimerField(1); beep(1000, 20); }
    else if (gameState == ST_WIFI_LIST && wifiNetCount > 0) { menuCursor = (menuCursor - 1 + wifiNetCount) % wifiNetCount; beep(1000, 30); }
    else if (gameState == ST_WIFI_KEYBOARD || gameState == ST_COIN_KEYBOARD) { kbRow = (kbRow - 1 + 5) % 5; beep(1000, 20); }
    else if (gameState == ST_SNAKE_GAME && dy == 0) { dx = 0; dy = -1; beep(900, 20); }
    else if (gameState == ST_TETRIS_GAME) {
      int nr = (tetrisCurrRot + 1) % 4;
      if (!checkTetrisCollision(tetrisX, tetrisY, nr)) { tetrisCurrRot = nr; beep(900, 20); }
      else if (!checkTetrisCollision(tetrisX - 1, tetrisY, nr)) { tetrisX--; tetrisCurrRot = nr; beep(900, 20); }
      else if (!checkTetrisCollision(tetrisX + 1, tetrisY, nr)) { tetrisX++; tetrisCurrRot = nr; beep(900, 20); }
    }
  } else if (!u) upPressed = false;

  if (canTrigger && d && !downPressed) {
    downPressed = true; lastBtnActionMs = now; wakeScreen();
    if (autoSpin && (gameState >= ST_SLOT_GAME && gameState <= ST_LOSE_ANIM)) { autoSpin = false; beep(330, 80); markDirty(); return; }
    if (gameState == ST_MAIN_MENU || gameState == ST_GAME_SELECT || gameState == ST_SETTINGS_MENU ||
        gameState == ST_WIFI_MANAGEMENT || gameState == ST_ACHIEVEMENTS_MENU || gameState == ST_CLOCK_MENU ||
        gameState == ST_MUSIC_MENU || gameState == ST_RECOVERY_MENU) {
      int maxItem = getMaxMenuItem(gameState);
      menuCursor = (menuCursor + 1) % (maxItem + 1); beep(800, 30);
    } else if (gameState == ST_CLICKER_GAME) {
      resetClickerCounter(true);
    } else if (gameState == ST_NOTES_LIST) {
      if (!notesList.empty()) { noteCursor = (noteCursor + 1) % (int)notesList.size(); beep(800, 25); }
    } else if (gameState == ST_NOTE_VIEW) { noteScrollLine++; beep(850, 20); }
    else if (gameState == ST_PIANO_MODE) {
      if (!songsList.empty()) { pianoSongCursor = (pianoSongCursor + 1) % (int)songsList.size(); beep(850, 25); }
    } else if (gameState == ST_METRONOME) { setMetronomeBpm(metroBpm - 1); beep(850, 20); }
    else if (gameState == ST_CALCULATOR) { calcCharIdx = (calcCharIdx - 1 + CALC_CHAR_COUNT) % CALC_CHAR_COUNT; beep(800, 20); }
    else if (gameState == ST_TIMER_SET) { adjustTimerField(-1); beep(800, 20); }
    else if (gameState == ST_STOPWATCH) { stopwatchRunning = false; stopwatchElapsed = 0; beep(500, 30); }
    else if (gameState == ST_TIMER_RUNNING) {
      // Вниз сбрасывает таймер, а Влево (<) выходит из меню без остановки!
      timerRemainingMs = 0;
      timerRunning = false;
      gameState = ST_TIMER_SET;
      beep(500, 30);
    } else if (gameState == ST_WIFI_LIST && wifiNetCount > 0) { menuCursor = (menuCursor + 1) % wifiNetCount; beep(800, 30); }
    else if (gameState == ST_WIFI_KEYBOARD || gameState == ST_COIN_KEYBOARD) { kbRow = (kbRow + 1) % 5; beep(800, 20); }
    else if (gameState == ST_SLOT_GAME) { autoSpin = true; beep(880, 80); startSpin(); }
    else if (gameState == ST_SNAKE_GAME && dy == 0) { dx = 0; dy = 1; beep(900, 20); }
    else if (gameState == ST_TETRIS_GAME && !checkTetrisCollision(tetrisX, tetrisY + 1, tetrisCurrRot)) tetrisY++;
  } else if (!d) downPressed = false;

  if (canTrigger && l && !leftPressed) {
    leftPressed = true; lastBtnActionMs = now; wakeScreen();
    if (gameState == ST_SNAKE_GAME) {
      if (dx == 0) { dx = -1; dy = 0; beep(900, 20); }
    } else if (gameState == ST_TETRIS_GAME) {
      if (!checkTetrisCollision(tetrisX - 1, tetrisY, tetrisCurrRot)) { tetrisX--; beep(800, 20); }
    } else if (gameState == ST_SLOT_GAME || gameState == ST_BOTTLE_GAME ||
               gameState == ST_FLAPPY_GAME || gameState == ST_FLAPPY_OVER ||
               gameState == ST_DINO_GAME || gameState == ST_DINO_OVER ||
               gameState == ST_CLICKER_GAME || gameState == ST_SNAKE_OVER ||
               gameState == ST_TETRIS_OVER || gameState == ST_PONG_OVER ||
               gameState == ST_DOODLE_OVER) {
      exitCurrentGameToSelect();
    } else if (gameState == ST_NOTE_VIEW) {
      gameState = ST_NOTES_LIST; beep(500, 35);
    } else if (gameState == ST_NOTES_LIST) {
      gameState = ST_MAIN_MENU; menuCursor = 7; beep(500, 40);
    } else if (gameState == ST_PIANO_MODE) {
      stopSongPlayback(); gameState = ST_MUSIC_MENU; menuCursor = 0; beep(500, 40);
    } else if (gameState == ST_METRONOME) {
      stopMetronome(true); beep(500, 40);
    } else if (gameState == ST_MUSIC_MENU) {
      gameState = ST_MAIN_MENU; menuCursor = 8; beep(500, 40);
    } else if (gameState == ST_RECORDS_SCREEN) {
      gameState = ST_MAIN_MENU; menuCursor = 6; beep(500, 40);
    } else if (gameState == ST_CALCULATOR) {
      if (strlen(calcExpr) > 0) { backspaceCalcExpr(); beep(700, 25); }
      else { gameState = ST_MAIN_MENU; menuCursor = 5; beep(500, 40); }
    } else if (gameState == ST_GAME_SELECT) {
      gameState = ST_MAIN_MENU; menuCursor = 0; beep(500, 40);
    } else if (gameState == ST_TIMER_SET) {
      if (timerFieldCursor > 0) { timerFieldCursor--; beep(700, 20); }
      else { gameState = ST_CLOCK_MENU; menuCursor = 2; beep(500, 40); }
    } else if (gameState == ST_NET_CLOCK || gameState == ST_STOPWATCH) {
      int curIdx = (gameState == ST_NET_CLOCK) ? 0 : 1;
      gameState = ST_CLOCK_MENU; menuCursor = curIdx; beep(500, 40);
    } else if (gameState == ST_TIMER_RUNNING) {
      // Выходим в меню «Часы», таймер НЕ останавливается!
      if (timerRunning) {
        if ((long)(now - timerTargetMs) >= 0) { timerRunning = false; timerRemainingMs = 0; }
        else timerRemainingMs = timerTargetMs - now;
      }
      gameState = ST_CLOCK_MENU; menuCursor = 2; beep(500, 40);
    } else if (gameState == ST_INFO_SCREEN || gameState == ST_ACHIEVEMENTS_MENU) {
      menuCursor = (gameState == ST_INFO_SCREEN) ? 2 : 1; gameState = ST_MAIN_MENU; beep(500, 40);
    } else if (gameState == ST_WIFI_LIST || gameState == ST_WIFI_MANAGEMENT) {
      gameState = ST_SETTINGS_MENU; menuCursor = 0; beep(500, 40);
    } else if (gameState == ST_SETTINGS_MENU || gameState == ST_CREDITS_MENU || gameState == ST_CLOCK_MENU) {
      menuCursor = (gameState == ST_SETTINGS_MENU) ? 3 : (gameState == ST_CREDITS_MENU) ? 2 : 4; gameState = ST_MAIN_MENU; beep(500, 40);
    } else if (gameState == ST_WIFI_KEYBOARD) {
      kbCol = (kbCol - 1 + 10) % 10; beep(900, 20);
    } else if (gameState == ST_COIN_KEYBOARD) {
      kbCol = (kbCol - 1 + 3) % 3; beep(900, 20);
    }
  } else if (!l) leftPressed = false;

  if (canTrigger && r && !rightPressed) {
    rightPressed = true; lastBtnActionMs = now; wakeScreen();
    if (gameState == ST_NOTE_VIEW) { noteScrollLine++; beep(850, 20); }
    else if (gameState == ST_PIANO_MODE) {
      if (!songsList.empty()) { int nextIdx = (pianoSongCursor + 1) % (int)songsList.size(); pianoSongCursor = nextIdx; startSongPlayback(nextIdx); }
    } else if (gameState == ST_METRONOME) { setMetronomeBpm(metroBpm + 10); beep(1150, 25); }
    else if (gameState == ST_CALCULATOR) { appendCalcChar(CALC_CHARS[calcCharIdx]); beep(950, 25); }
    else if (gameState == ST_TIMER_SET) { timerFieldCursor = (timerFieldCursor + 1) % 3; beep(900, 20); }
    else if (gameState == ST_WIFI_KEYBOARD) { kbCol = (kbCol + 1) % 10; beep(900, 20); }
    else if (gameState == ST_COIN_KEYBOARD) { kbCol = (kbCol + 1) % 3; beep(900, 20); }
    else if (gameState == ST_SNAKE_GAME && dx == 0) { dx = 1; dy = 0; beep(900, 20); }
    else if (gameState == ST_TETRIS_GAME && !checkTetrisCollision(tetrisX + 1, tetrisY, tetrisCurrRot)) { tetrisX++; beep(800, 20); }
  } else if (!r) rightPressed = false;

  if (canTrigger && s && !selectPressed) {
    selectPressed = true; lastBtnActionMs = now; wakeScreen();

    if (gameState == ST_MAIN_MENU) {
      beep(900, 50);
      if (menuCursor == 0) { gameState = ST_GAME_SELECT; menuCursor = 0; }
      else if (menuCursor == 1) { gameState = ST_ACHIEVEMENTS_MENU; menuCursor = 0; }
      else if (menuCursor == 2) { gameState = ST_INFO_SCREEN; }
      else if (menuCursor == 3) { gameState = ST_SETTINGS_MENU; menuCursor = 0; }
      else if (menuCursor == 4) { syncNtpTime(false); fetchWeatherIfNeeded(false); gameState = ST_CLOCK_MENU; menuCursor = 0; }
      else if (menuCursor == 5) { evaluateCalcExpression(calcExpr); gameState = ST_CALCULATOR; }
      else if (menuCursor == 6) { gameState = ST_RECORDS_SCREEN; }
      else if (menuCursor == 7) { if (noteCursor >= (int)notesList.size()) noteCursor = 0; gameState = ST_NOTES_LIST; }
      else if (menuCursor == 8) { gameState = ST_MUSIC_MENU; menuCursor = 0; }
    } else if (gameState == ST_CLICKER_GAME) {
      performClickerTap(true);
    } else if (gameState == ST_MUSIC_MENU) {
      beep(900, 50);
      if (menuCursor == 0) { if (pianoSongCursor >= (int)songsList.size()) pianoSongCursor = 0; gameState = ST_PIANO_MODE; }
      else if (menuCursor == 1) { metroRunning = true; metroBeat = 0; setMetronomeBpm(metroBpm); gameState = ST_METRONOME; }
      else { gameState = ST_MAIN_MENU; menuCursor = 8; }
    } else if (gameState == ST_RECORDS_SCREEN) {
      gameState = ST_MAIN_MENU; menuCursor = 6; beep(700, 35);
    } else if (gameState == ST_INFO_SCREEN) {
      gameState = ST_MAIN_MENU; menuCursor = 2; beep(700, 35);
    } else if (gameState == ST_ACHIEVEMENTS_MENU) {
      gameState = ST_MAIN_MENU; menuCursor = 1; beep(700, 35);
    } else if (gameState == ST_CREDITS_MENU) {
      gameState = ST_MAIN_MENU; menuCursor = 3; beep(700, 35);
    } else if (gameState == ST_NOTES_LIST) {
      if (!notesList.empty() && noteCursor >= 0 && noteCursor < (int)notesList.size()) { activeNoteIdx = noteCursor; noteScrollLine = 0; gameState = ST_NOTE_VIEW; beep(1050, 40); }
      else { gameState = ST_MAIN_MENU; menuCursor = 7; beep(700, 35); }
    } else if (gameState == ST_NOTE_VIEW) {
      gameState = ST_NOTES_LIST; beep(700, 35);
    } else if (gameState == ST_PIANO_MODE) {
      if (songPlaying) { stopSongPlayback(); beep(500, 35); }
      else if (!songsList.empty()) { if (pianoSongCursor < 0 || pianoSongCursor >= (int)songsList.size()) pianoSongCursor = 0; startSongPlayback(pianoSongCursor); }
      else { stopSongPlayback(); gameState = ST_MUSIC_MENU; menuCursor = 0; beep(700, 35); }
    } else if (gameState == ST_METRONOME) {
      metroRunning = !metroRunning; metroBeat = 0;
      if (metroRunning) setMetronomeBpm(metroBpm); else noTone(BUZZER_PIN);
      beep(1050, 35); markDirty();
    } else if (gameState == ST_RECOVERY_MENU) {
      beep(950, 50);
      if (menuCursor == 0) runUrlOtaUpdate();
      else if (menuCursor == 1) { otaArmed = true; gameState = ST_OTA_WAIT; }
      else if (menuCursor == 2) { resetSaveData(); beep(1250, 120); lastWebStatus = "Сохранения сброшены"; markDirty(); }
      else if (menuCursor == 3) gameState = ST_INFO_SCREEN;
    } else if (gameState == ST_CALCULATOR) {
      appendCalcChar(CALC_CHARS[calcCharIdx]); beep(1050, 25);
    } else if (gameState == ST_CLOCK_MENU) {
      beep(900, 50);
      if (menuCursor == 0) { syncNtpTime(false); fetchWeatherIfNeeded(false); gameState = ST_NET_CLOCK; }
      else if (menuCursor == 1) gameState = ST_STOPWATCH;
      else if (menuCursor == 2) { if (timerRunning || timerRemainingMs > 0) gameState = ST_TIMER_RUNNING; else { timerFieldCursor = 0; gameState = ST_TIMER_SET; } }
      else { gameState = ST_MAIN_MENU; menuCursor = 4; }
    } else if (gameState == ST_NET_CLOCK) {
      syncNtpTime(true); fetchWeatherIfNeeded(true); beep(1000, 40);
    } else if (gameState == ST_STOPWATCH) {
      if (stopwatchRunning) { stopwatchElapsed += (now - stopwatchStart); stopwatchRunning = false; }
      else { stopwatchStart = now; stopwatchRunning = true; }
      beep(1000, 35);
    } else if (gameState == ST_TIMER_SET) {
      unsigned long totalMs = ((unsigned long)timerHoursSet * 3600UL + (unsigned long)timerMinutesSet * 60UL + (unsigned long)timerSecondsSet) * 1000UL;
      if (totalMs > 0) { timerRemainingMs = totalMs; timerTargetMs = now + totalMs; timerRunning = true; beep(1100, 80); gameState = ST_TIMER_RUNNING; }
      else beep(300, 80);
    } else if (gameState == ST_TIMER_RUNNING) {
      if (timerRunning) { timerRemainingMs = ((long)(now - timerTargetMs) >= 0) ? 0 : (timerTargetMs - now); timerRunning = false; }
      else if (timerRemainingMs > 0) { timerTargetMs = now + timerRemainingMs; timerRunning = true; }
      beep(1000, 35);
    } else if (gameState == ST_GAME_SELECT) {
      beep(900, 50);
      if (menuCursor == 0) gameState = ST_SLOT_GAME;
      else if (menuCursor == 1) startSnake();
      else if (menuCursor == 2) startTetris();
      else if (menuCursor == 3) { gameState = ST_BOTTLE_GAME; bottleState = 0; bottleLanded = false; bottleScore = 0; }
      else if (menuCursor == 4) startFlappy();
      else if (menuCursor == 5) startPong();
      else if (menuCursor == 6) startDino();
      else if (menuCursor == 7) startDoodle();
      else if (menuCursor == 8) gameState = ST_CLICKER_GAME;
      else { gameState = ST_MAIN_MENU; menuCursor = 0; }
    } else if (gameState == ST_BOTTLE_GAME) {
      if (bottleState == 0) { bottleState = 1; bottleLanded = false; bottleAnimTime = now; beep(760, 45); markDirty(); }
      else if (bottleState == 2) { bottleState = 0; bottleLanded = false; beep(650, 25); markDirty(); }
    } else if (gameState == ST_FLAPPY_GAME) { flappyVelocity = FLAPPY_JUMP; beep(640, 25); }
    else if (gameState == ST_FLAPPY_OVER) { startFlappy(); beep(800, 30); }
    else if (gameState == ST_DINO_GAME) { if (!dinoJumping) { dinoJumping = true; dinoVY = DINO_JUMP_V; beep(980, 30); } }
    else if (gameState == ST_DINO_OVER) { startDino(); beep(800, 30); }
    else if (gameState == ST_SNAKE_OVER) { startSnake(); beep(800, 30); }
    else if (gameState == ST_TETRIS_OVER) { startTetris(); beep(800, 30); }
    else if (gameState == ST_PONG_OVER) { startPong(); beep(800, 30); }
    else if (gameState == ST_DOODLE_OVER) { startDoodle(); beep(800, 30); }
    else if (gameState == ST_COIN_KEYBOARD) {
      beep(1100, 40);
      char ch = NUM_KB_LAYOUT[kbRow][kbCol];
      int len = strlen(inputCoinStr);
      if (ch == 'O') {
        long val = atol(inputCoinStr);
        if (val > 0 && !godMode) { coins += val; if (coins > MAX_COINS) coins = MAX_COINS; saveGameData(); markDirty(); }
        inputCoinStr[0] = '\0'; gameState = ST_SETTINGS_MENU; menuCursor = 2;
      } else if (ch == '<') { if (len > 0) inputCoinStr[len - 1] = '\0'; }
      else if (ch == 'X') { inputCoinStr[0] = '\0'; gameState = ST_SETTINGS_MENU; menuCursor = 2; }
      else if (len < 6 && ch >= '0' && ch <= '9') { inputCoinStr[len] = ch; inputCoinStr[len + 1] = '\0'; }
    } else if (gameState == ST_SETTINGS_MENU) {
      beep(1000, 50);
      if (menuCursor == 0) { gameState = ST_WIFI_MANAGEMENT; menuCursor = 0; }
      else if (menuCursor == 1) { soundEnabled = !soundEnabled; if (!soundEnabled) { noTone(BUZZER_PIN); stopSongPlayback(); stopMetronome(false); } }
      else if (menuCursor == 2) { inputCoinStr[0] = '\0'; kbRow = 0; kbCol = 0; gameState = ST_COIN_KEYBOARD; }
      else if (menuCursor == 3) gameState = ST_RESET_STEP_1;
      else if (menuCursor == 4) gameState = ST_TEST_CONFIRM;
      else if (menuCursor == 5) runUrlOtaUpdate();
      else { gameState = ST_MAIN_MENU; menuCursor = 3; }
    } else if (gameState == ST_WIFI_MANAGEMENT) {
      beep(1000, 50);
      if (menuCursor == 0 || menuCursor == 3) scanWifiNetworksSafe(true);
      else if (menuCursor == 1) { ipAddressStr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "ОФФЛАЙН"; ignoreNextIpDismiss = true; gameState = ST_WIFI_CONNECTED_INFO; }
      else if (menuCursor == 2) {
        WiFi.disconnect();
        WiFi.setAutoReconnect(false);
        currentSsid[0] = '\0'; currentPass[0] = '\0'; ipAddressStr = "ОФФЛАЙН"; ntpConfigured = false; weatherValid = false;
        saveGameData(); beep(400, 150); markDirty();
      } else if (menuCursor == 4) { gameState = ST_SETTINGS_MENU; menuCursor = 0; }
    } else if (gameState == ST_WIFI_LIST && wifiNetCount > 0) {
      WiFi.SSID(menuCursor).toCharArray(targetSsid, 32); inputPassword[0] = '\0'; kbRow = 0; kbCol = 0; kbPage = 0; gameState = ST_WIFI_KEYBOARD; beep(1000, 50);
    } else if (gameState == ST_WIFI_KEYBOARD) {
      beep(1100, 40);
      if (kbRow == 4 && kbCol == 0) { int len = strlen(inputPassword); if (len > 0) inputPassword[len - 1] = '\0'; }
      else if (kbRow == 4 && kbCol == 1) { int len = strlen(inputPassword); if (len < 31) { inputPassword[len] = ' '; inputPassword[len + 1] = '\0'; } }
      else if (kbRow == 4 && kbCol == 2) { strcpy(currentSsid, targetSsid); strcpy(currentPass, inputPassword); saveGameData(); ntpConfigured = false; wifiConnectAttempts = 0; WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.setTxPower(WIFI_POWER_19_5dBm); WiFi.begin(currentSsid, currentPass); stateTimer = now; gameState = ST_WIFI_CONNECTING; }
      else if (kbRow == 4 && kbCol == 3) { kbPage = (kbPage + 1) % 3; }
      else if (kbRow == 4 && kbCol == 5) gameState = ST_WIFI_LIST;
      else if (!(kbRow == 4 && kbCol == 4)) { char ch = KB_PAGES[kbPage][kbRow][kbCol]; int len = strlen(inputPassword); if (len < 31 && ch != ' ') { inputPassword[len] = ch; inputPassword[len + 1] = '\0'; } }
    } else if (gameState == ST_SLOT_GAME) { startSpin(); }
  } else if (!s) selectPressed = false;
}

void drawHeaderBar(const char* title, const char* badge) {
  u8g2.setFont(u8g2_font_6x12_t_cyrillic);
  u8g2.drawRBox(0, 0, 128, 13, 3);
  u8g2.setDrawColor(0);
  u8g2.drawUTF8(5, 10, title);
  if (badge != nullptr && badge[0] != '\0') {
    int bw = u8g2.getUTF8Width(badge) + 6;
    int bx = 126 - bw;
    if (bx > 10) {
      u8g2.drawRBox(bx, 2, bw, 9, 2);
      u8g2.setDrawColor(1);
      u8g2.drawUTF8(bx + 3, 9, badge);
      u8g2.setDrawColor(0);
    }
  }
  u8g2.setDrawColor(1);
}

void drawFooterHint(const char* leftHint, const char* rightHint) {
  u8g2.drawHLine(0, 53, 128);
  u8g2.setFont(u8g2_font_5x7_tf);
  if (leftHint) u8g2.drawStr(2, 62, leftHint);
  if (rightHint) {
    int w = u8g2.getStrWidth(rightHint);
    u8g2.drawStr(126 - w, 62, rightHint);
  }
}

void drawMenuIcon(int menuType, int idx, int x, int y, bool inverted) {
  u8g2.setDrawColor(inverted ? 0 : 1);
  if (menuType == 0) {
    if (idx == 0) { u8g2.drawFrame(x, y + 1, 7, 5); u8g2.drawPixel(x + 2, y + 3); u8g2.drawPixel(x + 5, y + 3); }
    else if (idx == 1) { u8g2.drawTriangle(x + 3, y, x, y + 6, x + 6, y + 6); }
    else if (idx == 2) { u8g2.drawCircle(x + 3, y + 3, 3); u8g2.drawPixel(x + 3, y + 2); u8g2.drawVLine(x + 3, y + 4, 2); }
    else if (idx == 3) { u8g2.drawFrame(x + 1, y + 1, 5, 5); u8g2.drawPixel(x + 3, y + 3); }
    else if (idx == 4) { u8g2.drawCircle(x + 3, y + 3, 3); u8g2.drawLine(x + 3, y + 3, x + 3, y + 1); u8g2.drawLine(x + 3, y + 3, x + 5, y + 3); }
    else if (idx == 5) { u8g2.drawFrame(x, y, 7, 7); u8g2.drawHLine(x + 2, y + 2, 3); }
    else if (idx == 6) { u8g2.drawBox(x, y + 3, 2, 4); u8g2.drawBox(x + 3, y + 1, 2, 6); u8g2.drawBox(x + 6, y + 2, 2, 5); }
    else if (idx == 7) { u8g2.drawFrame(x + 1, y, 5, 7); u8g2.drawHLine(x + 2, y + 2, 3); u8g2.drawHLine(x + 2, y + 4, 3); }
    else if (idx == 8) { u8g2.drawVLine(x + 2, y + 1, 5); u8g2.drawVLine(x + 5, y, 5); u8g2.drawHLine(x + 2, y + 1, 4); u8g2.drawDisc(x + 1, y + 5, 1); u8g2.drawDisc(x + 4, y + 4, 1); }
  } else if (menuType == 1) {
    if (idx == 0) { u8g2.drawFrame(x, y + 1, 7, 5); u8g2.drawVLine(x + 2, y + 1, 5); u8g2.drawVLine(x + 4, y + 1, 5); }
    else if (idx == 1) { u8g2.drawHLine(x, y + 4, 4); u8g2.drawVLine(x + 3, y + 2, 3); u8g2.drawHLine(x + 3, y + 2, 3); }
    else if (idx == 2) { u8g2.drawBox(x + 2, y + 1, 3, 2); u8g2.drawBox(x, y + 3, 7, 2); }
    else if (idx == 3) { u8g2.drawBox(x + 2, y, 3, 2); u8g2.drawFrame(x + 1, y + 2, 5, 5); }
    else if (idx == 4) { u8g2.drawDisc(x + 3, y + 3, 3); u8g2.drawPixel(x + 6, y + 3); }
    else if (idx == 5) { u8g2.drawVLine(x, y + 1, 5); u8g2.drawBox(x + 4, y + 3, 2, 2); }
    else if (idx == 6) { u8g2.drawBox(x + 3, y + 1, 3, 3); u8g2.drawBox(x + 1, y + 3, 4, 3); }
    else if (idx == 7) { u8g2.drawHLine(x, y + 6, 7); u8g2.drawBox(x + 2, y + 1, 3, 3); }
    else if (idx == 8) { u8g2.drawCircle(x + 3, y + 3, 3); u8g2.drawBox(x + 2, y + 2, 3, 3); }
  }
  u8g2.setDrawColor(1);
}

void drawMenuRow(int idx, int curIdx, int yPos, const char* label, int menuType) {
  u8g2.setFont(u8g2_font_6x12_t_cyrillic);
  bool sel = (idx == curIdx);
  if (sel) {
    u8g2.drawRBox(2, yPos - 1, 119, 10, 3);
    u8g2.setDrawColor(0);
    u8g2.drawUTF8(5, yPos + 7, ">");
    u8g2.drawUTF8(13, yPos + 7, label);
    if (menuType >= 0) drawMenuIcon(menuType, idx, 109, yPos + 1, true);
    u8g2.setDrawColor(1);
  } else {
    u8g2.drawUTF8(9, yPos + 7, label);
    if (menuType >= 0) drawMenuIcon(menuType, idx, 109, yPos + 1, false);
  }
}

void drawScrollIndicator(int curIdx, int totalItems) {
  if (totalItems <= 1) return;
  const int trackTop = 15, trackHeight = 36;
  u8g2.drawVLine(125, trackTop, trackHeight);
  int thumbH = max(5, trackHeight / totalItems);
  int thumbY = trackTop + (curIdx * (trackHeight - thumbH)) / (totalItems - 1);
  u8g2.drawRBox(124, thumbY, 3, thumbH, 1);
}

void drawBottleGraphic(int cx, int cy, float angle) {
  float cosA = cosf(angle), sinA = sinf(angle);
  auto rotPt = [&](float lx, float ly, int &ox, int &oy) {
    ox = cx + (int)roundf(lx * cosA - ly * sinA);
    oy = cy + (int)roundf(lx * sinA + ly * cosA);
  };
  auto rotLine = [&](float x1, float y1, float x2, float y2) {
    int ax, ay, bx, by; rotPt(x1, y1, ax, ay); rotPt(x2, y2, bx, by); u8g2.drawLine(ax, ay, bx, by);
  };
  rotLine(-2.0f, -12.0f, 2.0f, -12.0f); rotLine(-2.0f, -10.0f, 2.0f, -10.0f);
  rotLine(-2.0f, -12.0f, -2.0f, -5.0f); rotLine(2.0f, -12.0f, 2.0f, -5.0f);
  rotLine(-2.0f, -5.0f, -5.0f, -2.0f);  rotLine(2.0f, -5.0f, 5.0f, -2.0f);
  rotLine(-5.0f, -2.0f, -5.0f, 11.0f);  rotLine(5.0f, -2.0f, 5.0f, 11.0f);
  rotLine(-5.0f, 11.0f, 5.0f, 11.0f);   rotLine(-4.0f, 2.0f, 4.0f, 2.0f);
  rotLine(-4.0f, 6.0f, 4.0f, 6.0f);
}

void renderDisplay() {
  unsigned long nowMs = millis();
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);

  if (disabledMode) {
    drawHeaderBar("ОЖИДАНИЕ", "OFF");
    u8g2.drawRFrame(8, 18, 112, 34, 4);
    u8g2.drawUTF8(16, 33, "Приставка отключена");
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(24, 45, "WEB: /disable");
    u8g2.sendBuffer();
    return;
  }

  if (screenSleeping) {
    drawArchLogo((int)archX, (int)archY);
    u8g2.sendBuffer();
    return;
  }

  if (gameState == ST_SPLASH) {
    const unsigned long splashElapsed = nowMs - stateTimer;
    const int pulse = (int)((nowMs / 90) % 6);
    const float angle = (float)nowMs * 0.006f;
    u8g2.drawRFrame(2, 2, 124, 60, 7);
    u8g2.drawRFrame(5, 5, 118, 54, 5);
    u8g2.drawCircle(64, 19, 13);
    u8g2.drawCircle(64, 19, 9);
    u8g2.drawDisc(64, 19, 3 + (pulse % 2));
    u8g2.drawDisc(64 + (int)(cosf(angle) * 15.0f), 19 + (int)(sinf(angle) * 10.0f), 2);
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    const char* brand = "ESP32 CONSOLE";
    u8g2.drawUTF8((128 - u8g2.getUTF8Width(brand)) / 2, 43, brand);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(11, 53, "BOOTING");
    u8g2.drawStr(91, 53, FW_VERSION);
    u8g2.drawRFrame(10, 55, 108, 4, 2);
    int progressW = (int)map((long)constrain((long)splashElapsed, 0L, 2500L), 0L, 2500L, 0L, 104L);
    if (progressW > 0) u8g2.drawRBox(12, 56, progressW, 2, 1);
  } else if (gameState == ST_WEB_MESSAGE) {
    drawHeaderBar("СООБЩЕНИЕ", "WEB");
    u8g2.drawRFrame(4, 16, 120, 34, 4);
    std::vector<String> lines;
    buildNoteLines(webBannerText, lines, 19);
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    for (int i = 0; i < 2 && i < (int)lines.size(); i++) {
      u8g2.drawUTF8(10, 29 + i * 13, lines[i].c_str());
    }
    drawFooterHint("ANY KEY: CLOSE");
  } else if (gameState == ST_CLICKER_GAME) {
    drawHeaderBar("СЧЁТЧИК", "+1");

    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    String cntStr = String(clickerCount);
    int cw = u8g2.getStrWidth(cntStr.c_str());
    u8g2.drawStr((128 - cw) / 2, 24, cntStr.c_str());

    int btnPad = (clickBtnAnim > 0) ? 2 : 0;
    u8g2.drawRFrame(36 + btnPad, 29 + btnPad, 56 - btnPad * 2, 16 - btnPad * 2, 4);
    if (clickBtnAnim > 0) u8g2.drawRBox(38 + btnPad, 31 + btnPad, 52 - btnPad * 2, 12 - btnPad * 2, 3);
    u8g2.setDrawColor(clickBtnAnim > 0 ? 0 : 1);
    u8g2.drawUTF8(46, 41, "+1 [OK]");
    u8g2.setDrawColor(1);

    u8g2.setFont(u8g2_font_5x7_tf);
    for (int i = 0; i < 6; i++) {
      if (clickParticles[i].life > 0) {
        u8g2.drawStr((int)clickParticles[i].x, (int)clickParticles[i].y, "+1");
      }
    }

    drawFooterHint("OK:+1 v:0", "<:EXIT");
  } else if (gameState == ST_RECOVERY_MENU) {
    drawHeaderBar("RECOVERY", FW_VERSION);
    const char* rItems[4] = {"1. OTA по ссылке", "2. OTA из браузера", "3. Сброс сейвов", "4. Инфо"};
    for (int i = 0; i < 4; i++) drawMenuRow(i, menuCursor, 15 + i * 10, rItems[i]);
    drawFooterHint("^v:MOVE", "OK:SELECT");
  } else if (gameState == ST_OTA_WAIT) {
    drawHeaderBar("OTA WEB", "WAIT");
    u8g2.drawUTF8(6, 25, "Адрес в браузере:");
    u8g2.drawRFrame(4, 29, 120, 15, 3);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(9, 39, (WiFi.status() == WL_CONNECTED) ? (String("http://") + WiFi.localIP().toString() + "/update").c_str() : "Wi-Fi offline");
    drawFooterHint("<:BACK");
  } else if (gameState == ST_OTA_UPDATING) {
    drawHeaderBar("ПРОШИВКА", "OTA");
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawStr(50, 29, (String(otaProgressPct) + "%").c_str());
    u8g2.drawRFrame(8, 35, 112, 13, 3);
    int fillW = map(constrain(otaProgressPct, 0, 100), 0, 100, 0, 108);
    if (fillW > 0) u8g2.drawRBox(10, 37, fillW, 9, 2);
  } else if (gameState == ST_OTA_DONE) {
    drawHeaderBar("OTA ГОТОВО", "OK");
    u8g2.drawRFrame(8, 18, 112, 34, 4);
    u8g2.drawUTF8(20, 32, "ПО ОБНОВЛЕНО!");
    u8g2.drawUTF8(18, 46, "Перезагрузка...");
  } else if (gameState == ST_MAIN_MENU) {
    char posBuf[8];
    snprintf(posBuf, sizeof(posBuf), "%d/9", (int)menuCursor + 1);
    drawHeaderBar(godMode ? "МЕНЮ [БОГ]" : "ГЛАВНОЕ МЕНЮ", posBuf);
    const char* items[9] = {
      "1. Играть", "2. Достижения", "3. Информация", "4. Настройки",
      "5. Часы", "6. Калькулятор", "7. Рекорды", "8. Шпаргалки", "9. Музыка"
    };
    int startIdx = constrain(menuCursor - 1, 0, 6);
    for (int i = 0; i < 3 && (startIdx + i) < 9; i++) {
      int idx = startIdx + i;
      drawMenuRow(idx, menuCursor, 16 + i * 12, items[idx], 0);
    }
    drawScrollIndicator(menuCursor, 9);
    drawFooterHint(timerRunning ? "TMR:ON" : (WiFi.status() == WL_CONNECTED ? "WIFI:ON" : "WIFI:OFF"), "OK:OPEN");
  } else if (gameState == ST_MUSIC_MENU) {
    drawHeaderBar("МУЗЫКА", "AUDIO");
    const char* mItems[3] = {"1. Песни", "2. Метроном", "3. Назад"};
    for (int i = 0; i < 3; i++) drawMenuRow(i, menuCursor, 16 + i * 12, mItems[i]);
    drawFooterHint("<:BACK", "OK:OPEN");
  } else if (gameState == ST_RECORDS_SCREEN) {
    drawHeaderBar("РЕКОРДЫ", "TOP");
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 22, (String("SNAKE: ") + snakeRecord + "   TETRIS: " + tetrisRecord).c_str());
    u8g2.drawStr(4, 31, (String("FLAPPY: ") + flappyRecord + "  PONG:   " + pongRecord).c_str());
    u8g2.drawStr(4, 40, (String("DINO: ") + dinoRecord + "    FLIP:   " + bottleRecord).c_str());
    u8g2.drawStr(4, 49, (String("DOODLE: ") + doodleRecord + "  COUNT:  " + clickerCount).c_str());
    drawFooterHint("< / OK: BACK");
  } else if (gameState == ST_PIANO_MODE) {
    drawHeaderBar("ПЛЕЕР ПЕСЕН", songPlaying ? "PLAY" : "LIST");
    if (songsList.empty()) {
      u8g2.drawUTF8(6, 28, "Песен пока нет");
      u8g2.drawUTF8(6, 42, "Добавь в веб-меню");
    } else if (songPlaying && activeSongIdx >= 0 && activeSongIdx < (int)songsList.size()) {
      u8g2.drawUTF8(6, 26, utf8Truncate(songsList[activeSongIdx].title, 18).c_str());
      u8g2.drawRFrame(6, 31, 116, 15, 3);
      u8g2.drawUTF8(12, 42, (String("Шаг: ") + activeSongStepLabel).c_str());
    } else {
      int total = (int)songsList.size(), cur = constrain(pianoSongCursor, 0, total - 1), startIdx = constrain(cur - 1, 0, max(0, total - 2));
      for (int i = 0; i < 2 && (startIdx + i) < total; i++) {
        int idx = startIdx + i;
        drawMenuRow(idx, cur, 17 + i * 12, utf8Truncate(songsList[idx].title, 17).c_str());
      }
    }
    drawFooterHint(songPlaying ? "OK:STOP" : "OK:PLAY", "<:BACK");
  } else if (gameState == ST_METRONOME) {
    char bpmBadge[8]; snprintf(bpmBadge, sizeof(bpmBadge), "%d", metroBpm);
    drawHeaderBar("МЕТРОНОМ", bpmBadge);
    u8g2.drawUTF8(8, 25, (String("Темп: ") + String(metroBpm) + " BPM").c_str());
    for (int b = 0; b < 4; b++) {
      int bx = 12 + b * 27;
      if (metroRunning && metroBeat == b) u8g2.drawRBox(bx, 30, 20, 10, 2);
      else u8g2.drawRFrame(bx, 30, 20, 10, 2);
    }
    drawFooterHint("^v:+-1 >:+10", metroRunning ? "OK:STOP" : "OK:START");
  } else if (gameState == ST_NOTES_LIST) {
    drawHeaderBar("ШПАРГАЛКИ", notesList.empty() ? "0" : (String(noteCursor + 1) + "/" + String(notesList.size())).c_str());
    if (notesList.empty()) {
      u8g2.drawUTF8(8, 28, "Шпаргалок пока нет");
      u8g2.drawUTF8(8, 42, "Добавь в веб-меню");
    } else {
      int total = (int)notesList.size(), startIdx = constrain(noteCursor - 1, 0, max(0, total - 3));
      for (int i = 0; i < 3 && (startIdx + i) < total; i++) {
        int idx = startIdx + i;
        drawMenuRow(idx, noteCursor, 16 + i * 12, utf8Truncate(notesList[idx].topic, 16).c_str());
      }
      drawScrollIndicator(noteCursor, total);
    }
    drawFooterHint("OK:OPEN", "<:BACK");
  } else if (gameState == ST_NOTE_VIEW) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    if (activeNoteIdx < 0 || activeNoteIdx >= (int)notesList.size()) {
      drawHeaderBar("ШПАРГАЛКА");
      u8g2.drawUTF8(8, 32, "Тема не найдена");
    } else {
      drawHeaderBar(utf8Truncate(notesList[activeNoteIdx].topic, 18).c_str());
      std::vector<String> wrapped; buildNoteLines(notesList[activeNoteIdx].body, wrapped, 20);
      const int visibleRows = 4;
      noteScrollLine = constrain(noteScrollLine, 0, max(0, (int)wrapped.size() - visibleRows));
      for (int i = 0; i < visibleRows && (noteScrollLine + i) < (int)wrapped.size(); i++) {
        u8g2.drawUTF8(6, 24 + (i * 9), wrapped[noteScrollLine + i].c_str());
      }
    }
    drawFooterHint("^v:SCROLL", "<:BACK");
  } else if (gameState == ST_CALCULATOR) {
    char symBadge[6]; snprintf(symBadge, sizeof(symBadge), "[%c]", CALC_CHARS[calcCharIdx]);
    drawHeaderBar("КАЛЬКУЛЯТОР", symBadge);
    String exprDisp = (strlen(calcExpr) > 0) ? String(calcExpr) : String("(пусто)");
    if (exprDisp.length() > 19) exprDisp = exprDisp.substring(exprDisp.length() - 19);
    u8g2.drawUTF8(6, 26, exprDisp.c_str());
    if (strlen(calcExpr) == 0) u8g2.drawStr(6, 42, "= 0");
    else if (calcError) u8g2.drawUTF8(6, 42, (String("! ") + calcErrMsg).c_str());
    else if (calcHasResult) {
      long asLong = (long)calcResult;
      String resStr = (fabs(calcResult - (double)asLong) < 0.0001 && fabs(calcResult) < 1e9) ? ("= " + String(asLong)) : ("= " + String(calcResult, 3));
      u8g2.drawStr(6, 42, resStr.c_str());
    }
    drawFooterHint("^v:CHAR >:ADD", "<:DEL");
  } else if (gameState == ST_GAME_SELECT) {
    char posBuf[8]; snprintf(posBuf, sizeof(posBuf), "%d/10", (int)menuCursor + 1);
    drawHeaderBar("ВЫБОР ИГРЫ", posBuf);
    const char* gItems[10] = {
      "1. Слоты", "2. Змейка", "3. Тетрис", "4. Баттл Флип",
      "5. Flappy Bird", "6. Понг", "7. Динозаврик", "8. Doodle Jump", "9. Счётчик", "10. Назад"
    };
    int startIdx = constrain(menuCursor - 1, 0, 7);
    for (int i = 0; i < 3 && (startIdx + i) < 10; i++) {
      int idx = startIdx + i;
      drawMenuRow(idx, menuCursor, 16 + i * 12, gItems[idx], 1);
    }
    drawScrollIndicator(menuCursor, 10);
    drawFooterHint("<:BACK", "OK:PLAY");
  } else if (gameState == ST_DOODLE_GAME) {
    u8g2.drawLine(0, 10, 128, 10);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 8, (String("SCORE: ") + String(doodleScore)).c_str());
    u8g2.drawStr(74, 8, (String("BEST: ") + String(doodleRecord)).c_str());
    for (int i = 0; i < DOODLE_PLAT_COUNT; i++) {
      int px = (int)doodlePlats[i].x, py = (int)doodlePlats[i].y;
      if (py >= 11 && py <= 63) u8g2.drawRBox(px, py, doodlePlats[i].w, 3, 1);
    }
    int dxPos = (int)doodleX, dyPos = (int)doodleY;
    u8g2.drawRBox(dxPos + 1, dyPos, 5, 5, 1); u8g2.drawBox(dxPos, dyPos + 5, 7, 2);
    u8g2.drawVLine(dxPos + 1, dyPos + 7, 2); u8g2.drawVLine(dxPos + 5, dyPos + 7, 2);
  } else if (gameState == ST_DOODLE_OVER) {
    drawHeaderBar("DOODLE OVER");
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(22, 27, (String("Счёт: ") + String(doodleScore)).c_str());
    u8g2.drawUTF8(22, 41, (String("Рекорд: ") + String(doodleRecord)).c_str());
    drawFooterHint("<:MENU", "OK:RETRY");
  } else if (gameState == ST_DINO_GAME) {
    u8g2.drawLine(0, 10, 128, 10); u8g2.drawLine(0, DINO_GROUND_Y, 128, DINO_GROUND_Y);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 8, (String("SCORE: ") + String(dinoScore)).c_str());
    u8g2.drawStr(74, 8, (String("BEST: ") + String(dinoRecord)).c_str());
    int dy = (int)dinoY;
    if (dinoDucking) { u8g2.drawRBox(DINO_X, dy + 1, 10, 5, 1); u8g2.drawVLine(DINO_X + 2, dy + 6, 2); u8g2.drawVLine(DINO_X + 7, dy + 6, 2); }
    else { u8g2.drawRBox(DINO_X + 2, dy, 6, 4, 1); u8g2.drawBox(DINO_X + 1, dy + 4, 6, 5); u8g2.drawPixel(DINO_X, dy + 7); u8g2.drawVLine(DINO_X + 2, dy + 9, 3); u8g2.drawVLine(DINO_X + 5, dy + 9, 3); }
    int cx = (int)cactusX, cyTop = DINO_GROUND_Y - cactusH;
    u8g2.drawBox(cx + 1, cyTop, max(2, cactusW - 2), cactusH);
    if (cactusH >= 10) { u8g2.drawHLine(cx, cyTop + 4, cactusW); u8g2.drawVLine(cx, cyTop + 2, 3); u8g2.drawVLine(cx + cactusW - 1, cyTop + 3, 2); }
  } else if (gameState == ST_DINO_OVER) {
    drawHeaderBar("GAME OVER");
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(22, 27, (String("Счёт: ") + String(dinoScore)).c_str());
    u8g2.drawUTF8(22, 41, (String("Рекорд: ") + String(dinoRecord)).c_str());
    drawFooterHint("<:MENU", "OK:RETRY");
  } else if (gameState == ST_PONG_GAME) {
    u8g2.drawLine(0, 10, 128, 10);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 8, (String("SCORE: ") + String(pongScore)).c_str());
    u8g2.drawStr(74, 8, (String("BEST: ") + String(pongRecord)).c_str());
    u8g2.drawRBox((int)paddleX, 58, PADDLE_W, PADDLE_H, 2);
    u8g2.drawDisc((int)ballX + 1, (int)ballY + 1, 2);
  } else if (gameState == ST_PONG_OVER) {
    drawHeaderBar("PONG OVER");
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(22, 27, (String("Счёт: ") + String(pongScore)).c_str());
    u8g2.drawUTF8(22, 41, (String("Рекорд: ") + String(pongRecord)).c_str());
    drawFooterHint("<:MENU", "OK:RETRY");
  } else if (gameState == ST_FLAPPY_GAME) {
    u8g2.drawLine(0, 10, 128, 10);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 8, (String("SCORE: ") + String(flappyScore)).c_str());
    u8g2.drawStr(74, 8, (String("BEST: ") + String(flappyRecord)).c_str());
    int px = (int)pipeX;
    if (px < 128 && px + PIPE_W > 0) {
      int drawX = max(0, px), drawW = min(128, px + PIPE_W) - drawX;
      if (pipeGapY > 11) u8g2.drawBox(drawX, 11, drawW, pipeGapY - 11);
      int botY = pipeGapY + PIPE_GAP_H;
      if (botY < 64) u8g2.drawBox(drawX, botY, drawW, 64 - botY);
    }
    int by = (int)flappyY;
    u8g2.drawRBox(30, by, 7, 5, 2); u8g2.drawBox(35, by + 1, 2, 2); u8g2.drawPixel(32, by + 1);
  } else if (gameState == ST_FLAPPY_OVER) {
    drawHeaderBar("FLAPPY OVER");
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(22, 27, (String("Счёт: ") + String(flappyScore)).c_str());
    u8g2.drawUTF8(22, 41, (String("Рекорд: ") + String(flappyRecord)).c_str());
    drawFooterHint("<:MENU", "OK:RETRY");
  } else if (gameState == ST_BOTTLE_GAME) {
    drawHeaderBar("БАТТЛ ФЛИП", (String(bottleScore) + "/" + String(bottleRecord)).c_str());
    u8g2.drawHLine(16, 48, 96);
    float ang = 0.0f;
    if (bottleState == 1) {
      unsigned long el = nowMs - bottleAnimTime;
      if (el < 850) { ang = (float)el * 0.018f; drawBottleGraphic(64, 34 - (int)(sinf((float)el / 850.0f * 3.14159f) * 14.0f), ang); }
      else { bottleState = 2; bottleAnimTime = nowMs; bottleLanded = (random(0, 100) < 42); if (bottleLanded) { bottleScore++; if (bottleScore > bottleRecord) { bottleRecord = bottleScore; if (!godMode) saveGameData(); } beep(1350, 90); } else { bottleScore = 0; beep(260, 120); } ang = bottleLanded ? 0.0f : 1.57f; drawBottleGraphic(64, 36, ang); }
    } else { ang = bottleLanded ? 0.0f : 1.57f; drawBottleGraphic(64, 36, ang); }
    drawFooterHint("OK:FLIP", "<:EXIT");
  } else if (gameState == ST_SNAKE_GAME) {
    u8g2.drawLine(0, 10, 128, 10);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 8, (String("SCORE: ") + String(snakeScore)).c_str());
    u8g2.drawStr(74, 8, (String("BEST: ") + String(snakeRecord)).c_str());
    for (int i = 0; i < snakeLen; i++) u8g2.drawBox(snake[i].x * 8, 11 + snake[i].y * 6, 7, 5);
    u8g2.drawDisc(food.x * 8 + 3, 11 + food.y * 6 + 2, 2);
  } else if (gameState == ST_SNAKE_OVER) {
    drawHeaderBar("SNAKE OVER");
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(22, 27, (String("Счёт: ") + String(snakeScore)).c_str());
    u8g2.drawUTF8(22, 41, (String("Рекорд: ") + String(snakeRecord)).c_str());
    drawFooterHint("<:MENU", "OK:RETRY");
  } else if (gameState == ST_TETRIS_GAME) {
    // Поле 10x20 с cell=2 и boardY=13: 13 + 20*2 = 53 px — влезает целиком вниз!
    u8g2.drawLine(0, 10, 128, 10);
    const int cell = 2, boardX = 12, boardY = 13;
    u8g2.drawFrame(boardX - 1, boardY - 1, TETRIS_W * cell + 2, TETRIS_H * cell + 2);
    for (int r = 0; r < TETRIS_H; r++) for (int c = 0; c < TETRIS_W; c++) if (tetrisBoard[r][c]) u8g2.drawBox(boardX + c * cell, boardY + r * cell, cell, cell);
    byte curMat[4][4]; getRotatedShape(tetrisCurrShape, tetrisCurrRot, curMat);
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) if (curMat[r][c]) {
      int bx = tetrisX + c, by = tetrisY + r;
      if (by >= 0 && by < TETRIS_H && bx >= 0 && bx < TETRIS_W) u8g2.drawBox(boardX + bx * cell, boardY + by * cell, cell, cell);
    }
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(40, 22, (String("SCORE: ") + String(tetrisScore)).c_str());
    u8g2.drawStr(40, 33, (String("BEST:  ") + String(tetrisRecord)).c_str());
    u8g2.drawStr(40, 44, (String("LVL:   ") + String(tetrisLevel)).c_str());
    drawFooterHint("^:ROT <:EXIT", "v:DOWN");
  } else if (gameState == ST_TETRIS_OVER) {
    drawHeaderBar("TETRIS OVER");
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(22, 27, (String("Счёт: ") + String(tetrisScore)).c_str());
    u8g2.drawUTF8(22, 41, (String("Рекорд: ") + String(tetrisRecord)).c_str());
    drawFooterHint("<:MENU", "OK:RETRY");
  } else if (gameState == ST_CLOCK_MENU) {
    drawHeaderBar("ЧАСЫ", timerRunning ? "TMR" : "TIME");
    const char* cItems[4] = {"1. Время и погода", "2. Секундомер", "3. Таймер", "4. Назад"};
    for (int i = 0; i < 4; i++) drawMenuRow(i, menuCursor, 15 + i * 10, cItems[i]);
    drawFooterHint("<:BACK", "OK:OPEN");
  } else if (gameState == ST_NET_CLOCK) {
    drawHeaderBar("ЧАСЫ", (WiFi.status() == WL_CONNECTED) ? "NTP" : "OFF");
    if (WiFi.status() != WL_CONNECTED) { u8g2.drawUTF8(8, 28, "Нет Wi-Fi"); u8g2.drawUTF8(8, 42, "Подключи в настройках"); }
    else if (!cachedTimeValid) { u8g2.drawUTF8(8, 28, "Синхронизация..."); u8g2.drawUTF8(8, 42, "OK: обновить"); }
    else {
      u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawStr(14, 24, cachedTimeBuf);
      u8g2.setFont(u8g2_font_5x7_tf); u8g2.drawStr(14, 34, cachedDateBuf);
      if (weatherValid) u8g2.drawUTF8(14, 46, (String("Погода: ") + String(weatherTempC, 1) + "C, " + weatherCondition).c_str());
      else u8g2.drawUTF8(14, 46, "Погода: загрузка...");
    }
    drawFooterHint("OK:SYNC", "<:BACK");
  } else if (gameState == ST_STOPWATCH) {
    drawHeaderBar("СЕКУНДОМЕР", stopwatchRunning ? "RUN" : "PAUSE");
    unsigned long ms = getCurrentStopwatchMs(), totalSec = ms / 1000, tenths = (ms % 1000) / 100;
    char swBuf[20]; snprintf(swBuf, sizeof(swBuf), "%02lu:%02lu.%lu", (totalSec / 60) % 60, totalSec % 60, tenths);
    u8g2.drawStr(22, 34, swBuf);
    drawFooterHint(stopwatchRunning ? "OK:PAUSE" : "OK:START", "v:RESET");
  } else if (gameState == ST_TIMER_SET) {
    drawHeaderBar("ТАЙМЕР", "SET");
    char tBuf[20]; snprintf(tBuf, sizeof(tBuf), "%02d : %02d : %02d", timerHoursSet, timerMinutesSet, timerSecondsSet);
    u8g2.drawStr(22, 32, tBuf);
    int curX = 22 + timerFieldCursor * 30;
    u8g2.drawHLine(curX, 35, 12);
    drawFooterHint("^v:ADJ >:NEXT", "OK:START");
  } else if (gameState == ST_TIMER_RUNNING) {
    drawHeaderBar("ТАЙМЕР", timerRunning ? "RUN" : "PAUSE");
    unsigned long remSec = (getCurrentTimerRemainingMs() + 999) / 1000;
    char rBuf[20]; snprintf(rBuf, sizeof(rBuf), "%02lu : %02lu : %02lu", (remSec / 3600) % 100, (remSec % 3600) / 60, remSec % 60);
    u8g2.drawStr(22, 33, rBuf);
    drawFooterHint("<:BG EXIT", "OK:P/R v:0");
  } else if (gameState == ST_TIMER_DONE) {
    drawHeaderBar("ТАЙМЕР!", "DONE");
    u8g2.drawRFrame(6, 17, 116, 32, 4);
    u8g2.drawUTF8(26, 36, "ВРЕМЯ ВЫШЛО!");
    drawFooterHint("ANY KEY: OK");
  } else if (gameState == ST_TEST_CONFIRM) {
    drawHeaderBar("ТЕСТ", "CONFIRM");
    u8g2.drawUTF8(10, 27, "Запустить тест?");
    drawFooterHint("^:NO", "OK:YES");
  } else if (gameState == ST_TEST_MELODY) {
    drawHeaderBar("ТЕСТ", "SOUND");
    u8g2.drawUTF8(14, 34, "Jingle Bells...");
  } else if (gameState == ST_TEST_MODE) {
    drawHeaderBar("ТЕСТ РЕЖИМ", "KEYS");
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(8, 25, "U D L R OK - TEST");
    u8g2.drawStr(8, 37, "HOLD ^ + OK TO EXIT");
  } else if (gameState == ST_ACHIEVEMENTS_MENU) {
    drawHeaderBar("АЧИВКИ", (String(menuCursor + 1) + "/" + String(ACH_COUNT)).c_str());
    const Achievement& a = achievements[menuCursor];
    u8g2.drawUTF8(8, 25, (String(a.unlocked ? "[+] " : "[ ] ") + a.title).c_str());
    u8g2.drawUTF8(8, 39, a.desc);
    drawFooterHint("^v:LIST", "< / OK: BACK");
  } else if (gameState == ST_INFO_SCREEN) {
    drawHeaderBar("ИНФО", FW_VERSION);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 23, (String("VER: ") + FW_VERSION + (godMode ? " [GOD]" : "")).c_str());
    u8g2.drawStr(4, 32, (String("IP:  ") + ipAddressStr).c_str());
    u8g2.drawStr(4, 41, (String("FS:  ") + fsInitMsg + " (" + String(notesList.size()) + "n/" + String(songsList.size()) + "s)").c_str());
    drawFooterHint("< / OK: BACK");
  } else if (gameState == ST_CREDITS_MENU) {
    drawHeaderBar("АВТОР", "INFO");
    u8g2.drawUTF8(12, 31, "ESP32 Mini Console");
    u8g2.drawUTF8(12, 44, FW_VERSION);
    drawFooterHint("< / OK: BACK");
  } else if (gameState == ST_SETTINGS_MENU) {
    drawHeaderBar("НАСТРОЙКИ", "SYS");
    const char* sItems[7] = {
      "1. Wi-Fi сеть", soundEnabled ? "2. Звук: ВКЛ" : "2. Звук: ВЫКЛ",
      "3. +Монеты", "4. Сброс сейвов", "5. Тест", "6. OTA Update", "7. Назад"
    };
    int startIdx = constrain(menuCursor - 1, 0, 4);
    for (int i = 0; i < 3 && (startIdx + i) < 7; i++) {
      int idx = startIdx + i;
      drawMenuRow(idx, menuCursor, 16 + i * 12, sItems[idx]);
    }
    drawScrollIndicator(menuCursor, 7);
    drawFooterHint("<:BACK", "OK:SELECT");
  } else if (gameState == ST_RESET_STEP_1 || gameState == ST_RESET_STEP_2 || gameState == ST_RESET_STEP_3) {
    drawHeaderBar("СБРОС СЕЙВОВ", (gameState == ST_RESET_STEP_1) ? "1/3" : (gameState == ST_RESET_STEP_2) ? "2/3" : "3/3");
    u8g2.drawUTF8(10, 29, "Точно сбросить всё?");
    drawFooterHint("v / <: CANCEL", "^:CONFIRM");
  } else if (gameState == ST_WIFI_MANAGEMENT) {
    drawHeaderBar("WI-FI", (WiFi.status() == WL_CONNECTED) ? "ON" : "OFF");
    const char* wItems[5] = {
      "1. Выбрать сеть", "2. Показать IP", "3. Отключить Wi-Fi", "4. Сканировать", "5. Назад"
    };
    int startIdx = constrain(menuCursor - 1, 0, 2);
    for (int i = 0; i < 3 && (startIdx + i) < 5; i++) {
      int idx = startIdx + i;
      drawMenuRow(idx, menuCursor, 16 + i * 12, wItems[idx]);
    }
    drawFooterHint("<:BACK", "OK:SELECT");
  } else if (gameState == ST_WIFI_CONNECTED_INFO) {
    drawHeaderBar("WI-FI OK", "IP");
    u8g2.drawUTF8(8, 25, "Подключено к:");
    u8g2.drawUTF8(8, 36, utf8Truncate(String(currentSsid), 18).c_str());
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(8, 47, (String("IP: ") + ipAddressStr).c_str());
    drawFooterHint("ANY KEY: OK");
  } else if (gameState == ST_WIFI_SCAN) {
    drawHeaderBar("WI-FI", "SCAN");
    u8g2.drawUTF8(14, 34, "Поиск сетей...");
  } else if (gameState == ST_WIFI_LIST) {
    drawHeaderBar("СЕТИ WI-FI", String(wifiNetCount).c_str());
    if (wifiNetCount <= 0) u8g2.drawUTF8(12, 34, "Сети не найдены");
    else {
      int startIdx = constrain(menuCursor - 1, 0, max(0, wifiNetCount - 3));
      for (int i = 0; i < 3 && (startIdx + i) < wifiNetCount; i++) {
        int idx = startIdx + i;
        drawMenuRow(idx, menuCursor, 16 + i * 12, utf8Truncate(WiFi.SSID(idx), 16).c_str());
      }
    }
    drawFooterHint("<:BACK", "OK:PASS");
  } else if (gameState == ST_WIFI_KEYBOARD) {
    drawHeaderBar("ПАРОЛЬ", (kbPage == 0) ? "abc" : (kbPage == 1) ? "ABC" : "#$!");
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 21, (String(inputPassword) + "_").c_str());
    for (int r = 0; r < 5; r++) {
      for (int c = 0; c < 10; c++) {
        char ch = (r == 4) ? ((c == 0) ? '<' : (c == 1) ? '_' : (c == 2) ? 'O' : (c == 3) ? '^' : (c == 5) ? 'X' : ' ') : KB_PAGES[kbPage][r][c];
        if (ch == ' ') continue;
        int kx = 4 + c * 12, ky = 30 + r * 6;
        if (r == kbRow && c == kbCol) u8g2.drawBox(kx - 1, ky - 5, 8, 7);
        u8g2.setDrawColor((r == kbRow && c == kbCol) ? 0 : 1);
        char buf[2] = {ch, '\0'};
        u8g2.drawStr(kx, ky, buf);
        u8g2.setDrawColor(1);
      }
    }
  } else if (gameState == ST_WIFI_CONNECTING) {
    drawHeaderBar("WI-FI", "CONN");
    u8g2.drawUTF8(12, 32, "Подключение...");
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(12, 44, utf8Truncate(String(currentSsid), 20).c_str());
  } else if (gameState == ST_WIFI_ERROR) {
    drawHeaderBar("WI-FI", "ERR");
    u8g2.drawUTF8(12, 34, "Ошибка сети!");
  } else if (gameState == ST_COIN_KEYBOARD) {
    drawHeaderBar("+МОНЕТЫ", "NUM");
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(8, 22, (String("ADD: ") + inputCoinStr + "_").c_str());
    for (int r = 0; r < 5; r++) {
      for (int c = 0; c < 3; c++) {
        char ch = NUM_KB_LAYOUT[r][c];
        if (ch == ' ') continue;
        int kx = 14 + c * 18, ky = 32 + r * 6;
        if (r == kbRow && c == kbCol) u8g2.drawBox(kx - 1, ky - 5, 10, 7);
        u8g2.setDrawColor((r == kbRow && c == kbCol) ? 0 : 1);
        char buf[2] = {ch, '\0'};
        u8g2.drawStr(kx, ky, buf);
        u8g2.setDrawColor(1);
      }
    }
  } else if (gameState == ST_WIN_ANIM) {
    drawHeaderBar("ПОБЕДА!", "WIN");
    u8g2.drawRFrame(8, 17, 112, 32, 4);
    u8g2.drawUTF8(16, 31, "Выигрыш монет:");
    u8g2.drawUTF8(16, 44, (String("+") + String(currentWin)).c_str());
  } else if (gameState == ST_LOSE_ANIM) {
    drawHeaderBar("МИМО", "LOSE");
    u8g2.drawRFrame(14, 20, 100, 26, 4);
    u8g2.drawUTF8(26, 37, "Попробуй ещё");
  } else if (gameState == ST_NO_MONEY) {
    drawHeaderBar("МАЛО МОНЕТ", "0");
    u8g2.drawRFrame(8, 18, 112, 32, 4);
    u8g2.drawUTF8(14, 37, "Пополни в веб-меню");
  } else {
    // Слоты и спиннинг
    drawHeaderBar(godMode ? "СЛОТЫ [БОГ]" : "СЛОТЫ", "10");
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 21, godMode ? "COINS: INF" : (String("COINS: ") + String(displayCoins)).c_str());
    u8g2.drawStr(76, 21, (String("SPINS: ") + String(totalSpins)).c_str());
    const int rx[3] = {16, 52, 88};
    const int8_t curS[3] = {slot1, slot2, slot3};
    const float bnc[3] = {bounce1, bounce2, bounce3};
    for (int i = 0; i < 3; i++) {
      int yOff = (int)roundf(bnc[i]);
      u8g2.drawRFrame(rx[i], 25 + yOff, 24, 22, 3);
      u8g2.setFont(u8g2_font_6x12_t_cyrillic);
      const char* lbl = (curS[i] == 0) ? "APL" : (curS[i] == 1) ? "777" : (curS[i] == 2) ? "MON" : (curS[i] == 3) ? "GRP" : (curS[i] == 4) ? "X" : "RKT";
      int w = u8g2.getUTF8Width(lbl);
      u8g2.drawStr(rx[i] + (24 - w) / 2, 40 + yOff, lbl);
    }
    drawFooterHint(autoSpin ? "AUTO:ON" : "OK:SPIN", "<:EXIT");
  }

  if (activeAchPopup >= 0 && activeAchPopup < ACH_COUNT) {
    if ((unsigned long)(nowMs - achPopupTimer) < 2200) {
      u8g2.drawRBox(10, 18, 108, 28, 4);
      u8g2.setDrawColor(0);
      u8g2.setFont(u8g2_font_6x12_t_cyrillic);
      u8g2.drawUTF8(16, 30, "АЧИВКА!");
      u8g2.drawUTF8(16, 42, achievements[activeAchPopup].title);
      u8g2.setDrawColor(1);
    } else {
      activeAchPopup = -1;
    }
  }

  u8g2.sendBuffer();
}

void beep(int freq, int duration) {
  if (!soundEnabled) return;
  tone(BUZZER_PIN, freq, duration);
}

void startToneSeq(const ToneStep* seq, uint8_t len, bool returnToTest) {
  if (!soundEnabled || !seq || len == 0) {
    activeSeq = nullptr; activeSeqLen = 0; activeSeqIdx = 0;
    if (returnToTest) { testMelodyStarted = false; gameState = ST_TEST_MODE; testExitLockUntil = millis() + 250; }
    return;
  }
  activeSeq = seq; activeSeqLen = len; activeSeqIdx = 0; returnToTestModeAfterSeq = returnToTest;
  noTone(BUZZER_PIN); tone(BUZZER_PIN, seq[0].freq, seq[0].dur);
  nextToneAtMs = millis() + (unsigned long)seq[0].dur + (unsigned long)seq[0].gap;
}

void updateToneSeq(unsigned long now) {
  if (!activeSeq || activeSeqIdx >= activeSeqLen) return;
  if ((long)(now - nextToneAtMs) < 0) return;
  activeSeqIdx++;
  if (activeSeqIdx >= activeSeqLen) {
    bool goTest = returnToTestModeAfterSeq;
    activeSeq = nullptr; activeSeqLen = 0; activeSeqIdx = 0; returnToTestModeAfterSeq = false;
    noTone(BUZZER_PIN);
    if (goTest) { testMelodyStarted = false; gameState = ST_TEST_MODE; testExitLockUntil = now + 250; markDirty(); }
    return;
  }
  noTone(BUZZER_PIN);
  tone(BUZZER_PIN, activeSeq[activeSeqIdx].freq, activeSeq[activeSeqIdx].dur);
  nextToneAtMs = now + (unsigned long)activeSeq[activeSeqIdx].dur + (unsigned long)activeSeq[activeSeqIdx].gap;
}

void playWinSound()    { startToneSeq(WIN_SEQ, sizeof(WIN_SEQ) / sizeof(WIN_SEQ[0]), false); }
void playLoseSound()   { startToneSeq(LOSE_SEQ, sizeof(LOSE_SEQ) / sizeof(LOSE_SEQ[0]), false); }
void playJackpotSound(){ startToneSeq(JACKPOT_SEQ, sizeof(JACKPOT_SEQ) / sizeof(JACKPOT_SEQ[0]), false); }
void playJingleBells() { startToneSeq(JINGLE_SEQ, sizeof(JINGLE_SEQ) / sizeof(JINGLE_SEQ[0]), true); }
