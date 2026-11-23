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

#define EEPROM_SIZE 140
const char* FW_VERSION = "v2.2.9";
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

const uint8_t ICON_COUNT = 5;
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

const uint8_t ADDR_COINS        = 0;
const uint8_t ADDR_SPINS        = 4;
const uint8_t ADDR_MAX_WIN      = 8;
const uint8_t ADDR_FLAG         = 12;
const uint8_t ADDR_SSID         = 16;
const uint8_t ADDR_PASS         = 48;
const uint8_t ADDR_FLAPPY_REC   = 80;
const uint8_t ADDR_TETRIS_REC   = 84;
const uint8_t ADDR_SNAKE_REC    = 88;
const uint8_t ADDR_BOTTLE_REC   = 92;
const uint8_t ADDR_ACHIEVEMENTS = 96;
const uint8_t ADDR_PONG_REC     = 100;
const uint8_t ADDR_DINO_REC     = 104;
const uint8_t ADDR_DOODLE_REC   = 108;

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

int kbRow = 0;
int kbCol = 0;
char inputPassword[33] = "";
char inputCoinStr[10]  = "";
char targetSsid[32]    = "";
char currentSsid[32]   = "";
char currentPass[32]   = "";
String ipAddressStr    = "ОФФЛАЙН";
String lastWebStatus   = "Готово к работе";
bool ntpConfigured     = false;

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

const char KB_LAYOUT[5][10] = {
  {'a','b','c','d','e','f','g','h','i','j'},
  {'k','l','m','n','o','p','q','r','s','t'},
  {'u','v','w','x','y','z','0','1','2','3'},
  {'4','5','6','7','8','9','-','_','.','@'},
  {'<',' ','O',' ',' ','X',' ',' ',' ',' '}
};

const char NUM_KB_LAYOUT[5][3] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'0', '<', 'X'},
  {'O', ' ', ' '}
};

const uint8_t ACH_COUNT = 6;
Achievement achievements[ACH_COUNT] = {
  {"Новичок",    "Первый спин",       false},
  {"Лудоман",    "50 спинов",         false},
  {"Джекпот!",   "Три главных",       false},
  {"Магнат",     "1000+ монет",       false},
  {"Змеелов",    "50 очков в Змейке", false},
  {"Архитектор", "500 очков Тетрис",  false}
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
const unsigned long BTN_COOLDOWN_MS = 110;
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
body{font-family:system-ui,-apple-system,sans-serif;margin:0;padding:16px;background:#0c0e13;color:#f1f5f9;}
.wrap{max-width:720px;margin:auto;}
.card{background:linear-gradient(180deg,#161b26,#11151d);padding:16px;border-radius:15px;margin-bottom:14px;border:1px solid #252e42;box-shadow:0 10px 26px rgba(0,0,0,.38);}
h1{margin:0 0 8px;font-size:20px;letter-spacing:.2px;display:flex;align-items:center;gap:8px;flex-wrap:wrap;}
.badge{font-size:12px;background:#1e293b;color:#7dd3fc;border:1px solid #334155;padding:3px 9px;border-radius:999px;}
h3{margin:0 0 10px;font-size:15px;color:#cbd5e1;}
.meta{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px;}
.pill{background:#1b2232;border:1px solid #2b364d;padding:6px 10px;border-radius:999px;font-size:13px;}
.pill b{color:#60a5fa;}
.status{margin-top:10px;font-size:13px;color:#94a3b8;}
.tabs{display:flex;gap:8px;margin-bottom:14px;flex-wrap:wrap;}
.tabBtn{flex:1;min-width:110px;padding:10px;border-radius:12px;border:1px solid #293347;background:#141822;color:#cbd5e1;font-weight:600;cursor:pointer;}
.tabBtn.active{background:#2563eb;border-color:#3b82f6;color:#fff;}
.tabBtn.hiddenTab{display:none;}
.pane{display:none;}
.pane.active{display:block;}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(122px,1fr));gap:8px;}
.recGrid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:10px;}
.recBox label{display:block;font-size:12px;color:#94a3b8;margin-bottom:4px;}
button,a.btn{display:inline-flex;align-items:center;justify-content:center;text-decoration:none;width:100%;padding:10px 12px;font-size:13px;font-weight:600;border-radius:10px;border:1px solid transparent;background:#2563eb;color:#fff;cursor:pointer;user-select:none;-webkit-tap-highlight-color:transparent;transition:transform .08s ease,background .12s ease,opacity .15s ease;}
button:hover,a.btn:hover{background:#3b82f6;transform:translateY(-1px);}
button:active,button.pressed,a.btn:active{transform:scale(.96);}
button:disabled{opacity:.55;cursor:not-allowed;transform:none;}
button.sec,a.btn.sec{background:#1c2230;border-color:#2f394f;color:#e2e8f0;}
button.sec:hover,a.btn.sec:hover{background:#263044;}
button.gold{background:linear-gradient(135deg,#d97706,#f59e0b);color:#111;}
button.teal{background:#0f766e;border-color:#14b8a6;color:#f0fdfa;}
button.teal:hover{background:#0d9488;}
button.plus{width:auto;padding:6px 12px;font-size:15px;border-radius:9px;}
button.del{width:auto;padding:6px 10px;font-size:12px;background:#2a1920;border-color:#5c2836;color:#fda4af;}
button.del:hover{background:#42202c;}
button.playBtn{width:auto;padding:6px 11px;font-size:12px;background:#166534;border-color:#22c55e;color:#dcfce7;}
button.playBtn:hover{background:#15803d;}
button.editBtn{width:auto;padding:6px 10px;font-size:12px;background:#1e293b;border-color:#38bdf8;color:#bae6fd;}
.reels{display:flex;justify-content:center;gap:10px;margin:12px 0;}
.reel{width:64px;height:64px;display:flex;align-items:center;justify-content:center;font-size:32px;background:#0a0d12;border:2px solid #2b364d;border-radius:14px;box-shadow:inset 0 0 12px rgba(0,0,0,.75);}
.reel.spin{animation:pulseReel .18s infinite alternate;border-color:#60a5fa;}
@keyframes pulseReel{from{transform:translateY(-1px);}to{transform:translateY(1px);}}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;}
.octRow{display:flex;gap:6px;margin-bottom:10px;flex-wrap:wrap;}
.octBtn{flex:1;min-width:68px;padding:8px 6px;border-radius:10px;border:1px solid #2e3950;background:#151a25;color:#cbd5e1;font-weight:600;cursor:pointer;font-size:12px;}
.octBtn.active{background:#0284c7;border-color:#38bdf8;color:#fff;}
.pianoScroll{overflow-x:auto;padding-bottom:6px;background:#090c11;border:1px solid #263147;border-radius:14px;padding:12px;}
.pianoStage{position:relative;height:142px;min-width:560px;user-select:none;}
.whiteRow{display:flex;height:138px;gap:2px;}
.wKey{position:relative;flex:1;height:100%;padding:0 2px 8px;border-radius:0 0 8px 8px;background:linear-gradient(180deg,#f8fafc,#e2e8f0);border:1px solid #94a3b8;color:#0f172a;font-size:11px;font-weight:700;display:flex;flex-direction:column;justify-content:flex-end;align-items:center;box-shadow:0 4px 0 #475569;}
.wKey:hover{background:linear-gradient(180deg,#ffffff,#f1f5f9);}
.wKey:active,.wKey.pressed{transform:translateY(2px);box-shadow:0 1px 0 #475569;background:#bae6fd;}
.bKey{position:absolute;top:0;width:28px;height:84px;margin-left:-14px;z-index:2;padding:0 1px 6px;border-radius:0 0 6px 6px;background:linear-gradient(180deg,#1e293b,#090d16);border:1px solid #475569;color:#e2e8f0;font-size:10px;font-weight:700;display:flex;flex-direction:column;justify-content:flex-end;align-items:center;box-shadow:0 4px 0 #020617;}
.bKey:hover{background:linear-gradient(180deg,#334155,#0f172a);}
.bKey:active,.bKey.pressed{transform:translateY(2px);box-shadow:0 1px 0 #020617;background:#0284c7;color:#fff;}
.pauseBar{margin-top:10px;height:44px;background:linear-gradient(180deg,#334155,#1e293b);border:1px solid #64748b;color:#f8fafc;font-size:13px;font-weight:700;border-radius:10px;}
.pianoInfo{margin-top:10px;font-size:13px;color:#93c5fd;text-align:center;}
.seqBox{background:#0a0d12;border:1px solid #273247;border-radius:11px;padding:10px;min-height:64px;margin:10px 0;font-size:13px;color:#e2e8f0;line-height:1.5;}
.chip{display:inline-flex;align-items:center;gap:4px;background:#182030;border:1px solid #2e3b55;padding:3px 8px;border-radius:8px;margin:2px;font-size:12px;cursor:pointer;}
.chip.pauseChip{background:#1b1d26;border-color:#475569;color:#cbd5e1;}
.chip.sel{border-color:#f59e0b;background:#2a2212;color:#fde68a;}
.backupBox{display:none;background:#0a0d12;border:1px solid #2b3850;border-radius:12px;padding:12px;margin-top:10px;}
.backupBox.open{display:block;}
.headRow{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;}
.headRow h3{margin:0;}
input,textarea,select{width:100%;padding:10px 12px;font-size:14px;font-family:inherit;border-radius:10px;border:1px solid #313c52;background:#0a0d12;color:#fff;margin-bottom:8px;}
textarea{min-height:110px;resize:vertical;}
.quick{display:flex;gap:6px;margin-top:8px;}
.quick button{padding:7px 10px;font-size:12px;background:#1a2130;border-color:#2c374d;}
.noteItem{background:#0d1118;border:1px solid #232c3d;border-radius:11px;padding:10px 12px;margin-top:8px;}
.noteTop{display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap;}
.noteTopic{font-weight:700;font-size:14px;color:#93c5fd;}
.noteBody{margin-top:6px;font-size:13px;color:#cbd5e1;white-space:pre-wrap;line-height:1.35;}
.emptyScroll{height:2200px;}
.secretZone{display:flex;flex-direction:column;gap:10px;}
button.secretGod{background:#5c1d24;border-color:#9f1239;color:#ffe4e6;}
button.secretGod:hover{background:#7f1d1d;}
button.secretOff{background:#3b2b10;border-color:#a16207;color:#fef08a;}
button.secretOff:hover{background:#523b12;}
</style></head><body><div class='wrap'>
<div class='card'>
  <h1>ESP32 Console <span class='badge' id='wVer'></span></h1>
  <div class='meta'>
    <div class='pill'>Режим: <b id='wState'></b></div>
    <div class='pill'>IP: <b id='wIp'></b></div>
    <div class='pill'>Монет: <b id='wCoins'></b></div>
    <div class='pill'>Спинов: <b id='wSpins'></b></div>
    <div class='pill'>Файлы: <b id='wFs'></b></div>
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
    <h3>Слоты по Wi-Fi</h3>
    <div class='reels'>
      <div class='reel' id='r1'>🍆</div>
      <div class='reel' id='r2'>🍏</div>
      <div class='reel' id='r3'>🌙</div>
    </div>
    <button class='gold' id='spinBtn' onclick='doSpin()'>Крутить спин</button>
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
      <div style='font-size:12px;color:#93c5fd;margin-bottom:6px;'>Выбери файл <b>esp32_songs_backup.json</b> или вставь JSON ниже:</div>
      <input type='file' id='backupFileInput' accept='.json,application/json' onchange='onBackupFilePicked(this)'>
      <textarea id='backupJsonText' placeholder='[{"title":"Мелодия","steps":[{"freq":440,"dur":260}]}]'></textarea>
      <div class='row'>
        <button type='button' class='teal' onclick='importBackupToBoard()' style='flex:1;'>Залить бэкап на консоль</button>
        <button type='button' class='sec' onclick='toggleBackupBox()' style='width:auto;padding:10px 14px;'>Закрыть</button>
      </div>
    </div>
    <div class='pianoScroll'>
      <div class='pianoStage' id='pianoStage'></div>
    </div>
    <button type='button' class='pauseBar' onpointerdown='pressPauseKey(this)'>ПАУЗА (тишина / ритм)</button>
    <div class='seqBox' id='seqPreview'>Нажимай белые и чёрные клавиши или ПАУЗУ - шаги запишутся сюда, затем нажми «Сохранить на консоль».</div>
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
    <h3>Песни на консоли (сохраняются в /songs.json)</h3>
    <div id='songsListBox'>Загрузка...</div>
  </div>
</div>

<div class='pane' id='paneNotes'>
  <div class='card'>
    <div class='headRow'>
      <h3>Новая шпаргалка</h3>
      <button type='button' class='plus' onclick='focusNewNote()' title='Новая тема'>+</button>
    </div>
    <label for='noteTopic' style='display:block;font-size:12px;color:#94a3b8;margin-bottom:4px;'>Тема</label>
    <input type='text' id='noteTopic' maxlength='40' placeholder='Например: Бел лит'>
    <label for='noteBody' style='display:block;font-size:12px;color:#94a3b8;margin-bottom:4px;'>Текст</label>
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
      <div class='recBox'><label for='recSnakeInp'>Змейка</label><input type='number' id='recSnakeInp' min='0' max='99999' value='0'></div>
      <div class='recBox'><label for='recTetrisInp'>Тетрис</label><input type='number' id='recTetrisInp' min='0' max='99999' value='0'></div>
      <div class='recBox'><label for='recFlappyInp'>Flappy Bird</label><input type='number' id='recFlappyInp' min='0' max='99999' value='0'></div>
      <div class='recBox'><label for='recPongInp'>Понг</label><input type='number' id='recPongInp' min='0' max='99999' value='0'></div>
      <div class='recBox'><label for='recDinoInp'>Динозаврик</label><input type='number' id='recDinoInp' min='0' max='99999' value='0'></div>
      <div class='recBox'><label for='recBottleInp'>Баттл Флип</label><input type='number' id='recBottleInp' min='0' max='99999' value='0'></div>
      <div class='recBox'><label for='recDoodleInp'>Doodle Jump</label><input type='number' id='recDoodleInp' min='0' max='99999' value='0'></div>
    </div>
    <button type='button' class='gold' onclick='saveRecordsWeb()'>Сохранить рекорды</button>
  </div>
</div>
</div>

<script>
const syms = ['🍏','🍆','🌙','🍇','✖️'];
const semitoneNames = ['До','До#','Ре','Ре#','Ми','Фа','Фа#','Соль','Соль#','Ля','Ля#','Си'];
const isBlackSemi = [false,true,false,true,false,false,true,false,true,false,true,false];
let animTimer = null;
let baseOctave = 3;
let draftSteps = [];
let selDraftIdx = -1;
let latestSongs = [];
let activeTab = 'games';
let lastRev = -1;
let godActive = false;

function syncGodUi(isGod) {
  godActive = Boolean(isGod);
  const recBtn = document.getElementById('tabRecordsBtn');
  if (recBtn) recBtn.classList.toggle('hiddenTab', !godActive);
  if (!godActive && activeTab === 'records') switchTab('games');
}

function setStatusMsg(msg, isErr) {
  const el = document.getElementById('wStatus');
  el.textContent = msg;
  el.style.color = isErr ? '#fca5a5' : '#94a3b8';
}

function switchTab(t) {
  if (t === 'records' && !godActive) t = 'games';
  activeTab = t;
  document.getElementById('tabGamesBtn').classList.toggle('active', t === 'games');
  document.getElementById('tabPianoBtn').classList.toggle('active', t === 'piano');
  document.getElementById('tabNotesBtn').classList.toggle('active', t === 'notes');
  const recBtn = document.getElementById('tabRecordsBtn');
  if (recBtn) recBtn.classList.toggle('active', t === 'records');
  document.getElementById('paneGames').classList.toggle('active', t === 'games');
  document.getElementById('panePiano').classList.toggle('active', t === 'piano');
  document.getElementById('paneNotes').classList.toggle('active', t === 'notes');
  document.getElementById('paneRecords').classList.toggle('active', t === 'records');
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
  const startMidi = (baseOctave + 1) * 12;
  const endMidi = startMidi + 24;
  const whites = [];
  for (let m = startMidi; m <= endMidi; m++) {
    const semi = ((m % 12) + 12) % 12;
    if (!isBlackSemi[semi]) whites.push({midi: m, label: midiToLabelJs(m)});
  }
  const totalW = whites.length;
  let htmlKeys = `<div class='whiteRow'>`;
  whites.forEach(w => { htmlKeys += `<button type='button' class='wKey' onpointerdown='pressMidiKey(${w.midi}, this)'><span>${w.label}</span></button>`; });
  htmlKeys += `</div>`;
  for (let m = startMidi; m < endMidi; m++) {
    const semi = ((m % 12) + 12) % 12;
    if (isBlackSemi[semi]) {
      let leftWhites = 0;
      for (let k = startMidi; k < m; k++) if (!isBlackSemi[((k % 12) + 12) % 12]) leftWhites++;
      const pct = (leftWhites / totalW) * 100;
      htmlKeys += `<button type='button' class='bKey' style='left:${pct.toFixed(3)}%' onpointerdown='event.stopPropagation();pressMidiKey(${m}, this)'><span>${midiToLabelJs(m)}</span></button>`;
    }
  }
  stage.innerHTML = htmlKeys;
}

function flashKey(el) {
  if (!el) return;
  el.classList.add('pressed');
  setTimeout(() => el.classList.remove('pressed'), 120);
}

function renderDraftSteps() {
  const box = document.getElementById('seqPreview');
  if (draftSteps.length === 0) {
    selDraftIdx = -1;
    box.textContent = 'Нажимай белые и чёрные клавиши или ПАУЗУ - шаги запишутся сюда, затем нажми «Сохранить на консоль».';
    return;
  }
  if (selDraftIdx < 0 || selDraftIdx >= draftSteps.length) selDraftIdx = draftSteps.length - 1;
  const chips = draftSteps.map((s, idx) => `<span class='chip${s.freq === 0 ? ' pauseChip' : ''}${idx === selDraftIdx ? ' sel' : ''}' onclick='pickDraftStep(${idx})'>${idx+1}. ${escHtml(s.label)}</span>`).join('');
  box.innerHTML = `<div style='font-size:11px;color:#94a3b8;margin-bottom:4px;'>Шагов в черновике: ${draftSteps.length} (нажми на шаг для выбора)</div>` + chips;
}

function pickDraftStep(idx) { if (idx >= 0 && idx < draftSteps.length) { selDraftIdx = idx; renderDraftSteps(); } }
function moveDraftStep(dir) {
  if (selDraftIdx < 0 || draftSteps.length < 2) return;
  const ni = selDraftIdx + dir;
  if (ni < 0 || ni >= draftSteps.length) return;
  const tmp = draftSteps[selDraftIdx]; draftSteps[selDraftIdx] = draftSteps[ni]; draftSteps[ni] = tmp; selDraftIdx = ni; renderDraftSteps();
}
function deleteSelectedDraftStep() {
  if (selDraftIdx < 0 || selDraftIdx >= draftSteps.length) return;
  draftSteps.splice(selDraftIdx, 1);
  if (selDraftIdx >= draftSteps.length) selDraftIdx = draftSteps.length - 1;
  renderDraftSteps();
}

async function pressMidiKey(midiNote, el) {
  flashKey(el);
  const dur = parseInt(document.getElementById('durSelect').value, 10) || 260;
  const freq = midiToFreqJs(midiNote); const label = midiToLabelJs(midiNote);
  draftSteps.push({freq, dur, label}); selDraftIdx = draftSteps.length - 1; renderDraftSteps();
  try { const res = await fetch('/piano', {method:'POST', body: new URLSearchParams({freq, dur})}); if (res.ok) applyData(await res.json(), false); } catch (e) {}
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
  draftSteps.push({freq: last.freq, dur: last.dur, label: last.label}); selDraftIdx = draftSteps.length - 1; renderDraftSteps();
  try {
    const body = (last.freq === 0) ? new URLSearchParams({pause: '1', dur: last.dur || 260}) : new URLSearchParams({freq: last.freq, dur: last.dur || 260});
    const res = await fetch('/piano', {method:'POST', body}); if (res.ok) applyData(await res.json(), false);
  } catch (e) {}
}

function clearSongDraft() { draftSteps = []; selDraftIdx = -1; renderDraftSteps(); }
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
  const raw = document.getElementById('backupJsonText').value.trim();
  if (!raw) { alert('Выбери файл бэкапа или вставь JSON'); return; }
  try {
    const res = await fetch('/songs/import', {method:'POST', body: new URLSearchParams({json: raw})});
    if (!res.ok) { setStatusMsg('Ошибка HTTP при импорте: ' + res.status, true); return; }
    applyData(await res.json(), true); document.getElementById('backupBox').classList.remove('open');
  } catch (e) { setStatusMsg('Ошибка связи при загрузке бэкапа', true); }
}

function loadSongIntoEditor(idx) {
  const s = latestSongs[idx]; if (!s || !Array.isArray(s.steps)) return;
  document.getElementById('songTitle').value = s.title || '';
  draftSteps = s.steps.map(x => ({freq: x.freq, dur: x.dur, label: x.label || (x.freq > 0 ? x.freq + 'Гц' : 'Пауза')}));
  selDraftIdx = draftSteps.length > 0 ? 0 : -1; switchTab('piano'); renderDraftSteps();
}

async function uploadSongToBoard(playAfter) {
  const title = document.getElementById('songTitle').value.trim();
  if (!title) { alert('Введи название песни'); return; }
  if (draftSteps.length === 0) { alert('Нажми хотя бы одну клавишу или Паузу'); return; }
  const b1 = document.getElementById('saveSongBtn'); const b2 = document.getElementById('savePlaySongBtn');
  b1.disabled = true; b2.disabled = true;
  try {
    const res = await fetch('/songs/add', {method:'POST', body: new URLSearchParams({title, seq: draftSteps.map(s => s.freq + ':' + s.dur).join(','), play: playAfter ? '1' : '0'})});
    if (!res.ok) { setStatusMsg('Ошибка сохранения (HTTP ' + res.status + ')', true); return; }
    const data = await res.json(); applyData(data, true);
    if (data.status && !data.status.toLowerCase().includes('ошиб')) { document.getElementById('songTitle').value = ''; draftSteps = []; selDraftIdx = -1; renderDraftSteps(); }
  } catch (e) { setStatusMsg('Не удалось достучаться до ESP32 при сохранении', true); } finally { b1.disabled = false; b2.disabled = false; }
}

async function playSongOnBoard(idx) { try { const res = await fetch('/songs/play', {method:'POST', body: new URLSearchParams({idx})}); if (res.ok) applyData(await res.json(), false); } catch (e) {} }
async function deleteSongOnBoard(idx) { try { const res = await fetch('/songs/delete', {method:'POST', body: new URLSearchParams({idx})}); if (res.ok) applyData(await res.json(), true); } catch (e) {} }
function focusNewNote() { document.getElementById('noteTopic').value = ''; document.getElementById('noteBody').value = ''; document.getElementById('noteTopic').focus(); }
function escHtml(s) { return String(s || '').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }

function renderSongs(list) {
  latestSongs = Array.isArray(list) ? list : [];
  const box = document.getElementById('songsListBox');
  if (latestSongs.length === 0) { box.innerHTML = "<div style='color:#94a3b8;font-size:13px;'>Пока песен нет. Сыграй на пианино выше и нажми «Сохранить на консоль» или загрузи бэкап.</div>"; return; }
  box.innerHTML = latestSongs.map((s, i) => `<div class='noteItem'><div class='noteTop'><span class='noteTopic'>${escHtml(s.title)} (${s.count} шагов)</span><div class='row' style='gap:6px;'><button type='button' class='playBtn' onclick='playSongOnBoard(${i})'>Слушать</button><button type='button' class='editBtn' onclick='loadSongIntoEditor(${i})'>В пианино</button><button type='button' class='del' onclick='deleteSongOnBoard(${i})'>Удалить</button></div></div><div class='noteBody'>${escHtml(Array.isArray(s.steps) ? s.steps.map(x => x.label).slice(0, 22).join(' - ') : '')}</div></div>`).join('');
}

function renderNotes(list) {
  const box = document.getElementById('notesListBox');
  if (!Array.isArray(list) || list.length === 0) { box.innerHTML = "<div style='color:#94a3b8;font-size:13px;'>Пока тем нет. Нажми + и загрузи первую шпаргалку.</div>"; return; }
  box.innerHTML = list.map((n, i) => `<div class='noteItem'><div class='noteTop'><span class='noteTopic'>${escHtml(n.topic)}</span><button type='button' class='del' onclick='deleteNote(${i})'>Удалить</button></div><div class='noteBody'>${escHtml(n.body)}</div></div>`).join('');
}

async function loadNotesList() { try { const res = await fetch('/notes'); if (res.ok) renderNotes(await res.json()); } catch (e) {} }
async function loadSongsList() { try { const res = await fetch('/songs'); if (res.ok) renderSongs(await res.json()); } catch (e) {} }

function syncRecordsInputs(d) {
  if (document.activeElement && document.activeElement.closest('#paneRecords')) return;
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

function applyData(d, reloadLists) {
  if (typeof d.rev === 'number') lastRev = d.rev;
  if (typeof d.ver === 'string') document.getElementById('wVer').textContent = d.ver;
  if (typeof d.ip === 'string') document.getElementById('wIp').textContent = d.ip;
  if (typeof d.fs === 'string') document.getElementById('wFs').textContent = d.fs;
  if (typeof d.god === 'boolean') syncGodUi(d.god);
  document.getElementById('wState').textContent = d.state;
  document.getElementById('wCoins').textContent = d.god ? '∞ (БОГ)' : d.coins;
  document.getElementById('wSpins').textContent = d.totalSpins;
  syncRecordsInputs(d);
  setStatusMsg(d.status || '', Boolean(d.status && d.status.toLowerCase().includes('ошиб')));
  if (d.pianoNote) document.getElementById('pianoInfo').textContent = d.pianoFreq > 0 ? ('Последняя нота: ' + d.pianoNote + ' (' + d.pianoFreq + ' Гц)') : 'Последний шаг: Пауза';
  if (reloadLists) { if (activeTab === 'piano') loadSongsList(); if (activeTab === 'notes') loadNotesList(); }
  if (!d.spinning) {
    if (animTimer) { clearInterval(animTimer); animTimer = null; }
    ['r1','r2','r3'].forEach(id => document.getElementById(id).classList.remove('spin'));
    document.getElementById('r1').textContent = d.s1; document.getElementById('r2').textContent = d.s2; document.getElementById('r3').textContent = d.s3;
    document.getElementById('spinBtn').disabled = false;
  } else {
    document.getElementById('spinBtn').disabled = true;
    ['r1','r2','r3'].forEach(id => document.getElementById(id).classList.add('spin'));
    if (!animTimer) animTimer = setInterval(() => { document.getElementById('r1').textContent = syms[Math.floor(Math.random()*syms.length)]; document.getElementById('r2').textContent = syms[Math.floor(Math.random()*syms.length)]; document.getElementById('r3').textContent = syms[Math.floor(Math.random()*syms.length)]; }, 115);
  }
}

async function refreshStatus(force) {
  if (document.hidden && !force) return;
  try { const res = await fetch('/status'); if (!res.ok) return; const d = await res.json(); if (!force && d.rev === lastRev) return; applyData(d, true); } catch (e) {}
}

async function toggleGod() { try { const res = await fetch('/god', {method:'POST'}); if (res.ok) applyData(await res.json(), false); } catch (e) {} }
async function toggleDisable() { try { const res = await fetch('/disable', {method:'POST'}); if (res.ok) applyData(await res.json(), false); } catch (e) {} }
async function sendMode(m) { const res = await fetch('/mode', {method:'POST', body: new URLSearchParams({m})}); if (res.ok) applyData(await res.json(), false); }
async function doSpin() { document.getElementById('spinBtn').disabled = true; ['r1','r2','r3'].forEach(id => document.getElementById(id).classList.add('spin')); const res = await fetch('/spin', {method:'POST'}); if (res.ok) applyData(await res.json(), false); }
async function addCoins(customVal) { const val = customVal || document.getElementById('coinInput').value; const res = await fetch('/coins', {method:'POST', body: new URLSearchParams({amount: val})}); if (res.ok) applyData(await res.json(), false); }
function quickCoins(v) { document.getElementById('coinInput').value = v; addCoins(v); }
async function uploadNote() { const topic = document.getElementById('noteTopic').value.trim(); const bodyText = document.getElementById('noteBody').value.trim(); if (!topic) { alert('Введи тему шпаргалки'); return; } const res = await fetch('/notes/add', {method:'POST', body: new URLSearchParams({topic, body: bodyText})}); if (res.ok) { document.getElementById('noteTopic').value = ''; document.getElementById('noteBody').value = ''; applyData(await res.json(), true); } }
async function deleteNote(idx) { const res = await fetch('/notes/delete', {method:'POST', body: new URLSearchParams({idx})}); if (res.ok) applyData(await res.json(), true); }

document.addEventListener('visibilitychange', () => { if (!document.hidden) refreshStatus(false); });
buildPianoKeyboard(); setInterval(() => refreshStatus(false), 3500); refreshStatus(true);
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
void drawMenuRow(int idx, int curIdx, int yPos, const char* label);
void drawScrollIndicator(int curIdx, int totalItems);
void drawSymbolAt(int type, int cx, int cy);
void drawBottleGraphic(int cx, int cy, float angle);
void drawArchLogo(int x, int y);
void startArchScreensaver();
void updateArchScreensaver();
void wakeScreen();
void beep(int freq, int duration);
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
void scanWifiNetworksSafe();
void pollWifiScanAsync(unsigned long now);
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

void applyGodMode(bool enable) {
  godMode = enable;
  if (enable) {
    disabledMode = false;
    coins = GOD_COINS_VAL;
    displayCoins = GOD_COINS_VAL;
    for (uint8_t i = 0; i < ACH_COUNT; i++) {
      achievements[i].unlocked = true;
    }
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

static bool isUtf8Continuation(uint8_t b) {
  return (b & 0xC0) == 0x80;
}

static String utf8Truncate(const String& s, size_t maxChars) {
  String out;
  size_t chars = 0;
  size_t i = 0;
  const size_t n = s.length();

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
  s.replace("\r\n", "\n");
  s.replace('\r', '\n');
  s.replace("\xE2\x80\x94", "-");
  s.replace("\xE2\x80\x93", "-");
  s.replace("\xE2\x80\xA6", "...");
  s.replace("\xC2\xA0", " ");
  return s;
}

static String jsonUnescape(const String& raw) {
  String out;
  out.reserve(raw.length());
  for (size_t i = 0; i < raw.length(); i++) {
    char c = raw[i];
    if (c == '\\' && i + 1 < raw.length()) {
      char n = raw[++i];
      if (n == 'n') out += '\n';
      else if (n == 'r') out += '\r';
      else if (n == 't') out += ' ';
      else out += n;
    } else {
      out += c;
    }
  }
  return out;
}

static bool extractJsonStringField(const String& obj, const char* key, String& valOut) {
  String pat = String("\"") + key + "\"";
  int kPos = obj.indexOf(pat);
  if (kPos < 0) return false;
  int colonPos = obj.indexOf(':', kPos + pat.length());
  if (colonPos < 0) return false;
  int qStart = obj.indexOf('"', colonPos + 1);
  if (qStart < 0) return false;
  String raw;
  bool escaped = false;
  for (int i = qStart + 1; i < (int)obj.length(); i++) {
    char c = obj[i];
    if (escaped) { raw += c; escaped = false; }
    else if (c == '\\') { escaped = true; }
    else if (c == '"') { valOut = jsonUnescape(raw); return true; }
    else { raw += c; }
  }
  return false;
}

static int findMatchingBracket(const String& s, int openPos, char openCh, char closeCh) {
  if (openPos < 0 || openPos >= (int)s.length() || s[openPos] != openCh) return -1;
  int depth = 0;
  bool inStr = false;
  bool esc = false;
  for (int i = openPos; i < (int)s.length(); i++) {
    char c = s[i];
    if (esc) { esc = false; continue; }
    if (c == '\\') { esc = true; continue; }
    if (c == '"') { inStr = !inStr; continue; }
    if (!inStr) {
      if (c == openCh) depth++;
      else if (c == closeCh) {
        depth--;
        if (depth == 0) return i;
      }
    }
  }
  return -1;
}

static int extractIntAfterKey(const String& obj, const char* key, bool& okOut) {
  okOut = false;
  String pat = String("\"") + key + "\"";
  int kPos = obj.indexOf(pat);
  if (kPos < 0) return 0;
  int colonPos = obj.indexOf(':', kPos + pat.length());
  if (colonPos < 0) return 0;
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
  File f = LittleFS.open(NOTES_FILE, "w");
  if (!f) return;
  f.print("[");
  for (size_t i = 0; i < notesList.size(); i++) {
    if (i > 0) f.print(",");
    f.print("{\"t\":\"");
    f.print(escapeJsonStr(notesList[i].topic));
    f.print("\",\"b\":\"");
    f.print(escapeJsonStr(notesList[i].body));
    f.print("\"}");
  }
  f.print("]");
  f.flush();
  f.close();
}

void loadNotesFromFs() {
  notesList.clear();
  if (!fsReady || !LittleFS.exists(NOTES_FILE)) return;
  File f = LittleFS.open(NOTES_FILE, "r");
  if (!f) return;
  String content = f.readString();
  f.close();

  int pos = 0;
  while (pos < (int)content.length() && notesList.size() < MAX_NOTES) {
    int objStart = content.indexOf('{', pos);
    if (objStart < 0) break;
    int objEnd = findMatchingBracket(content, objStart, '{', '}');
    if (objEnd < 0) break;
    String obj = content.substring(objStart, objEnd + 1);
    String tVal, bVal;
    if (extractJsonStringField(obj, "t", tVal) && extractJsonStringField(obj, "b", bVal)) {
      tVal = utf8Truncate(sanitizeNoteText(tVal), MAX_NOTE_TOPIC_W);
      tVal.trim();
      bVal = utf8Truncate(sanitizeNoteText(bVal), MAX_NOTE_BODY_W);
      bVal.trim();
      if (tVal.length() > 0) notesList.push_back({tVal, bVal});
    }
    pos = objEnd + 1;
  }
}

bool saveSongsToFs(String* errOut) {
  if (!fsReady) {
    if (errOut) *errOut = "LittleFS не смонтирован";
    return false;
  }
  File f = LittleFS.open(SONGS_FILE, "w");
  if (!f) {
    if (errOut) *errOut = "Не удалось открыть /songs.json для записи";
    return false;
  }
  f.print("[");
  for (size_t i = 0; i < songsList.size(); i++) {
    if (i > 0) f.print(",");
    f.print("{\"title\":\"");
    f.print(escapeJsonStr(songsList[i].title));
    f.print("\",\"steps\":[");
    for (size_t j = 0; j < songsList[i].steps.size(); j++) {
      if (j > 0) f.print(",");
      f.print("[");
      f.print(songsList[i].steps[j].freq);
      f.print(",");
      f.print(songsList[i].steps[j].dur);
      f.print("]");
    }
    f.print("]}");
  }
  f.print("]");
  f.flush();
  f.close();
  return true;
}

static bool parseSongObject(const String& obj, SongItem& outSong) {
  String titleVal;
  if (!extractJsonStringField(obj, "title", titleVal)) return false;
  titleVal = utf8Truncate(sanitizeNoteText(titleVal), MAX_SONG_TITLE_W);
  titleVal.trim();
  if (titleVal.length() == 0) return false;

  int keyPos = obj.indexOf("\"steps\"");
  if (keyPos < 0) return false;
  int arrStart = obj.indexOf('[', keyPos);
  if (arrStart < 0) return false;
  int arrEnd = findMatchingBracket(obj, arrStart, '[', ']');
  if (arrEnd <= arrStart) return false;

  String arrStr = obj.substring(arrStart + 1, arrEnd);
  std::vector<SongStep> parsedSteps;
  int p = 0;
  while (p < (int)arrStr.length() && parsedSteps.size() < MAX_SONG_STEPS) {
    int pairStart = arrStr.indexOf('[', p);
    if (pairStart >= 0) {
      int pairEnd = findMatchingBracket(arrStr, pairStart, '[', ']');
      if (pairEnd < 0) break;
      String pairStr = arrStr.substring(pairStart + 1, pairEnd);
      int comma = pairStr.indexOf(',');
      if (comma > 0) {
        int fr = pairStr.substring(0, comma).toInt();
        int du = pairStr.substring(comma + 1).toInt();
        if (fr >= 0 && fr <= 4200 && du >= 30 && du <= 5000) parsedSteps.push_back({fr, du});
      }
      p = pairEnd + 1;
      continue;
    }
    int elemStart = arrStr.indexOf('{', p);
    if (elemStart >= 0) {
      int elemEnd = findMatchingBracket(arrStr, elemStart, '{', '}');
      if (elemEnd < 0) break;
      String elemStr = arrStr.substring(elemStart, elemEnd + 1);
      bool okF = false, okD = false;
      int fr = extractIntAfterKey(elemStr, "freq", okF);
      int du = extractIntAfterKey(elemStr, "dur", okD);
      if (okF && okD && fr >= 0 && fr <= 4200 && du >= 30 && du <= 5000) parsedSteps.push_back({fr, du});
      p = elemEnd + 1;
      continue;
    }
    break;
  }

  if (parsedSteps.empty()) return false;
  outSong.title = titleVal;
  outSong.steps = parsedSteps;
  return true;
}

void loadSongsFromFs() {
  songsList.clear();
  if (!fsReady || !LittleFS.exists(SONGS_FILE)) return;
  File f = LittleFS.open(SONGS_FILE, "r");
  if (!f) return;
  String content = f.readString();
  f.close();

  int pos = 0;
  while (pos < (int)content.length() && songsList.size() < MAX_SONGS) {
    int objStart = content.indexOf('{', pos);
    if (objStart < 0) break;
    int objEnd = findMatchingBracket(content, objStart, '{', '}');
    if (objEnd < 0) break;
    String obj = content.substring(objStart, objEnd + 1);
    SongItem item;
    if (parseSongObject(obj, item)) songsList.push_back(item);
    pos = objEnd + 1;
  }
}

void initStorage() {
  fsReady = LittleFS.begin(false);
  if (!fsReady) {
    fsReady = LittleFS.begin(true);
    fsInitMsg = fsReady ? "LittleFS отформатирован" : "Ошибка LittleFS";
  } else {
    fsInitMsg = "LittleFS готов";
  }
  if (fsReady) {
    loadNotesFromFs();
    loadSongsFromFs();
  }
}

void addOrUpdateNote(const String& topic, const String& body) {
  String cleanTopic = sanitizeNoteText(topic);
  cleanTopic.trim();
  if (cleanTopic.length() == 0) cleanTopic = "Без темы";
  cleanTopic = utf8Truncate(cleanTopic, MAX_NOTE_TOPIC_W);

  String cleanBody = sanitizeNoteText(body);
  cleanBody.trim();
  cleanBody = utf8Truncate(cleanBody, MAX_NOTE_BODY_W);

  for (size_t i = 0; i < notesList.size(); i++) {
    if (notesList[i].topic.equalsIgnoreCase(cleanTopic)) {
      notesList[i].topic = cleanTopic;
      notesList[i].body = cleanBody;
      noteCursor = (int)i;
      saveNotesToFs();
      markDirty();
      return;
    }
  }

  if (notesList.size() >= MAX_NOTES) notesList.erase(notesList.begin());
  notesList.push_back({cleanTopic, cleanBody});
  noteCursor = (int)notesList.size() - 1;
  saveNotesToFs();
  markDirty();
}

void deleteNoteByIndex(int idx) {
  if (idx < 0 || idx >= (int)notesList.size()) return;
  if (activeNoteIdx == idx) {
    activeNoteIdx = -1;
    if (gameState == ST_NOTE_VIEW) gameState = ST_NOTES_LIST;
  } else if (activeNoteIdx > idx) {
    activeNoteIdx--;
  }
  notesList.erase(notesList.begin() + idx);
  if (noteCursor >= (int)notesList.size()) noteCursor = max(0, (int)notesList.size() - 1);
  saveNotesToFs();
  markDirty();
}

int addOrUpdateSong(const String& title, const std::vector<SongStep>& steps, String* errOut) {
  String cleanTitle = sanitizeNoteText(title);
  cleanTitle.trim();
  if (cleanTitle.length() == 0) cleanTitle = "Без названия";
  cleanTitle = utf8Truncate(cleanTitle, MAX_SONG_TITLE_W);

  std::vector<SongStep> validSteps;
  for (size_t i = 0; i < steps.size() && validSteps.size() < MAX_SONG_STEPS; i++) {
    int fr = constrain(steps[i].freq, 0, 4200);
    int du = constrain(steps[i].dur, 30, 5000);
    validSteps.push_back({fr, du});
  }
  if (validSteps.empty()) {
    if (errOut) *errOut = "Список шагов пуст";
    return -1;
  }

  int targetIdx = -1;
  for (size_t i = 0; i < songsList.size(); i++) {
    if (songsList[i].title.equalsIgnoreCase(cleanTitle)) {
      songsList[i].title = cleanTitle;
      songsList[i].steps = validSteps;
      targetIdx = (int)i;
      break;
    }
  }

  if (targetIdx < 0) {
    if (songsList.size() >= MAX_SONGS) songsList.erase(songsList.begin());
    songsList.push_back({cleanTitle, validSteps});
    targetIdx = (int)songsList.size() - 1;
  }

  String fsErr;
  if (!saveSongsToFs(&fsErr)) {
    if (errOut) *errOut = fsErr;
    return -1;
  }
  pianoSongCursor = targetIdx;
  markDirty();
  return targetIdx;
}

void deleteSongByIndex(int idx) {
  if (idx < 0 || idx >= (int)songsList.size()) return;
  if (songPlaying && activeSongIdx == idx) stopSongPlayback();
  else if (songPlaying && activeSongIdx > idx) activeSongIdx--;
  songsList.erase(songsList.begin() + idx);
  if (pianoSongCursor >= (int)songsList.size()) pianoSongCursor = max(0, (int)songsList.size() - 1);
  saveSongsToFs();
  markDirty();
}

int importSongsFromJson(const String& rawJson, String* errOut) {
  String s = sanitizeNoteText(rawJson);
  s.trim();
  if (s.length() == 0) {
    if (errOut) *errOut = "Файл бэкапа пуст";
    return 0;
  }

  std::vector<SongItem> imported;
  int pos = 0;
  while (pos < (int)s.length() && imported.size() < MAX_SONGS) {
    int objStart = s.indexOf('{', pos);
    if (objStart < 0) break;
    int objEnd = findMatchingBracket(s, objStart, '{', '}');
    if (objEnd < 0) break;
    String obj = s.substring(objStart, objEnd + 1);
    SongItem item;
    if (parseSongObject(obj, item)) imported.push_back(item);
    pos = objEnd + 1;
  }

  if (imported.empty()) {
    if (errOut) *errOut = "Не найдено ни одной валидной песни в JSON";
    return 0;
  }

  for (size_t k = 0; k < imported.size(); k++) {
    bool merged = false;
    for (size_t i = 0; i < songsList.size(); i++) {
      if (songsList[i].title.equalsIgnoreCase(imported[k].title)) {
        songsList[i] = imported[k];
        merged = true;
        break;
      }
    }
    if (!merged) {
      if (songsList.size() >= MAX_SONGS) songsList.erase(songsList.begin());
      songsList.push_back(imported[k]);
    }
  }

  String fsErr;
  if (!saveSongsToFs(&fsErr)) {
    if (errOut) *errOut = fsErr;
    return 0;
  }
  markDirty();
  return (int)imported.size();
}

void buildNoteLines(const String& text, std::vector<String>& linesOut, size_t maxCharsPerLine) {
  linesOut.clear();
  if (maxCharsPerLine < 6) maxCharsPerLine = 6;
  String normalized = sanitizeNoteText(text);

  int start = 0;
  while (start <= (int)normalized.length()) {
    int nl = normalized.indexOf('\n', start);
    String para = (nl >= 0) ? normalized.substring(start, nl) : normalized.substring(start);
    if (para.length() == 0) {
      linesOut.push_back("");
    } else {
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
        size_t take = min(maxCharsPerLine, glyphs.size() - pos);
        size_t cut = take;
        if (pos + take < glyphs.size()) {
          for (size_t back = take; back > 4; back--) {
            if (glyphs[pos + back - 1] == " ") { cut = back - 1; break; }
          }
        }
        String line;
        for (size_t k = 0; k < cut; k++) line += glyphs[pos + k];
        line.trim();
        linesOut.push_back(line);
        pos += cut;
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
    case ST_GAME_SELECT:       return 8;
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
  String out;
  out.reserve(s.length() + 12);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\\') out += "\\\\";
    else if (c == '"') out += "\\\"";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else out += c;
  }
  return out;
}

void wakeScreen() {
  screenSleeping = false;
  u8g2.setContrast(220);
  idleTimer = millis();
  markDirty();
}

void startArchScreensaver() {
  screenSleeping = true;
  u8g2.setContrast(150);
  archX = (float)random(4, 128 - ARCH_LOGO_W - 4);
  archY = (float)random(4, 64 - ARCH_LOGO_H - 4);
  archVX = (random(0, 2) == 0) ? 0.85f : -0.85f;
  archVY = (random(0, 2) == 0) ? 0.65f : -0.65f;
  archTimer = millis();
  markDirty();
}

void updateArchScreensaver() {
  if (!screenSleeping) return;
  unsigned long now = millis();
  if ((unsigned long)(now - archTimer) < 28) return;
  archTimer = now;

  archX += archVX;
  archY += archVY;

  if (archX <= 2.0f) { archX = 2.0f; archVX = -archVX; }
  else if (archX >= (float)(126 - ARCH_LOGO_W)) { archX = (float)(126 - ARCH_LOGO_W); archVX = -archVX; }

  if (archY <= 2.0f) { archY = 2.0f; archVY = -archVY; }
  else if (archY >= (float)(62 - ARCH_LOGO_H)) { archY = (float)(62 - ARCH_LOGO_H); archVY = -archVY; }
}

void drawArchLogo(int x, int y) {
  const int cx = x + 22;
  const int topY = y + 2;
  const int botY = y + 26;

  int prevLx = cx;
  int prevLy = topY;
  for (int i = 1; i <= 15; i++) {
    float t = (float)i / 15.0f;
    float curve = powf(t, 1.5f);
    int lx = cx - (int)roundf(curve * 16.0f);
    int ly = topY + (int)roundf(t * (botY - topY));
    u8g2.drawLine(prevLx, prevLy, lx, ly);
    if (i >= 4) u8g2.drawLine(prevLx + 1, prevLy, lx + 1, ly);
    prevLx = lx;
    prevLy = ly;
  }

  int prevRx = cx + 1;
  int prevRy = topY + 6;
  for (int i = 1; i <= 11; i++) {
    float t = (float)i / 11.0f;
    float curve = powf(t, 1.4f);
    int rx = (cx + 1) + (int)roundf(curve * 15.0f);
    int ry = (topY + 6) + (int)roundf(t * (botY - (topY + 6)));
    u8g2.drawLine(prevRx, prevRy, rx, ry);
    if (i >= 3) u8g2.drawLine(prevRx + 1, prevRy, rx + 1, ry);
    prevRx = rx;
    prevRy = ry;
  }

  u8g2.drawLine(cx - 2, y + 19, cx + 13, y + 24);

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(x + 1, y + 36, "ARCH LINUX");
}

struct CalcParser {
  const char* s;
  size_t pos;
  bool err;
  const char* errText;

  void skipSpaces() { while (s[pos] == ' ' || s[pos] == '\t') pos++; }

  double parseExpr() {
    double val = parseTerm();
    while (!err) {
      skipSpaces();
      char op = s[pos];
      if (op == '+' || op == '-') {
        pos++;
        double rhs = parseTerm();
        if (err) return 0.0;
        if (op == '+') val += rhs; else val -= rhs;
      } else break;
    }
    return val;
  }

  double parseTerm() {
    double val = parseFactor();
    while (!err) {
      skipSpaces();
      char op = s[pos];
      if (op == '*' || op == '/') {
        pos++;
        double rhs = parseFactor();
        if (err) return 0.0;
        if (op == '*') val *= rhs;
        else {
          if (fabs(rhs) < 1e-12) { err = true; errText = "Деление на 0"; return 0.0; }
          val /= rhs;
        }
      } else break;
    }
    return val;
  }

  double parseFactor() {
    skipSpaces();
    char c = s[pos];
    if (c == '+') { pos++; return parseFactor(); }
    if (c == '-') { pos++; return -parseFactor(); }
    if (c == '(') { pos++; double v = parseExpr(); skipSpaces(); if (s[pos] == ')') pos++; return v; }
    if ((c >= '0' && c <= '9') || c == '.') {
      char numBuf[32]; size_t nIdx = 0; bool hasDot = false;
      while (((s[pos] >= '0' && s[pos] <= '9') || s[pos] == '.') && nIdx < sizeof(numBuf) - 1) {
        if (s[pos] == '.') {
          if (hasDot) { err = true; errText = "Лишняя точка"; return 0.0; }
          hasDot = true;
        }
        numBuf[nIdx++] = s[pos++];
      }
      numBuf[nIdx] = '\0';
      return ::strtod(numBuf, nullptr);
    }
    if (c == '\0') { err = true; errText = "Незакончено"; return 0.0; }
    err = true; errText = "Синтаксис"; return 0.0;
  }
};

static bool isCalcOp(char c) {
  return (c == '+' || c == '-' || c == '*' || c == '/');
}

void evaluateCalcExpression(const char* expr) {
  calcError = false; calcHasResult = false; calcErrMsg = ""; calcResult = 0.0;
  if (!expr || expr[0] == '\0') return;
  CalcParser p{expr, 0, false, ""};
  double res = p.parseExpr();
  p.skipSpaces();
  if (!p.err && p.s[p.pos] != '\0') { p.err = true; p.errText = "Лишний знак"; }
  if (p.err || isnan(res) || isinf(res)) {
    calcError = true;
    calcErrMsg = (p.err && p.errText) ? String(p.errText) : String("Ошибка");
  } else {
    calcResult = res;
    calcHasResult = true;
  }
}

void appendCalcChar(char ch) {
  size_t len = strlen(calcExpr);
  if (len >= MAX_CALC_EXPR) return;
  if (isCalcOp(ch) && len > 0) {
    char prev = calcExpr[len - 1];
    if (isCalcOp(prev)) {
      if (ch == '-' && prev != '-') { calcExpr[len] = ch; calcExpr[len + 1] = '\0'; }
      else calcExpr[len - 1] = ch;
      evaluateCalcExpression(calcExpr);
      return;
    }
  }
  calcExpr[len] = ch;
  calcExpr[len + 1] = '\0';
  evaluateCalcExpression(calcExpr);
}

void backspaceCalcExpr() {
  size_t len = strlen(calcExpr);
  if (len > 0) {
    calcExpr[len - 1] = '\0';
    evaluateCalcExpression(calcExpr);
  }
}

int midiToFreq(int midiNote) {
  midiNote = constrain(midiNote, 36, 96);
  float freq = 440.0f * powf(2.0f, ((float)midiNote - 69.0f) / 12.0f);
  return constrain((int)roundf(freq), 65, 2400);
}

String midiToLabel(int midiNote) {
  const char* names[12] = {"До", "До#", "Ре", "Ре#", "Ми", "Фа", "Фа#", "Соль", "Соль#", "Ля", "Ля#", "Си"};
  int note = ((midiNote % 12) + 12) % 12;
  int oct = (midiNote / 12) - 1;
  return String(names[note]) + String(oct);
}

String freqToLabel(int freq) {
  if (freq <= 0) return "Пауза";
  float midi = 69.0f + 12.0f * log2f((float)freq / 440.0f);
  return midiToLabel((int)roundf(midi));
}

void playMidiNote(int midiNote, int durMs) {
  stopSongPlayback();
  metroRunning = false;
  midiNote = constrain(midiNote, 36, 96);
  durMs = constrain(durMs, 40, 2000);
  int freq = midiToFreq(midiNote);
  lastPlayedFreq = freq;
  lastPlayedNoteName = midiToLabel(midiNote);
  if (soundEnabled) tone(BUZZER_PIN, freq, durMs);
  markDirty();
}

void triggerPauseStep(int durMs) {
  stopSongPlayback();
  metroRunning = false;
  lastPlayedFreq = 0;
  lastPlayedNoteName = "Пауза";
  noTone(BUZZER_PIN);
  (void)durMs;
  markDirty();
}

void stopSongPlayback() {
  if (songPlaying) {
    songPlaying = false;
    activeSongStepLabel = "";
    noTone(BUZZER_PIN);
    markDirty();
  } else {
    noTone(BUZZER_PIN);
  }
}

static void applySongStepAt(size_t idx, unsigned long baseMs) {
  const std::vector<SongStep>& steps = songsList[activeSongIdx].steps;
  const SongStep& st = steps[idx];
  if (st.freq > 0) {
    activeSongStepLabel = String(st.freq) + " Гц";
    lastPlayedFreq = st.freq;
    if (soundEnabled) tone(BUZZER_PIN, st.freq, st.dur);
  } else {
    activeSongStepLabel = "Пауза";
    lastPlayedFreq = 0;
    noTone(BUZZER_PIN);
  }
  nextSongStepMs = baseMs + (unsigned long)st.dur + 12UL;
}

void startSongPlayback(int songIdx) {
  if (songIdx < 0 || songIdx >= (int)songsList.size() || songsList[songIdx].steps.empty()) return;
  metroRunning = false;
  noTone(BUZZER_PIN);
  activeSeq = nullptr; activeSeqLen = 0; activeSeqIdx = 0;
  pianoSongCursor = songIdx; activeSongIdx = songIdx; activeSongStepIdx = 0; songPlaying = true;
  applySongStepAt(0, millis());
  markDirty();
}

void updateSongPlayback(unsigned long now) {
  if (!songPlaying) return;
  if (activeSongIdx < 0 || activeSongIdx >= (int)songsList.size()) { stopSongPlayback(); return; }
  if ((unsigned long)(now - nextSongStepMs) < 0x80000000UL && now < nextSongStepMs) return;
  activeSongStepIdx++;
  const std::vector<SongStep>& steps = songsList[activeSongIdx].steps;
  if (activeSongStepIdx >= steps.size()) { stopSongPlayback(); return; }
  applySongStepAt(activeSongStepIdx, now);
}

void stopMetronome(bool goToMenu) {
  metroRunning = false;
  metroBeat = 0;
  noTone(BUZZER_PIN);
  if (goToMenu) { gameState = ST_MUSIC_MENU; menuCursor = 1; }
  markDirty();
}

void setMetronomeBpm(int nextBpm) {
  metroBpm = constrain(nextBpm, MIN_BPM, MAX_BPM);
  nextMetroMs = millis();
  markDirty();
}

void updateMetronome(unsigned long now) {
  if (!metroRunning || gameState != ST_METRONOME) return;
  if ((unsigned long)(now - nextMetroMs) < 0x80000000UL && now < nextMetroMs) return;
  unsigned long intervalMs = max(120UL, 60000UL / (unsigned long)constrain(metroBpm, MIN_BPM, MAX_BPM));
  nextMetroMs = now + intervalMs;
  metroBeat = (metroBeat + 1) % 4;
  if (soundEnabled) tone(BUZZER_PIN, (metroBeat == 0) ? 1480 : 980, 35);
}

void scanWifiNetworksSafe() {
  wifiScanActive = true;
  wifiScanStartMs = millis();
  gameState = ST_WIFI_SCAN;
  markDirty();

  WiFi.scanDelete();
  if (WiFi.getMode() == WIFI_OFF) { WiFi.mode(WIFI_STA); }
  else if (!(WiFi.getMode() & WIFI_STA)) { WiFi.mode(WIFI_AP_STA); }

  WiFi.scanNetworks(true, true);
}

void pollWifiScanAsync(unsigned long now) {
  if (!wifiScanActive) return;
  int res = WiFi.scanComplete();
  if (res == WIFI_SCAN_RUNNING) {
    if ((unsigned long)(now - wifiScanStartMs) > 12000UL) {
      WiFi.scanDelete();
      wifiScanActive = false;
      wifiNetCount = 0;
      lastWebStatus = "Таймаут сканирования Wi-Fi";
      menuCursor = 0;
      if (gameState == ST_WIFI_SCAN) gameState = ST_WIFI_LIST;
      markDirty();
    }
    return;
  }
  wifiScanActive = false;
  if (res < 0) {
    wifiNetCount = 0;
    lastWebStatus = "Ошибка скана Wi-Fi (" + String(res) + ")";
  } else {
    wifiNetCount = res;
    lastWebStatus = "Найдено сетей: " + String(wifiNetCount);
  }
  menuCursor = 0;
  if (gameState == ST_WIFI_SCAN) gameState = ST_WIFI_LIST;
  markDirty();
}

void syncNtpTime(bool force) {
  if (WiFi.status() == WL_CONNECTED && (!ntpConfigured || force)) {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
    ntpConfigured = true;
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

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(WEATHER_LAT, 2) +
               "&longitude=" + String(WEATHER_LON, 2) + "&current=temperature_2m,weather_code";
  if (!http.begin(client, url)) return;
  http.setTimeout(1800);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    int curPos = payload.indexOf("\"current\"");
    String slice = (curPos >= 0) ? payload.substring(curPos) : payload;
    int tPos = slice.indexOf("\"temperature_2m\"");
    int cPos = slice.indexOf("\"weather_code\"");
    if (tPos >= 0 && cPos >= 0) {
      int tColon = slice.indexOf(':', tPos);
      int cColon = slice.indexOf(':', cPos);
      if (tColon >= 0 && cColon >= 0) {
        String tStr = slice.substring(tColon + 1, tColon + 10);
        String cStr = slice.substring(cColon + 1, cColon + 8);
        tStr.trim(); cStr.trim();
        weatherTempC = tStr.toFloat();
        weatherCondition = wmoCodeToCondition(cStr.toInt());
        weatherValid = true;
      }
    }
  }
  http.end();
}

void runUrlOtaUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    lastWebStatus = "Нет Wi-Fi для OTA";
    markDirty();
    if (gameState != ST_RECOVERY_MENU) gameState = ST_SETTINGS_MENU;
    return;
  }
  stopSongPlayback();
  stopMetronome(false);
  GameState prevState = gameState;
  gameState = ST_OTA_UPDATING;
  otaProgressPct = 5;
  lastOtaUiMs = 0;
  lastWebStatus = "Скачивание с GitHub...";
  markDirty();
  renderDisplay();

  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.onProgress([](int cur, int total) {
    if (total > 0) {
      int nextPct = constrain((int)(((long long)cur * 100LL) / (long long)total), 0, 100);
      unsigned long m = millis();
      if (nextPct != otaProgressPct && ((unsigned long)(m - lastOtaUiMs) >= 150 || nextPct == 100)) {
        otaProgressPct = nextPct;
        lastOtaUiMs = m;
        renderDisplay();
      }
    }
  });

  t_httpUpdate_return ret = httpUpdate.update(client, OTA_URL);
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      lastWebStatus = "OTA ошибка: " + httpUpdate.getLastErrorString();
      gameState = (prevState == ST_RECOVERY_MENU) ? ST_RECOVERY_MENU : ST_SETTINGS_MENU;
      markDirty();
      break;
    case HTTP_UPDATE_NO_UPDATES:
      lastWebStatus = "Обновлений нет";
      gameState = (prevState == ST_RECOVERY_MENU) ? ST_RECOVERY_MENU : ST_SETTINGS_MENU;
      markDirty();
      break;
    case HTTP_UPDATE_OK:
      otaProgressPct = 100;
      lastWebStatus = "OTA готово! Ребут...";
      gameState = ST_OTA_DONE;
      restartAtMs = millis() + 450;
      markDirty();
      renderDisplay();
      break;
  }
}

const char* stateToName(GameState st) {
  if (disabledMode) return "ОТКЛЮЧЕНО";
  if (godMode) return "РЕЖИМ БОГА";
  switch (st) {
    case ST_MAIN_MENU: return "Главное меню";
    case ST_GAME_SELECT: return "Выбор игры";
    case ST_SLOT_GAME:
    case ST_SPINNING:
    case ST_WIN_ANIM:
    case ST_LOSE_ANIM:
    case ST_NO_MONEY: return "Слоты";
    case ST_SNAKE_GAME:
    case ST_SNAKE_OVER: return "Змейка";
    case ST_TETRIS_GAME:
    case ST_TETRIS_OVER: return "Тетрис";
    case ST_BOTTLE_GAME: return "Баттл Флип";
    case ST_FLAPPY_GAME:
    case ST_FLAPPY_OVER: return "Flappy Bird";
    case ST_PONG_GAME:
    case ST_PONG_OVER: return "Понг";
    case ST_DINO_GAME:
    case ST_DINO_OVER: return "Динозаврик";
    case ST_DOODLE_GAME:
    case ST_DOODLE_OVER: return "Doodle Jump";
    case ST_RECORDS_SCREEN: return "Рекорды";
    case ST_CALCULATOR: return "Калькулятор";
    case ST_MUSIC_MENU:
    case ST_PIANO_MODE: return "Музыка / Песни";
    case ST_METRONOME: return "Музыка / Метроном";
    case ST_NOTES_LIST:
    case ST_NOTE_VIEW: return "Шпаргалки";
    case ST_RECOVERY_MENU:
    case ST_OTA_WAIT:
    case ST_OTA_UPDATING:
    case ST_OTA_DONE: return "Recovery / OTA";
    case ST_CLOCK_MENU:
    case ST_NET_CLOCK:
    case ST_STOPWATCH:
    case ST_TIMER_SET:
    case ST_TIMER_RUNNING:
    case ST_TIMER_DONE: return "Часы";
    default: return "Меню / Система";
  }
}

const char* symbolToChar(int s) {
  switch (s) {
    case 0: return "🍏";
    case 1: return "🍆";
    case 2: return "🌙";
    case 3: return "🍇";
    default: return "✖️";
  }
}

String buildNotesJsonArray() {
  String out = "[";
  for (size_t i = 0; i < notesList.size(); i++) {
    if (i > 0) out += ",";
    out += "{\"id\":" + String(i) + ",\"topic\":\"" + escapeJsonStr(notesList[i].topic) + "\",\"body\":\"" + escapeJsonStr(notesList[i].body) + "\"}";
  }
  out += "]";
  return out;
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
  out += "]";
  return out;
}

String buildStatusJson() {
  String json = "{";
  json += "\"rev\":" + String(stateRev) + ",";
  json += "\"ver\":\"" + escapeJsonStr(String(FW_VERSION)) + "\",";
  json += "\"ip\":\"" + escapeJsonStr(ipAddressStr) + "\",";
  json += "\"fs\":\"" + escapeJsonStr(fsInitMsg) + "\",";
  json += "\"state\":\"" + escapeJsonStr(String(stateToName(gameState))) + "\",";
  json += "\"god\":" + String(godMode ? "true" : "false") + ",";
  json += "\"disabled\":" + String(disabledMode ? "true" : "false") + ",";
  json += "\"spinning\":" + String(gameState == ST_SPINNING ? "true" : "false") + ",";
  json += "\"coins\":" + String(coins) + ",";
  json += "\"totalSpins\":" + String(totalSpins) + ",";
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
  json += "}";
  return json;
}

void handleRootWeb() { server.send_P(200, "text/html", WEB_PAGE_HTML); }
void handleStatusWeb() { server.send(200, "application/json", buildStatusJson()); }

void handleGodWeb() {
  wakeScreen();
  applyGodMode(!godMode);
  server.send(200, "application/json", buildStatusJson());
}

void handleDisableWeb() {
  wakeScreen();
  applyDisabledMode(!disabledMode);
  server.send(200, "application/json", buildStatusJson());
}

void handleRecordsSetWeb() {
  if (!godMode) {
    lastWebStatus = "Ошибка: рекорды доступны в режиме бога";
    markDirty();
    server.send(200, "application/json", buildStatusJson());
    return;
  }
  if (server.hasArg("snake"))  snakeRecord  = constrain(server.arg("snake").toInt(),  0, 99999);
  if (server.hasArg("tetris")) tetrisRecord = constrain(server.arg("tetris").toInt(), 0, 99999);
  if (server.hasArg("flappy")) flappyRecord = constrain(server.arg("flappy").toInt(), 0, 99999);
  if (server.hasArg("pong"))   pongRecord   = constrain(server.arg("pong").toInt(),   0, 99999);
  if (server.hasArg("dino"))   dinoRecord   = constrain(server.arg("dino").toInt(),   0, 99999);
  if (server.hasArg("bottle")) bottleRecord = constrain(server.arg("bottle").toInt(), 0, 99999);
  if (server.hasArg("doodle")) doodleRecord = constrain(server.arg("doodle").toInt(), 0, 99999);
  saveGameData();
  beep(1250, 50);
  lastWebStatus = "Рекорды сохранены на консоль";
  markDirty();
  server.send(200, "application/json", buildStatusJson());
}

void handlePianoWeb() {
  if (disabledMode) { lastWebStatus = "Приставка отключена"; server.send(200, "application/json", buildStatusJson()); return; }
  int dur = server.hasArg("dur") ? constrain(server.arg("dur").toInt(), 40, 2500) : 260;
  if (server.hasArg("pause") && server.arg("pause") == "1") { triggerPauseStep(dur); lastWebStatus = "Пианино: Пауза"; }
  else if (server.hasArg("midi")) { playMidiNote(constrain(server.arg("midi").toInt(), 36, 96), dur); lastWebStatus = "Пианино: " + lastPlayedNoteName; }
  else {
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
  String title = sanitizeNoteText(server.hasArg("title") ? server.arg("title") : "");
  title.trim();
  String seq = server.hasArg("seq") ? server.arg("seq") : "";
  bool playAfter = server.hasArg("play") && server.arg("play") == "1";
  if (title.length() == 0) { lastWebStatus = "Ошибка: укажи название песни"; markDirty(); server.send(200, "application/json", buildStatusJson()); return; }

  std::vector<SongStep> steps;
  int pos = 0;
  while (pos <= (int)seq.length() && steps.size() < MAX_SONG_STEPS) {
    int comma = seq.indexOf(',', pos);
    String token = (comma >= 0) ? seq.substring(pos, comma) : seq.substring(pos);
    token.trim();
    int colon = token.indexOf(':');
    if (colon > 0) {
      int fr = token.substring(0, colon).toInt();
      int du = token.substring(colon + 1).toInt();
      if (fr >= 0 && fr <= 4200 && du >= 30 && du <= 5000) steps.push_back({fr, du});
    }
    if (comma < 0) break;
    pos = comma + 1;
  }

  if (steps.empty()) lastWebStatus = "Ошибка: добавь хотя бы одну ноту или паузу";
  else {
    String errMsg;
    int savedIdx = addOrUpdateSong(title, steps, &errMsg);
    if (savedIdx < 0) lastWebStatus = "Ошибка сохранения: " + errMsg;
    else if (playAfter) { disabledMode = false; wakeScreen(); gameState = ST_PIANO_MODE; startSongPlayback(savedIdx); lastWebStatus = "Сохранено в /songs.json и играет: " + title; }
    else { beep(1200, 50); lastWebStatus = "Сохранено в /songs.json: " + title; }
  }
  markDirty();
  server.send(200, "application/json", buildStatusJson());
}

void handleSongsImportWeb() {
  String rawJson = server.hasArg("json") ? server.arg("json") : (server.hasArg("plain") ? server.arg("plain") : "");
  String errMsg;
  int count = importSongsFromJson(rawJson, &errMsg);
  if (count <= 0) lastWebStatus = "Ошибка импорта: " + (errMsg.length() > 0 ? errMsg : String("некорректный JSON"));
  else { beep(1250, 60); lastWebStatus = "Импортировано песен: " + String(count); }
  markDirty();
  server.send(200, "application/json", buildStatusJson());
}

void handleSongsPlayWeb() {
  if (disabledMode) { lastWebStatus = "Приставка отключена"; server.send(200, "application/json", buildStatusJson()); return; }
  if (server.hasArg("idx")) {
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
    String m = server.arg("m");
    wakeScreen();
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
    else if (m == "records") { gameState = ST_RECORDS_SCREEN; lastWebStatus = "Открыты Рекорды"; }
    else if (m == "clock")   { syncNtpTime(false); fetchWeatherIfNeeded(false); gameState = ST_NET_CLOCK; lastWebStatus = "Открыты Часы и погода"; }
    else if (m == "music")   { gameState = ST_MUSIC_MENU; menuCursor = 0; lastWebStatus = "Открыто меню Музыка"; }
    else if (m == "metro")   { metroRunning = true; metroBeat = 0; nextMetroMs = millis(); gameState = ST_METRONOME; lastWebStatus = "Включен Метроном"; }
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
  else lastWebStatus = "Пустая тема шпаргалки";
  markDirty();
  server.send(200, "application/json", buildStatusJson());
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
    else { lastWebStatus = "Некорректное число монет"; markDirty(); }
  }
  server.send(200, "application/json", buildStatusJson());
}

void handleSpinWeb() {
  if (disabledMode) { lastWebStatus = "Приставка отключена"; server.send(200, "application/json", buildStatusJson()); return; }
  wakeScreen(); stopSongPlayback(); stopMetronome(false); gameState = ST_SLOT_GAME;
  long costNow = godMode ? 0L : FIXED_SPIN_COST;
  if (!godMode && coins < costNow) { autoSpin = false; gameState = ST_NO_MONEY; stateTimer = millis(); lastWebStatus = "Не хватает монет"; }
  else { startSpin(); lastWebStatus = "Крутится спин на приставке..."; }
  markDirty();
  server.send(200, "application/json", buildStatusJson());
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
  setupWebServerRoutes();
  server.begin(WEB_PORT);
  webServerStarted = true;
  lastWebStatus = "Веб-панель запущена по запросу";
  markDirty();
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
  (void)now;
  server.handleClient();
}

unsigned long getCurrentStopwatchMs() {
  return stopwatchRunning ? (stopwatchElapsed + (millis() - stopwatchStart)) : stopwatchElapsed;
}

unsigned long getCurrentTimerRemainingMs() {
  if (!timerRunning) return timerRemainingMs;
  unsigned long now = millis();
  return ((unsigned long)(now - timerTargetMs) < 0x80000000UL && now >= timerTargetMs) ? 0 : (timerTargetMs - now);
}

void adjustTimerField(int delta) {
  if (timerFieldCursor == 0) timerHoursSet = (timerHoursSet + delta + 24) % 24;
  else if (timerFieldCursor == 1) timerMinutesSet = (timerMinutesSet + delta + 60) % 60;
  else timerSecondsSet = (timerSecondsSet + delta + 60) % 60;
}

void resetSaveData() {
  godMode = false; disabledMode = false;
  coins = 150; displayCoins = 150; totalSpins = 0; maxWin = 0;
  flappyRecord = 0; tetrisRecord = 0; snakeRecord = 0; bottleRecord = 0; pongRecord = 0; dinoRecord = 0; doodleRecord = 0;
  for (int i = 0; i < ACH_COUNT; i++) achievements[i].unlocked = false;
  saveGameData();
  markDirty();
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

    for (int i = 0; i < 32; i++) currentSsid[i] = EEPROM.read(ADDR_SSID + i);
    for (int i = 0; i < 32; i++) currentPass[i] = EEPROM.read(ADDR_PASS + i);
    currentSsid[31] = '\0'; currentPass[31] = '\0';

    if (coins < 0 || (coins > MAX_COINS && coins != GOD_COINS_VAL)) coins = 150;
    if (coins == GOD_COINS_VAL) godMode = true;
    if (flappyRecord < 0 || flappyRecord > 99999) flappyRecord = 0;
    if (tetrisRecord < 0 || tetrisRecord > 99999) tetrisRecord = 0;
    if (snakeRecord < 0 || snakeRecord > 99999) snakeRecord = 0;
    if (bottleRecord < 0 || bottleRecord > 99999) bottleRecord = 0;
    if (pongRecord < 0 || pongRecord > 99999) pongRecord = 0;
    if (dinoRecord < 0 || dinoRecord > 99999) dinoRecord = 0;
    if (doodleRecord < 0 || doodleRecord > 99999) doodleRecord = 0;
  } else {
    coins = 150; totalSpins = 0; maxWin = 0;
    flappyRecord = 0; tetrisRecord = 0; snakeRecord = 0; bottleRecord = 0; pongRecord = 0; dinoRecord = 0; doodleRecord = 0;
    for (int i = 0; i < ACH_COUNT; i++) achievements[i].unlocked = false;
    saveGameData();
  }
  displayCoins = coins;
}

void saveGameData() {
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
  WiFi.setTxPower(WIFI_POWER_11dBm);
  WiFi.setAutoReconnect(true);
  if (strlen(currentSsid) > 0) WiFi.begin(currentSsid, currentPass);

  bool recoveryBoot = (digitalRead(BTN_SELECT_PIN) == LOW);
  gameState = recoveryBoot ? ST_RECOVERY_MENU : ST_SPLASH;
  menuCursor = 0;
  prevObservedState = gameState;
  stateTimer = millis();
  idleTimer = millis();
  lastFrameTime = millis();
  markDirty();
}

void loop() {
  unsigned long now = millis();

  if (restartAtMs > 0 && (unsigned long)(now - restartAtMs) < 0x80000000UL && now >= restartAtMs) ESP.restart();

  pollWifiScanAsync(now);
  updateToneSeq(now);
  updateSongPlayback(now);
  updateMetronome(now);
  handleButtons();

  now = millis();
  serviceWebServer(now);

  if (disabledMode) {
    if (gameState != prevObservedState) { prevObservedState = gameState; markDirty(); }
    if ((unsigned long)(now - lastFrameTime) >= 40) { lastFrameTime = now; renderDisplay(); }
    yield();
    return;
  }

  if (godMode && coins != GOD_COINS_VAL) { coins = GOD_COINS_VAL; displayCoins = GOD_COINS_VAL; }
  if (WiFi.status() == WL_CONNECTED) {
    ipAddressStr = WiFi.localIP().toString();
    if (!ntpConfigured && gameState == ST_NET_CLOCK) syncNtpTime(false);
  } else {
    ipAddressStr = "ОФФЛАЙН";
  }
  if (coins >= 1000) unlockAchievement(3);

  if (timerRunning) {
    if ((unsigned long)(now - timerTargetMs) < 0x80000000UL && now >= timerTargetMs) {
      timerRunning = false; timerRemainingMs = 0; gameState = ST_TIMER_DONE; stateTimer = now;
      timerAlarmStep = 1; timerAlarmTime = now;
      if (soundEnabled) tone(BUZZER_PIN, 1000, 180);
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

  if (gameState == ST_SPLASH && ((unsigned long)(now - stateTimer) > 1600)) {
    gameState = ST_MAIN_MENU; menuCursor = 0;
  }

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
  if (gameState == ST_BOTTLE_GAME && bottleState == 2 && ((unsigned long)(now - stateTimer) > 1500)) {
    bottleState = 0;
    bottleLanded = false;
    markDirty();
  }

  if (gameState == ST_TEST_MELODY) {
    if (!testMelodyStarted) {
      testMelodyStarted = true;
      if (soundEnabled) playJingleBells();
      else { testMelodyStarted = false; gameState = ST_TEST_MODE; }
    }
  } else {
    testMelodyStarted = false;
  }

  if (gameState == ST_WIFI_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      ipAddressStr = WiFi.localIP().toString();
      syncNtpTime(true);
      fetchWeatherIfNeeded(true);
      lastWebStatus = "Wi-Fi подключён: " + String(currentSsid);
      gameState = ST_WIFI_CONNECTED_INFO;
      ignoreNextIpDismiss = true;
    } else if ((unsigned long)(now - stateTimer) > 8000) {
      WiFi.disconnect();
      lastWebStatus = "Ошибка подключения к Wi-Fi";
      gameState = ST_WIFI_ERROR;
      stateTimer = now;
    }
  }

  if (gameState == ST_WIFI_ERROR && ((unsigned long)(now - stateTimer) > 2200)) {
    gameState = ST_SETTINGS_MENU; menuCursor = 0;
  }

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

  bool l = (digitalRead(BTN_LEFT_PIN) == LOW);
  bool r = (digitalRead(BTN_RIGHT_PIN) == LOW);
  if (l && !r) doodleVX = -2.8f;
  else if (r && !l) doodleVX = 2.8f;
  else doodleVX *= 0.72f;

  doodleX += doodleVX;
  if (doodleX < -6.0f) doodleX = 126.0f;
  if (doodleX > 128.0f) doodleX = -4.0f;

  float prevBottom = doodleY + 8.0f;
  doodleVY += DOODLE_GRAV;
  doodleY += doodleVY;
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
    float shift = 24.0f - doodleY;
    doodleY = 24.0f;
    doodleScore += max(1, (int)roundf(shift));
    if (doodleScore > doodleRecord) { doodleRecord = doodleScore; if (!godMode) saveGameData(); }
    for (int i = 0; i < DOODLE_PLAT_COUNT; i++) {
      doodlePlats[i].y += shift;
      if (doodlePlats[i].y > 64.0f) {
        uint8_t pw = (uint8_t)random(22, 34);
        doodlePlats[i].w = pw;
        doodlePlats[i].x = (float)random(2, 128 - pw - 2);
        doodlePlats[i].y -= 66.0f;
      }
    }
  }

  if (doodleY > 64.0f) {
    if (godMode) { doodleY = 44.0f; doodleVY = DOODLE_JUMP_V * 1.15f; return; }
    beep(230, 220);
    if (doodleScore > doodleRecord) { doodleRecord = doodleScore; saveGameData(); }
    gameState = ST_DOODLE_OVER; stateTimer = now; markDirty();
  }
}

void spawnCactus() {
  cactusX = 128.0f + (float)random(0, 24);
  int t = random(0, 3);
  if (t == 0) { cactusW = 5; cactusH = 10; }
  else if (t == 1) { cactusW = 7; cactusH = 14; }
  else { cactusW = 10; cactusH = 11; }
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
    float groundTop = DINO_GROUND_Y - 12;
    if (dinoY >= groundTop) { dinoY = groundTop; dinoVY = 0.0f; dinoJumping = false; }
  } else {
    dinoY = dinoDucking ? (DINO_GROUND_Y - 7) : (DINO_GROUND_Y - 12);
  }

  cactusX -= dinoSpeed;
  if (cactusX < -cactusW) {
    dinoScore++; dinoSpeed = min(5.4f, 2.6f + dinoScore * 0.06f);
    if (dinoScore > dinoRecord) { dinoRecord = dinoScore; if (!godMode) saveGameData(); }
    beep(1150, 25); spawnCactus();
  }

  int dW = dinoDucking ? 10 : 8;
  int dH = dinoDucking ? 7 : 12;
  if ((DINO_X + dW - 1 >= (int)cactusX) && (DINO_X + 1 <= (int)cactusX + cactusW) && ((int)dinoY + dH - 1 >= DINO_GROUND_Y - cactusH)) {
    if (godMode) { if (!dinoJumping) { dinoJumping = true; dinoVY = DINO_JUMP_V; } return; }
    beep(220, 240);
    if (dinoScore > dinoRecord) { dinoRecord = dinoScore; saveGameData(); }
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
    pongScore++;
    if (pongScore > pongRecord) { pongRecord = pongScore; if (!godMode) saveGameData(); }
    beep(1200, 30);
  }

  if (ballY > 64) {
    if (godMode) { ballY = paddleY - 3; ballVY = -fabsf(ballVY); pongScore++; if (pongScore > pongRecord) pongRecord = pongScore; beep(1200, 30); return; }
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

  flappyVelocity += FLAPPY_GRAVITY;
  if (flappyVelocity > 3.6f) flappyVelocity = 3.6f;
  flappyY += flappyVelocity;
  pipeX -= 2.2f;
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
    beep(200, 300);
    if (tetrisScore > tetrisRecord) { tetrisRecord = tetrisScore; saveGameData(); }
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
  unsigned long dropInt = (rawInt < 100L) ? 100UL : (unsigned long)rawInt;
  if ((unsigned long)(now - tetrisTimer) < dropInt) return;
  tetrisTimer = now;
  if (!checkTetrisCollision(tetrisX, tetrisY + 1, tetrisCurrRot)) tetrisY++;
  else { mergeTetrisPiece(); clearTetrisLines(); spawnTetrisPiece(); }
}

void startSnake() {
  snakeLen = 3; snake[0] = {8, 4}; snake[1] = {7, 4}; snake[2] = {6, 4};
  dx = 1; dy = 0; snakeScore = 0;
  spawnFood(); gameState = ST_SNAKE_GAME; snakeTimer = millis(); markDirty();
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
    autoSpin = false;
    gameState = ST_NO_MONEY;
    stateTimer = now;
    lastWebStatus = "Не хватает монет";
    beep(140, 160);
    markDirty();
    return;
  }

  beep(560, 25);
  if (!godMode) coins -= costNow;
  totalSpins++;
  unlockAchievement(0);
  if (totalSpins >= 50) unlockAchievement(1);
  if (!godMode) saveGameData();

  if (godMode || random(0, 100) < 24) {
    int sym = godMode ? 1 : ((random(0, 100) < 55) ? 1 : random(0, ICON_COUNT));
    targetSlot1 = sym;
    targetSlot2 = sym;
    targetSlot3 = sym;
  } else {
    targetSlot1 = random(0, ICON_COUNT);
    targetSlot2 = random(0, ICON_COUNT);
    targetSlot3 = random(0, ICON_COUNT);
    if (targetSlot1 == targetSlot2 && targetSlot2 == targetSlot3) {
      targetSlot3 = (targetSlot3 + 1) % ICON_COUNT;
    }
  }

  gameState = ST_SPINNING;
  spinStartTime = now;
  spinLastTickMs = now;
  spinResolved = false;

  reel1Stopped = false;
  reel2Stopped = false;
  reel3Stopped = false;

  reelOffset1 = 0.0f;
  reelOffset2 = 0.0f;
  reelOffset3 = 0.0f;

  bounce1 = 0.0f;
  bounce2 = 0.0f;
  bounce3 = 0.0f;

  markDirty();
}

void handleSpinning() {
  unsigned long now = millis();
  unsigned long elapsed = now - spinStartTime;
  const float stepPitch = 24.0f;

  if (elapsed < 750) {
    reelOffset1 += 5.5f;
    if (reelOffset1 >= stepPitch) {
      reelOffset1 -= stepPitch;
      prevSlot1 = slot1;
      slot1 = (slot1 + 1 + random(0, ICON_COUNT - 1)) % ICON_COUNT;
    }
  } else if (!reel1Stopped) {
    reel1Stopped = true;
    reelOffset1 = 0.0f;
    bounce1 = 1.5f;
    slot1 = targetSlot1;
    prevSlot1 = targetSlot1;
    beep(660, 18);
  }

  if (elapsed < 1150) {
    reelOffset2 += 6.0f;
    if (reelOffset2 >= stepPitch) {
      reelOffset2 -= stepPitch;
      prevSlot2 = slot2;
      slot2 = (slot2 + 1 + random(0, ICON_COUNT - 1)) % ICON_COUNT;
    }
  } else if (!reel2Stopped) {
    reel2Stopped = true;
    reelOffset2 = 0.0f;
    bounce2 = 1.5f;
    slot2 = targetSlot2;
    prevSlot2 = targetSlot2;
    beep(780, 18);
  }

  if (elapsed < 1550) {
    reelOffset3 += 6.5f;
    if (reelOffset3 >= stepPitch) {
      reelOffset3 -= stepPitch;
      prevSlot3 = slot3;
      slot3 = (slot3 + 1 + random(0, ICON_COUNT - 1)) % ICON_COUNT;
    }
  } else if (!reel3Stopped) {
    reel3Stopped = true;
    reelOffset3 = 0.0f;
    bounce3 = 1.5f;
    slot3 = targetSlot3;
    prevSlot3 = targetSlot3;
    beep(920, 22);
  }

  if (bounce1 > 0.2f) bounce1 *= 0.5f; else bounce1 = 0.0f;
  if (bounce2 > 0.2f) bounce2 *= 0.5f; else bounce2 = 0.0f;
  if (bounce3 > 0.2f) bounce3 *= 0.5f; else bounce3 = 0.0f;

  if (reel1Stopped && reel2Stopped && reel3Stopped && elapsed >= 1620 && !spinResolved) {
    spinResolved = true;
    slot1 = targetSlot1;
    slot2 = targetSlot2;
    slot3 = targetSlot3;
    checkWin();
  }
}

void checkWin() {
  slot1 = targetSlot1;
  slot2 = targetSlot2;
  slot3 = targetSlot3;

  if (slot1 == slot2 && slot2 == slot3) {
    int mult = (slot1 == 1) ? 50 : (slot1 == 3) ? 25 : (slot1 == 4) ? 20 : (slot1 == 2) ? 15 : 6;
    currentWin = FIXED_SPIN_COST * mult;
    if (godMode) {
      coins = GOD_COINS_VAL;
      displayCoins = GOD_COINS_VAL;
    } else {
      coins += currentWin;
      if (coins > MAX_COINS) coins = MAX_COINS;
    }
    if (currentWin > maxWin) maxWin = currentWin;
    if (slot1 == 1) unlockAchievement(2);
    if (!godMode) saveGameData();
    lastWebStatus = (slot1 == 1) ? ("ДЖЕКПОТ! +" + String(currentWin)) : ("Победа! Куш +" + String(currentWin));
    if (slot1 == 1) playJackpotSound(); else playWinSound();
    gameState = ST_WIN_ANIM;
  } else {
    currentWin = 0;
    lastWebStatus = "Не повезло в этом спине";
    playLoseSound();
    gameState = ST_LOSE_ANIM;
  }
  stateTimer = millis();
  markDirty();
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
    wakeScreen();
    upPressed = u; downPressed = d; leftPressed = l; rightPressed = r; selectPressed = s;
    selectPressStartMs = s ? now : 0; selectLongPressHandled = false; lastBtnActionMs = now;
    beep(900, 30); return;
  }

  if (gameState == ST_OTA_UPDATING || gameState == ST_OTA_DONE || gameState == ST_WIFI_SCAN) {
    upPressed = u; downPressed = d; leftPressed = l; rightPressed = r; selectPressed = s; return;
  }

  if (s && !selectPressed) { selectPressStartMs = now; selectLongPressHandled = false; }
  else if (!s) { selectPressStartMs = 0; selectLongPressHandled = false; }

  if (gameState == ST_CALCULATOR && s && !selectLongPressHandled && ((unsigned long)(now - selectPressStartMs) >= LONG_PRESS_MS)) {
    selectLongPressHandled = true; lastBtnActionMs = now; calcExpr[0] = '\0'; calcHasResult = false; calcError = false; beep(600, 50);
  }

  if (gameState == ST_METRONOME && s && !selectLongPressHandled && ((unsigned long)(now - selectPressStartMs) >= LONG_PRESS_MS)) {
    selectLongPressHandled = true; selectPressed = true; lastBtnActionMs = now; stopMetronome(true); beep(500, 45);
    upPressed = u; downPressed = d; leftPressed = l; rightPressed = r; return;
  }

  bool canTrigger = ((unsigned long)(now - lastBtnActionMs) >= BTN_COOLDOWN_MS);

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
    if (canTrigger && ((unsigned long)(now - testExitLockUntil) >= 0x80000000UL || now >= testExitLockUntil) && s && u) { lastBtnActionMs = now; beep(1000, 50); gameState = ST_SETTINGS_MENU; menuCursor = 4; testExitLockUntil = now + 300; }
    upPressed = u; downPressed = d; leftPressed = l; rightPressed = r; selectPressed = s; return;
  }

  if (gameState == ST_TEST_CONFIRM) {
    if (canTrigger && s && !selectPressed) { selectPressed = true; lastBtnActionMs = now; gameState = ST_TEST_MELODY; } else if (!s) selectPressed = false;
    if (canTrigger && u && !upPressed) { upPressed = true; lastBtnActionMs = now; gameState = ST_SETTINGS_MENU; menuCursor = 4; } else if (!u) upPressed = false;
    downPressed = d; leftPressed = l; rightPressed = r; return;
  }

  if (gameState == ST_WIFI_CONNECTED_INFO) {
    if (!u && !d && !l && !r && !s) ignoreNextIpDismiss = false;
    if (canTrigger && !ignoreNextIpDismiss && (u || d || l || r || s)) {
      lastBtnActionMs = now; beep(800, 30); gameState = ST_WIFI_MANAGEMENT; menuCursor = 1;
    }
    upPressed = u; downPressed = d; leftPressed = l; rightPressed = r; selectPressed = s;
    return;
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
      if (timerRunning) { timerRemainingMs = ((unsigned long)(now - timerTargetMs) < 0x80000000UL && now >= timerTargetMs) ? 0 : (timerTargetMs - now); timerRunning = false; }
      else { timerRemainingMs = 0; gameState = ST_TIMER_SET; }
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
    } else if (gameState == ST_NET_CLOCK || gameState == ST_STOPWATCH || gameState == ST_TIMER_RUNNING) {
      int curIdx = (gameState == ST_NET_CLOCK) ? 0 : (gameState == ST_STOPWATCH) ? 1 : 2;
      if (gameState == ST_TIMER_RUNNING && timerRunning) { timerRemainingMs = ((unsigned long)(now - timerTargetMs) < 0x80000000UL && now >= timerTargetMs) ? 0 : (timerTargetMs - now); timerRunning = false; }
      gameState = ST_CLOCK_MENU; menuCursor = curIdx; beep(500, 40);
    } else if (gameState == ST_SLOT_GAME) {
      gameState = ST_GAME_SELECT; menuCursor = 0; beep(500, 40);
    } else if (gameState == ST_BOTTLE_GAME) {
      bottleState = 0; bottleLanded = false; gameState = ST_GAME_SELECT; menuCursor = 3; beep(500, 40);
    } else if (gameState == ST_FLAPPY_GAME || gameState == ST_FLAPPY_OVER) {
      gameState = ST_GAME_SELECT; menuCursor = 4; beep(500, 40);
    } else if (gameState == ST_DINO_GAME || gameState == ST_DINO_OVER) {
      gameState = ST_GAME_SELECT; menuCursor = 6; beep(500, 40);
    } else if (gameState == ST_SNAKE_OVER || gameState == ST_TETRIS_OVER ||
               gameState == ST_PONG_OVER || gameState == ST_DOODLE_OVER) {
      gameState = ST_GAME_SELECT; menuCursor = 0; beep(500, 40);
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
    } else if (gameState == ST_MUSIC_MENU) {
      beep(900, 50);
      if (menuCursor == 0) { if (pianoSongCursor >= (int)songsList.size()) pianoSongCursor = 0; gameState = ST_PIANO_MODE; }
      else if (menuCursor == 1) { metroRunning = true; metroBeat = 0; nextMetroMs = now; gameState = ST_METRONOME; }
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
      metroRunning = !metroRunning; metroBeat = 0; nextMetroMs = now;
      if (!metroRunning) noTone(BUZZER_PIN);
      beep(1050, 35); markDirty();
    } else if (gameState == ST_RECOVERY_MENU) {
      beep(950, 50);
      if (menuCursor == 0) runUrlOtaUpdate();
      else if (menuCursor == 1) { otaArmed = true; gameState = ST_OTA_WAIT; }
      else if (menuCursor == 2) { resetSaveData(); beep(1250, 120); lastWebStatus = "Сохранения сброшены в Recovery"; markDirty(); }
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
      if (timerRunning) { timerRemainingMs = ((unsigned long)(now - timerTargetMs) < 0x80000000UL && now >= timerTargetMs) ? 0 : (timerTargetMs - now); timerRunning = false; }
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
      else { gameState = ST_MAIN_MENU; menuCursor = 0; }
    } else if (gameState == ST_BOTTLE_GAME) {
      if (bottleState == 0) {
        bottleState = 1;
        bottleLanded = false;
        bottleAnimTime = now;
        beep(760, 45);
        markDirty();
      } else if (bottleState == 2) {
        bottleState = 0;
        bottleLanded = false;
        beep(650, 25);
        markDirty();
      }
    } else if (gameState == ST_FLAPPY_GAME) {
      flappyVelocity = FLAPPY_JUMP;
      beep(640, 25);
    } else if (gameState == ST_FLAPPY_OVER) {
      startFlappy();
      beep(800, 30);
    } else if (gameState == ST_DINO_GAME) {
      if (!dinoJumping) {
        dinoJumping = true;
        dinoVY = DINO_JUMP_V;
        beep(980, 30);
      }
    } else if (gameState == ST_DINO_OVER) {
      startDino();
      beep(800, 30);
    } else if (gameState == ST_SNAKE_GAME || gameState == ST_SNAKE_OVER) {
      gameState = ST_GAME_SELECT; menuCursor = 1; beep(700, 40);
    } else if (gameState == ST_TETRIS_GAME || gameState == ST_TETRIS_OVER ||
               gameState == ST_PONG_GAME || gameState == ST_PONG_OVER || gameState == ST_DOODLE_GAME || gameState == ST_DOODLE_OVER) {
      gameState = ST_GAME_SELECT; menuCursor = 0; beep(700, 40);
    } else if (gameState == ST_COIN_KEYBOARD) {
      beep(1100, 40);
      char ch = NUM_KB_LAYOUT[kbRow][kbCol];
      int len = strlen(inputCoinStr);
      if (ch == 'O') {
        long val = atol(inputCoinStr);
        if (val > 0 && !godMode) { coins += val; if (coins > MAX_COINS) coins = MAX_COINS; saveGameData(); markDirty(); }
        inputCoinStr[0] = '\0'; gameState = ST_SETTINGS_MENU; menuCursor = 2;
      } else if (ch == '<') {
        if (len > 0) inputCoinStr[len - 1] = '\0';
      } else if (ch == 'X') {
        inputCoinStr[0] = '\0'; gameState = ST_SETTINGS_MENU; menuCursor = 2;
      } else if (len < 6 && ch >= '0' && ch <= '9') {
        inputCoinStr[len] = ch; inputCoinStr[len + 1] = '\0';
      }
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
      if (menuCursor == 0 || menuCursor == 3) scanWifiNetworksSafe();
      else if (menuCursor == 1) { ipAddressStr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "ОФФЛАЙН"; ignoreNextIpDismiss = true; gameState = ST_WIFI_CONNECTED_INFO; }
      else if (menuCursor == 2) {
        if (WiFi.status() == WL_CONNECTED) { WiFi.disconnect(); currentSsid[0] = '\0'; currentPass[0] = '\0'; ipAddressStr = "ОФФЛАЙН"; ntpConfigured = false; weatherValid = false; saveGameData(); beep(400, 150); markDirty(); }
      } else if (menuCursor == 4) { gameState = ST_SETTINGS_MENU; menuCursor = 0; }
    } else if (gameState == ST_WIFI_LIST && wifiNetCount > 0) {
      WiFi.SSID(menuCursor).toCharArray(targetSsid, 32); inputPassword[0] = '\0'; kbRow = 0; kbCol = 0; gameState = ST_WIFI_KEYBOARD; beep(1000, 50);
    } else if (gameState == ST_WIFI_KEYBOARD) {
      beep(1100, 40);
      if (kbRow == 4 && kbCol == 0) { int len = strlen(inputPassword); if (len > 0) inputPassword[len - 1] = '\0'; }
      else if (kbRow == 4 && kbCol == 1) { int len = strlen(inputPassword); if (len < 31) { inputPassword[len] = ' '; inputPassword[len + 1] = '\0'; } }
      else if (kbRow == 4 && kbCol == 2) { strcpy(currentSsid, targetSsid); strcpy(currentPass, inputPassword); saveGameData(); ntpConfigured = false; WiFi.mode(WIFI_STA); WiFi.setTxPower(WIFI_POWER_11dBm); WiFi.begin(currentSsid, currentPass); stateTimer = now; gameState = ST_WIFI_CONNECTING; }
      else if (kbRow == 4 && kbCol == 5) gameState = ST_WIFI_LIST;
      else if (!(kbRow == 4 && (kbCol == 3 || kbCol == 4))) { char ch = KB_LAYOUT[kbRow][kbCol]; int len = strlen(inputPassword); if (len < 31) { inputPassword[len] = ch; inputPassword[len + 1] = '\0'; } }
    } else if (gameState == ST_SLOT_GAME) {
      startSpin();
    }
  } else if (!s) selectPressed = false;
}

void drawHeaderBar(const char* title, const char* badge) {
  u8g2.setFont(u8g2_font_6x12_t_cyrillic);
  u8g2.drawRBox(0, 0, 128, 13, 3);
  u8g2.drawHLine(0, 14, 128);
  u8g2.setDrawColor(0);
  u8g2.drawUTF8(6, 10, title);
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

void drawMenuRow(int idx, int curIdx, int yPos, const char* label) {
  u8g2.setFont(u8g2_font_6x12_t_cyrillic);
  bool sel = (idx == curIdx);
  if (sel) {
    u8g2.drawRBox(2, yPos - 1, 120, 11, 3);
    u8g2.setDrawColor(0);
    u8g2.drawUTF8(6, yPos + 8, ">");
    u8g2.drawUTF8(15, yPos + 8, label);
    u8g2.setDrawColor(1);
  } else {
    u8g2.drawUTF8(10, yPos + 8, label);
  }
}

void drawScrollIndicator(int curIdx, int totalItems) {
  if (totalItems <= 1) return;
  const int trackTop = 16;
  const int trackHeight = 45;
  u8g2.drawVLine(125, trackTop, trackHeight);
  int thumbH = max(6, trackHeight / totalItems);
  int thumbY = trackTop + (curIdx * (trackHeight - thumbH)) / (totalItems - 1);
  u8g2.drawRBox(124, thumbY, 3, thumbH, 1);
}

void drawBottleGraphic(int cx, int cy, float angle) {
  float cosA = cosf(angle);
  float sinA = sinf(angle);
  auto rotPt = [&](float lx, float ly, int &ox, int &oy) {
    ox = cx + (int)roundf(lx * cosA - ly * sinA);
    oy = cy + (int)roundf(lx * sinA + ly * cosA);
  };
  auto rotLine = [&](float x1, float y1, float x2, float y2) {
    int ax, ay, bx, by;
    rotPt(x1, y1, ax, ay);
    rotPt(x2, y2, bx, by);
    u8g2.drawLine(ax, ay, bx, by);
  };

  rotLine(-2.0f, -12.0f,  2.0f, -12.0f);
  rotLine(-2.0f, -10.0f,  2.0f, -10.0f);
  rotLine(-2.0f, -12.0f, -2.0f, -5.0f);
  rotLine( 2.0f, -12.0f,  2.0f, -5.0f);

  rotLine(-2.0f, -5.0f,  -5.0f, -2.0f);
  rotLine( 2.0f, -5.0f,   5.0f, -2.0f);

  rotLine(-5.0f, -2.0f,  -5.0f, 11.0f);
  rotLine( 5.0f, -2.0f,   5.0f, 11.0f);
  rotLine(-5.0f, 11.0f,   5.0f, 11.0f);

  rotLine(-4.0f,  2.0f,   4.0f,  2.0f);
  rotLine(-4.0f,  6.0f,   4.0f,  6.0f);
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
    u8g2.drawRFrame(4, 6, 120, 52, 5);
    u8g2.drawRFrame(7, 9, 114, 46, 3);
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(26, 26, "ESP32 CONSOLE");
    int barStep = (int)((nowMs / 120) % 5);
    for (int i = 0; i < 5; i++) {
      int bx = 28 + i * 15;
      if (i <= barStep) u8g2.drawRBox(bx, 34, 11, 7, 2);
      else u8g2.drawRFrame(bx, 34, 11, 7, 2);
    }
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(42, 50, FW_VERSION);
  } else if (gameState == ST_RECOVERY_MENU) {
    drawHeaderBar("RECOVERY", FW_VERSION);
    const char* rItems[4] = {"1. OTA по ссылке", "2. OTA из браузера", "3. Сброс сейвов", "4. Инфо"};
    for (int i = 0; i < 4; i++) drawMenuRow(i, menuCursor, 16 + i * 12, rItems[i]);
    drawScrollIndicator(menuCursor, 4);
  } else if (gameState == ST_OTA_WAIT) {
    drawHeaderBar("OTA WEB", "WAIT");
    u8g2.drawUTF8(6, 25, "Адрес в браузере:");
    u8g2.drawRFrame(4, 29, 120, 15, 3);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(9, 39, (WiFi.status() == WL_CONNECTED) ? (String("http://") + WiFi.localIP().toString() + "/update").c_str() : "Wi-Fi offline");
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(6, 60, "< Назад");
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
    int startIdx = constrain(menuCursor - 2, 0, 5);
    for (int i = 0; i < 4 && (startIdx + i) < 9; i++) {
      int idx = startIdx + i;
      drawMenuRow(idx, menuCursor, 16 + i * 12, items[idx]);
    }
    drawScrollIndicator(menuCursor, 9);
  } else if (gameState == ST_MUSIC_MENU) {
    drawHeaderBar("МУЗЫКА", "AUDIO");
    const char* mItems[3] = {"1. Песни", "2. Метроном", "3. Назад"};
    for (int i = 0; i < 3; i++) drawMenuRow(i, menuCursor, 18 + i * 13, mItems[i]);
    drawScrollIndicator(menuCursor, 3);
  } else if (gameState == ST_RECORDS_SCREEN) {
    drawHeaderBar("РЕКОРДЫ", "TOP");
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(6, 23, (String("SNAKE: ") + snakeRecord + "   TETRIS: " + tetrisRecord).c_str());
    u8g2.drawStr(6, 32, (String("FLAPPY: ") + flappyRecord + "  PONG:   " + pongRecord).c_str());
    u8g2.drawStr(6, 41, (String("DINO: ") + dinoRecord + "    FLIP:   " + bottleRecord).c_str());
    u8g2.drawStr(6, 50, (String("DOODLE: ") + doodleRecord).c_str());
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(6, 62, "OK или < Назад");
  } else if (gameState == ST_PIANO_MODE) {
    drawHeaderBar("ПЛЕЕР ПЕСЕН", songPlaying ? "PLAY" : "LIST");
    if (songsList.empty()) {
      u8g2.drawUTF8(6, 28, "Песен пока нет");
      u8g2.drawUTF8(6, 42, "Добавь их в веб-меню");
      u8g2.drawUTF8(6, 62, "OK или < Назад");
    } else if (songPlaying && activeSongIdx >= 0 && activeSongIdx < (int)songsList.size()) {
      u8g2.drawUTF8(6, 26, utf8Truncate(songsList[activeSongIdx].title, 18).c_str());
      u8g2.drawRFrame(6, 31, 116, 15, 3);
      u8g2.drawUTF8(12, 42, (String("Шаг: ") + activeSongStepLabel).c_str());
      u8g2.drawUTF8(6, 62, "OK Стоп  < Назад");
    } else {
      int total = (int)songsList.size(), cur = constrain(pianoSongCursor, 0, total - 1), startIdx = constrain(cur - 1, 0, max(0, total - 2));
      for (int i = 0; i < 2 && (startIdx + i) < total; i++) {
        int idx = startIdx + i;
        drawMenuRow(idx, cur, 17 + i * 12, utf8Truncate(songsList[idx].title, 17).c_str());
      }
      if (!soundEnabled) u8g2.drawUTF8(6, 50, "(Звук выкл)");
      u8g2.drawUTF8(6, 62, "^v Выбор OK Плей");
    }
  } else if (gameState == ST_METRONOME) {
    char bpmBadge[8];
    snprintf(bpmBadge, sizeof(bpmBadge), "%d", metroBpm);
    drawHeaderBar("МЕТРОНОМ", bpmBadge);
    u8g2.drawUTF8(8, 25, (String("Темп: ") + String(metroBpm) + " BPM").c_str());
    for (int b = 0; b < 4; b++) {
      int bx = 12 + b * 27;
      if (metroRunning && metroBeat == b) u8g2.drawRBox(bx, 30, 20, 10, 2);
      else u8g2.drawRFrame(bx, 30, 20, 10, 2);
    }
    u8g2.drawUTF8(4, 62, metroRunning ? "OK:Стоп  < Выход" : "^v ±1 > +10 OK");
  } else if (gameState == ST_NOTES_LIST) {
    drawHeaderBar("ШПАРГАЛКИ", notesList.empty() ? "0" : (String(noteCursor + 1) + "/" + String(notesList.size())).c_str());
    if (notesList.empty()) {
      u8g2.drawUTF8(8, 30, "Шпаргалок пока нет");
      u8g2.drawUTF8(8, 44, "Добавь в веб-меню");
      u8g2.drawUTF8(8, 61, "OK или < Назад");
    } else {
      int total = (int)notesList.size(), startIdx = constrain(noteCursor - 1, 0, max(0, total - 3));
      for (int i = 0; i < 3 && (startIdx + i) < total; i++) {
        int idx = startIdx + i;
        drawMenuRow(idx, noteCursor, 17 + i * 12, utf8Truncate(notesList[idx].topic, 16).c_str());
      }
      drawScrollIndicator(noteCursor, total);
      u8g2.drawUTF8(6, 62, "OK открыть  < вых");
    }
  } else if (gameState == ST_NOTE_VIEW) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    if (activeNoteIdx < 0 || activeNoteIdx >= (int)notesList.size()) {
      drawHeaderBar("ШПАРГАЛКА");
      u8g2.drawUTF8(8, 32, "Тема не найдена");
      u8g2.drawUTF8(8, 61, "OK или < Назад");
    } else {
      drawHeaderBar(utf8Truncate(notesList[activeNoteIdx].topic, 18).c_str());
      std::vector<String> wrapped; buildNoteLines(notesList[activeNoteIdx].body, wrapped, 20);
      const int visibleRows = 4;
      int maxScroll = max(0, (int)wrapped.size() - visibleRows);
      noteScrollLine = constrain(noteScrollLine, 0, maxScroll);
      for (int i = 0; i < visibleRows && (noteScrollLine + i) < (int)wrapped.size(); i++) {
        u8g2.drawUTF8(6, 25 + (i * 9), wrapped[noteScrollLine + i].c_str());
      }
      u8g2.drawUTF8(6, 63, "^v лист  OK выход");
    }
  } else if (gameState == ST_CALCULATOR) {
    char symBadge[6];
    snprintf(symBadge, sizeof(symBadge), "[%c]", CALC_CHARS[calcCharIdx]);
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
    u8g2.drawUTF8(4, 61, "^v Симв >+ <-Стр");
  } else if (gameState == ST_GAME_SELECT) {
    char posBuf[8];
    snprintf(posBuf, sizeof(posBuf), "%d/9", (int)menuCursor + 1);
    drawHeaderBar("ВЫБОР ИГРЫ", posBuf);
    const char* gItems[9] = {
      "1. Слоты", "2. Змейка", "3. Тетрис", "4. Баттл Флип",
      "5. Flappy Bird", "6. Понг", "7. Динозаврик", "8. Doodle Jump", "9. Назад"
    };
    int startIdx = constrain(menuCursor - 2, 0, 5);
    for (int i = 0; i < 4 && (startIdx + i) < 9; i++) {
      int idx = startIdx + i;
      drawMenuRow(idx, menuCursor, 16 + i * 12, gItems[idx]);
    }
    drawScrollIndicator(menuCursor, 9);
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
    u8g2.drawRBox(dxPos + 1, dyPos, 5, 5, 1);
    u8g2.drawBox(dxPos, dyPos + 5, 7, 3);
  } else if (gameState == ST_DINO_GAME) {
    u8g2.drawLine(0, 10, 128, 10);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 8, (String("SCORE: ") + String(dinoScore)).c_str());
    u8g2.drawStr(74, 8, (String("BEST: ") + String(dinoRecord)).c_str());
    u8g2.drawLine(0, DINO_GROUND_Y, 128, DINO_GROUND_Y);
    int dyPos = (int)dinoY;
    if (dinoDucking) {
      u8g2.drawBox(DINO_X, dyPos, 10, 7);
      u8g2.drawBox(DINO_X + 7, dyPos - 2, 4, 3);
    } else {
      u8g2.drawBox(DINO_X, dyPos, 8, 9);
      u8g2.drawBox(DINO_X + 4, dyPos - 3, 5, 4);
      if ((nowMs / 110) % 2 == 0) {
        u8g2.drawBox(DINO_X + 1, dyPos + 9, 2, 3);
        u8g2.drawBox(DINO_X + 5, dyPos + 8, 2, 2);
      } else {
        u8g2.drawBox(DINO_X + 1, dyPos + 8, 2, 2);
        u8g2.drawBox(DINO_X + 5, dyPos + 9, 2, 3);
      }
    }
    int cxPos = (int)cactusX, cTop = DINO_GROUND_Y - cactusH;
    u8g2.drawBox(cxPos + cactusW / 2 - 1, cTop, 3, cactusH);
    if (cactusW >= 7) u8g2.drawBox(cxPos, cTop + 4, 2, 4);
    if (cactusW >= 10) u8g2.drawBox(cxPos + cactusW - 2, cTop + 3, 2, 5);
  } else if (gameState == ST_CLOCK_MENU) {
    drawHeaderBar("ЧАСЫ", "TIME");
    const char* cItems[4] = {"1. Время по сети", "2. Секундомер", "3. Таймер", "4. Назад"};
    for (int i = 0; i < 4; i++) drawMenuRow(i, menuCursor, 16 + i * 12, cItems[i]);
    drawScrollIndicator(menuCursor, 4);
  } else if (gameState == ST_NET_CLOCK) {
    struct tm timeinfo;
    if (WiFi.status() == WL_CONNECTED && getLocalTime(&timeinfo, 25)) {
      const char* wdays[7] = {"ВС", "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ"};
      const char* wdLabel = (timeinfo.tm_wday >= 0 && timeinfo.tm_wday < 7) ? wdays[timeinfo.tm_wday] : "";
      char topBuf[24];
      snprintf(topBuf, sizeof(topBuf), "%s %02d.%02d.%04d", wdLabel, timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
      drawHeaderBar(topBuf);

      char timeBuf[16];
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      u8g2.drawRFrame(10, 16, 108, 28, 3);
      u8g2.setFont(u8g2_font_10x20_tn);
      u8g2.drawStr(24, 37, timeBuf);

      u8g2.setFont(u8g2_font_6x12_t_cyrillic);
      if (weatherValid) {
        int tRound = (int)roundf(weatherTempC);
        String wLine = (tRound > 0 ? "+" : "") + String(tRound) + "°C · " + weatherCondition;
        u8g2.drawUTF8(6, 59, utf8Truncate(wLine, 20).c_str());
      } else {
        u8g2.drawUTF8(14, 59, "OK: обновить погоду");
      }
    } else {
      drawHeaderBar("ЧАСЫ", "NTP");
      u8g2.drawUTF8(12, 30, (WiFi.status() == WL_CONNECTED) ? "Синхронизация..." : "Нет Wi-Fi");
      u8g2.drawUTF8(12, 48, "< Назад");
    }
  } else if (gameState == ST_STOPWATCH) {
    drawHeaderBar("СЕКУНДОМЕР", stopwatchRunning ? "RUN" : "STOP");
    unsigned long ms = getCurrentStopwatchMs();
    char swBuf[16];
    snprintf(swBuf, sizeof(swBuf), "%02u:%02u.%u", (unsigned int)((ms / 60000UL) % 100UL), (unsigned int)((ms / 1000UL) % 60UL), (unsigned int)((ms / 100UL) % 10UL));
    u8g2.drawRFrame(14, 16, 100, 27, 3);
    u8g2.setFont(u8g2_font_10x20_tn);
    u8g2.drawStr(24, 36, swBuf);
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(6, 59, stopwatchRunning ? "OK:Стоп" : "OK:Старт");
    u8g2.drawUTF8(70, 59, "v:Сброс");
  } else if (gameState == ST_TIMER_SET) {
    drawHeaderBar("ТАЙМЕР", "SET");
    char hBuf[4], mBuf[4], sBuf[4];
    snprintf(hBuf, sizeof(hBuf), "%02d", timerHoursSet);
    snprintf(mBuf, sizeof(mBuf), "%02d", timerMinutesSet);
    snprintf(sBuf, sizeof(sBuf), "%02d", timerSecondsSet);
    u8g2.setFont(u8g2_font_10x20_tn);
    if (timerFieldCursor == 0) { u8g2.drawBox(16, 19, 24, 22); u8g2.setDrawColor(0); }
    u8g2.drawStr(18, 37, hBuf); u8g2.setDrawColor(1); u8g2.drawStr(42, 37, ":");
    if (timerFieldCursor == 1) { u8g2.drawBox(52, 19, 24, 22); u8g2.setDrawColor(0); }
    u8g2.drawStr(54, 37, mBuf); u8g2.setDrawColor(1); u8g2.drawStr(78, 37, ":");
    if (timerFieldCursor == 2) { u8g2.drawBox(88, 19, 24, 22); u8g2.setDrawColor(0); }
    u8g2.drawStr(90, 37, sBuf); u8g2.setDrawColor(1);
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(6, 59, "<>:Поле");
    u8g2.drawUTF8(68, 59, "OK:Старт");
  } else if (gameState == ST_TIMER_RUNNING) {
    drawHeaderBar("ТАЙМЕР", timerRunning ? "RUN" : "PAUS");
    unsigned long totalSec = (getCurrentTimerRemainingMs() + 999UL) / 1000UL;
    char tBuf[16];
    snprintf(tBuf, sizeof(tBuf), "%02u:%02u:%02u", (unsigned int)((totalSec / 3600UL) % 100UL), (unsigned int)((totalSec / 60UL) % 60UL), (unsigned int)(totalSec % 60UL));
    u8g2.drawRFrame(10, 16, 108, 27, 3);
    u8g2.setFont(u8g2_font_10x20_tn);
    u8g2.drawStr(18, 36, tBuf);
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(6, 59, timerRunning ? "OK:Пауза" : "OK:Прод.");
    u8g2.drawUTF8(70, 59, "v:Сброс");
  } else if (gameState == ST_TIMER_DONE) {
    drawHeaderBar("ТАЙМЕР", "DONE");
    u8g2.drawUTF8(24, 30, "ВРЕМЯ ВЫШЛО!");
    u8g2.drawUTF8(14, 48, "Нажми кнопку...");
  } else if (gameState == ST_COIN_KEYBOARD) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(4, 10, "МОНЕТ:");
    u8g2.drawStr(62, 10, inputCoinStr);
    u8g2.drawLine(0, 12, 128, 12);
    for (int r = 0; r < 5; r++) for (int c = 0; c < 3; c++) {
      char ch = NUM_KB_LAYOUT[r][c];
      if (ch == ' ') continue;
      int x = 16 + c * 36, y = 14 + r * 10;
      if (r == kbRow && c == kbCol) { u8g2.drawBox(x - 2, y, 24, 9); u8g2.setDrawColor(0); }
      char label[4] = {ch, '\0', '\0', '\0'};
      if (ch == 'O') strcpy(label, "ОК");
      u8g2.drawUTF8(x, y + 7, label);
      u8g2.setDrawColor(1);
    }
  } else if (gameState == ST_ACHIEVEMENTS_MENU) {
    drawHeaderBar("ДОСТИЖЕНИЯ", "ACH");
    int startIdx = constrain(menuCursor - 1, 0, ACH_COUNT - 2);
    for (int i = 0; i < 2; i++) {
      int idx = startIdx + i; int y = 18 + i * 22;
      if (menuCursor == idx) u8g2.drawRFrame(2, y - 2, 120, 20, 3);
      u8g2.drawUTF8(8, y + 7, achievements[idx].title);
      u8g2.drawUTF8(8, y + 17, achievements[idx].unlocked ? "[ОТКРЫТО]" : achievements[idx].desc);
    }
    drawScrollIndicator(menuCursor, ACH_COUNT);
  } else if (gameState == ST_INFO_SCREEN) {
    drawHeaderBar("ИНФОРМАЦИЯ", FW_VERSION);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(6, 24, (String("VER: ") + FW_VERSION).c_str());
    u8g2.drawStr(6, 34, (String("SPINS: ") + totalSpins + " WIN: " + maxWin).c_str());
    u8g2.drawStr(6, 44, (String("SNK:") + snakeRecord + " TET:" + tetrisRecord + " DOO:" + doodleRecord).c_str());
    u8g2.drawStr(6, 54, (String("IP: ") + ((WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "OFF")).c_str());
  } else if (gameState == ST_CREDITS_MENU) {
    drawHeaderBar("О СИСТЕМЕ", "SYS");
    u8g2.drawUTF8(20, 26, "СИСТЕМА ГОТОВА");
    u8g2.drawUTF8(20, 42, "РЕЖИМ: АКТИВЕН");
    u8g2.drawUTF8(20, 58, "< Назад");
  } else if (gameState == ST_SETTINGS_MENU) {
    char posBuf[8];
    snprintf(posBuf, sizeof(posBuf), "%d/7", (int)menuCursor + 1);
    drawHeaderBar("НАСТРОЙКИ", posBuf);
    const char* sItems[7] = {
      "1. Wi-Fi сеть", soundEnabled ? "2. Звук: ВКЛ" : "2. Звук: ВЫКЛ",
      "3. Дать монет", "4. Сброс сейвов", "5. Тест железа", "6. OTA с GitHub", "7. Назад"
    };
    int startIdx = constrain(menuCursor - 2, 0, 3);
    for (int i = 0; i < 4 && (startIdx + i) < 7; i++) {
      int idx = startIdx + i;
      drawMenuRow(idx, menuCursor, 16 + i * 12, sItems[idx]);
    }
    drawScrollIndicator(menuCursor, 7);
  } else if (gameState == ST_WIFI_MANAGEMENT) {
    drawHeaderBar("WI-FI СЕТЬ", "NET");
    const char* wItems[5] = {
      "1. Найти сеть", "2. Показать IP",
      (WiFi.status() == WL_CONNECTED) ? "3. Забыть сеть" : "3. Сброс сети",
      "4. Обновить", "5. Назад"
    };
    int startIdx = constrain(menuCursor - 2, 0, 1);
    for (int i = 0; i < 4 && (startIdx + i) < 5; i++) {
      int idx = startIdx + i;
      drawMenuRow(idx, menuCursor, 16 + i * 12, wItems[idx]);
    }
    drawScrollIndicator(menuCursor, 5);
  } else if (gameState == ST_WIFI_CONNECTED_INFO) {
    drawHeaderBar("WI-FI ПОДКЛЮЧЁН", "OK");
    u8g2.drawUTF8(6, 25, (String("Сеть: ") + utf8Truncate(String(currentSsid), 14)).c_str());
    u8g2.drawRFrame(4, 29, 120, 15, 3);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(10, 39, (String("IP: ") + ipAddressStr).c_str());
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(6, 60, "Любая кнопка: далее");
  } else if (gameState == ST_WIFI_SCAN) {
    drawHeaderBar("ПОИСК WI-FI", "SCAN");
    u8g2.drawUTF8(14, 34, "Сканирую эфир...");
    int dots = (int)((nowMs / 200) % 4);
    for (int d = 0; d < dots; d++) u8g2.drawBox(46 + d * 12, 43, 6, 4);
  } else if (gameState == ST_WIFI_LIST) {
    drawHeaderBar("СПИСОК СЕТЕЙ", String(wifiNetCount).c_str());
    if (wifiNetCount <= 0) {
      u8g2.drawUTF8(8, 30, "Сети не найдены");
      u8g2.drawUTF8(8, 48, "< Назад");
    } else {
      int startIdx = constrain(menuCursor - 1, 0, max(0, wifiNetCount - 3));
      for (int i = 0; i < 3 && (startIdx + i) < wifiNetCount; i++) {
        int idx = startIdx + i;
        drawMenuRow(idx, menuCursor, 17 + i * 12, utf8Truncate(WiFi.SSID(idx), 16).c_str());
      }
      drawScrollIndicator(menuCursor, wifiNetCount);
    }
  } else if (gameState == ST_WIFI_KEYBOARD) {
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(2, 8, (String("PASS: ") + inputPassword).c_str());
    u8g2.drawLine(0, 10, 128, 10);
    for (int r = 0; r < 5; r++) for (int c = 0; c < 10; c++) {
      char ch = KB_LAYOUT[r][c];
      if (ch == ' ') continue;
      int x = 3 + c * 12, y = 12 + r * 10;
      if (r == kbRow && c == kbCol) { u8g2.drawBox(x - 1, y, 10, 9); u8g2.setDrawColor(0); }
      char buf[2] = {ch, '\0'};
      u8g2.drawStr(x + 2, y + 7, buf);
      u8g2.setDrawColor(1);
    }
  } else if (gameState == ST_WIFI_CONNECTING) {
    drawHeaderBar("ПОДКЛЮЧЕНИЕ", "WIFI");
    u8g2.drawUTF8(10, 28, utf8Truncate(String(targetSsid), 18).c_str());
    u8g2.drawUTF8(10, 44, "Установка связи...");
  } else if (gameState == ST_WIFI_ERROR) {
    drawHeaderBar("ОШИБКА WI-FI", "ERR");
    u8g2.drawUTF8(12, 32, "Не удалось войти");
    u8g2.drawUTF8(12, 48, "Проверь пароль");
  } else if (gameState >= ST_RESET_STEP_1 && gameState <= ST_RESET_STEP_3) {
    int stepNum = (gameState - ST_RESET_STEP_1) + 1;
    drawHeaderBar("СБРОС СЕЙВОВ", (String(stepNum) + "/3").c_str());
    u8g2.drawUTF8(8, 28, "Удалить прогресс?");
    u8g2.drawUTF8(8, 44, "^ Да");
    u8g2.drawUTF8(54, 44, "v/< Нет");
  } else if (gameState == ST_TEST_CONFIRM) {
    drawHeaderBar("ТЕСТ ЖЕЛЕЗА", "TEST");
    u8g2.drawUTF8(8, 28, "Запустить тест?");
    u8g2.drawUTF8(8, 45, "OK: Да   ^: Нет");
  } else if (gameState == ST_TEST_MELODY) {
    drawHeaderBar("ЗВУКОВОЙ ТЕСТ", "SND");
    u8g2.drawUTF8(14, 34, "Играет мелодия...");
  } else if (gameState == ST_TEST_MODE) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(14, 14, "ТЕСТ КНОПОК");
    if (digitalRead(BTN_UP_PIN) == LOW)     u8g2.drawUTF8(54, 24, "^");
    if (digitalRead(BTN_DOWN_PIN) == LOW)   u8g2.drawUTF8(54, 52, "v");
    if (digitalRead(BTN_LEFT_PIN) == LOW)   u8g2.drawUTF8(20, 38, "<");
    if (digitalRead(BTN_RIGHT_PIN) == LOW)  u8g2.drawUTF8(88, 38, ">");
    if (digitalRead(BTN_SELECT_PIN) == LOW) u8g2.drawUTF8(48, 38, "ОК");
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(14, 62, "HOLD UP+OK TO EXIT");
  } else if (gameState == ST_SNAKE_GAME) {
    u8g2.drawLine(0, 10, 128, 10);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 8, (String("SCORE: ") + String(snakeScore)).c_str());
    u8g2.drawStr(74, 8, (String("BEST: ") + String(snakeRecord)).c_str());
    for (int i = 0; i < snakeLen; i++) {
      int sx = snake[i].x * 8, sy = 11 + snake[i].y * 6;
      if (i == 0) u8g2.drawBox(sx, sy, 8, 6);
      else u8g2.drawFrame(sx + 1, sy + 1, 6, 4);
    }
    int fx = food.x * 8, fy = 11 + food.y * 6;
    u8g2.drawBox(fx + 2, fy + 1, 4, 4);
  } else if (gameState == ST_TETRIS_GAME) {
    u8g2.drawFrame(2, 12, 52, 50);
    for (int r = 0; r < TETRIS_H; r++) for (int c = 0; c < TETRIS_W; c++) {
      if (tetrisBoard[r][c]) u8g2.drawBox(4 + c * 5, 13 + r * 2, 4, 2);
    }
    byte mat[4][4]; getRotatedShape(tetrisCurrShape, tetrisCurrRot, mat);
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
      if (mat[r][c]) { int bx = tetrisX + c, by = tetrisY + r; if (by >= 0 && by < TETRIS_H && bx >= 0 && bx < TETRIS_W) u8g2.drawBox(4 + bx * 5, 13 + by * 2, 4, 2); }
    }
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(62, 20, (String("SCR: ") + String(tetrisScore)).c_str());
    u8g2.drawStr(62, 32, (String("TOP: ") + String(tetrisRecord)).c_str());
    u8g2.drawStr(62, 44, (String("LVL: ") + String(tetrisLevel)).c_str());
  } else if (gameState == ST_BOTTLE_GAME) {
    drawHeaderBar("БАТТЛ ФЛИП", (String(bottleScore) + "/" + String(bottleRecord)).c_str());
    int tableY = 53;
    u8g2.drawLine(14, tableY, 114, tableY);
    u8g2.drawLine(18, tableY + 1, 18, 63);
    u8g2.drawLine(110, tableY + 1, 110, 63);

    if (bottleState == 0) {
      drawBottleGraphic(64, tableY - 11, 0.0f);
      u8g2.drawUTF8(20, 26, "OK: Бросить");
    } else if (bottleState == 1) {
      unsigned long el = nowMs - bottleAnimTime;
      float t = (float)el / 620.0f;
      if (t >= 1.0f) {
        bottleState = 2;
        stateTimer = nowMs;
        bottleLanded = (random(0, 100) < 42);
        if (bottleLanded) {
          bottleScore++;
          if (bottleScore > bottleRecord) { bottleRecord = bottleScore; saveGameData(); }
          beep(1350, 80);
        } else {
          bottleScore = 0;
          beep(320, 140);
        }
        markDirty();
      } else {
        float arc = sinf(t * 3.14159265f);
        int by = (int)roundf((float)(tableY - 11) - arc * 23.0f);
        float angle = t * 6.2831853f * 2.0f;
        drawBottleGraphic(64, by, angle);
      }
    } else {
      drawBottleGraphic(64, tableY - (bottleLanded ? 11 : 5), bottleLanded ? 0.0f : 1.5707963f);
      u8g2.drawUTF8(bottleLanded ? 22 : 24, 25, bottleLanded ? "СТОИТ! +1" : "УПАЛА!");
      u8g2.drawUTF8(14, 38, "OK: Ещё раз");
    }
  } else if (gameState == ST_FLAPPY_GAME) {
    u8g2.drawLine(0, 10, 128, 10);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 8, (String("SCORE: ") + String(flappyScore)).c_str());
    u8g2.drawStr(74, 8, (String("BEST: ") + String(flappyRecord)).c_str());
    int px = (int)pipeX;
    if (pipeGapY > 11) u8g2.drawBox(px, 11, PIPE_W, pipeGapY - 11);
    if (pipeGapY + PIPE_GAP_H < 64) u8g2.drawBox(px, pipeGapY + PIPE_GAP_H, PIPE_W, 64 - (pipeGapY + PIPE_GAP_H));
    int by = (int)flappyY;
    u8g2.drawBox(30, by, 7, 5);
    u8g2.drawBox(35, by + 1, 3, 2);
  } else if (gameState == ST_PONG_GAME) {
    u8g2.drawLine(0, 10, 128, 10);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 8, (String("SCORE: ") + String(pongScore)).c_str());
    u8g2.drawStr(74, 8, (String("BEST: ") + String(pongRecord)).c_str());
    u8g2.drawRBox((int)paddleX, 58, PADDLE_W, PADDLE_H, 2);
    u8g2.drawBox((int)ballX, (int)ballY, 4, 4);
  } else if (gameState == ST_SNAKE_OVER || gameState == ST_TETRIS_OVER ||
             gameState == ST_FLAPPY_OVER || gameState == ST_PONG_OVER ||
             gameState == ST_DINO_OVER || gameState == ST_DOODLE_OVER) {
    drawHeaderBar("ИГРА ОКОНЧЕНА", "OVER");
    int sc = (gameState == ST_SNAKE_OVER) ? snakeScore :
             (gameState == ST_TETRIS_OVER) ? tetrisScore :
             (gameState == ST_FLAPPY_OVER) ? flappyScore :
             (gameState == ST_PONG_OVER) ? pongScore :
             (gameState == ST_DINO_OVER) ? dinoScore : doodleScore;
    int bc = (gameState == ST_SNAKE_OVER) ? snakeRecord :
             (gameState == ST_TETRIS_OVER) ? tetrisRecord :
             (gameState == ST_FLAPPY_OVER) ? flappyRecord :
             (gameState == ST_PONG_OVER) ? pongRecord :
             (gameState == ST_DINO_OVER) ? dinoRecord : doodleRecord;
    u8g2.drawUTF8(14, 28, (String("Счёт: ") + String(sc)).c_str());
    u8g2.drawUTF8(14, 42, (String("Рекорд: ") + String(bc)).c_str());
    u8g2.drawUTF8(14, 58, "< Меню");
  } else {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    if (godMode) {
      u8g2.drawUTF8(2, 11, "МОНЕТ: ∞");
      u8g2.drawUTF8(80, 11, "БОГ");
    } else {
      u8g2.drawUTF8(2, 11, (String("МОНЕТ:") + String(displayCoins)).c_str());
      if (autoSpin) u8g2.drawUTF8(92, 11, "АВТО");
    }
    u8g2.drawHLine(0, 13, 128);

    const int reelXs[3] = {6, 48, 90};
    const int reelW = 32, reelH = 44, reelTop = 16;
    const float offs[3] = {reelOffset1, reelOffset2, reelOffset3};
    const float bounces[3] = {bounce1, bounce2, bounce3};
    const int8_t curSyms[3] = {slot1, slot2, slot3};
    const int8_t prevSyms[3] = {prevSlot1, prevSlot2, prevSlot3};

    for (int i = 0; i < 3; i++) {
      int rx = reelXs[i], ry = reelTop + (int)roundf(bounces[i]);
      u8g2.drawRFrame(rx, ry, reelW, reelH, 4);
      u8g2.setClipWindow(rx + 2, ry + 2, rx + reelW - 3, ry + reelH - 3);
      int centerY = ry + reelH / 2 + 1;
      int yPrev = centerY - 24 + (int)offs[i];
      int yCur  = centerY + (int)offs[i];
      int yNext = centerY + 24 + (int)offs[i];
      drawSymbolAt(prevSyms[i], rx + reelW / 2, yPrev);
      drawSymbolAt(curSyms[i],  rx + reelW / 2, yCur);
      drawSymbolAt(curSyms[i],  rx + reelW / 2, yNext);
      u8g2.setMaxClipWindow();
      u8g2.drawLine(rx - 2, ry + reelH / 2, rx + 1, ry + reelH / 2);
      u8g2.drawLine(rx + reelW - 2, ry + reelH / 2, rx + reelW + 1, ry + reelH / 2);
    }

    if (gameState == ST_WIN_ANIM) {
      u8g2.drawRBox(10, 22, 108, 22, 4);
      u8g2.setDrawColor(0);
      u8g2.drawUTF8(16, 37, (slot1 == 1) ? ("ДЖЕКПОТ! +" + String(currentWin)).c_str() : ("КУШ! +" + String(currentWin)).c_str());
      u8g2.setDrawColor(1);
    } else if (gameState == ST_LOSE_ANIM) {
      u8g2.drawRBox(24, 24, 80, 18, 4);
      u8g2.setDrawColor(0);
      u8g2.drawUTF8(34, 37, "МИМО!");
      u8g2.setDrawColor(1);
    } else if (gameState == ST_NO_MONEY) {
      u8g2.drawRBox(6, 22, 116, 22, 4);
      u8g2.setDrawColor(0);
      u8g2.drawUTF8(14, 37, "МАЛО МОНЕТ!");
      u8g2.setDrawColor(1);
    }
  }

  if (activeAchPopup >= 0 && ((unsigned long)(nowMs - achPopupTimer) < 2200)) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawRBox(4, 18, 120, 28, 4);
    u8g2.setDrawColor(0);
    u8g2.drawUTF8(12, 30, "АЧИВКА ОТКРЫТА!");
    u8g2.drawUTF8(12, 42, achievements[activeAchPopup].title);
    u8g2.setDrawColor(1);
  } else {
    activeAchPopup = -1;
  }

  u8g2.sendBuffer();
}

void drawSymbolAt(int type, int cx, int cy) {
  switch (type) {
    case 0:
      u8g2.drawCircle(cx, cy + 2, 6, U8G2_DRAW_ALL);
      u8g2.drawLine(cx, cy - 4, cx + 1, cy - 8);
      u8g2.drawLine(cx + 1, cy - 7, cx + 5, cy - 9);
      break;
    case 1:
      u8g2.drawCircle(cx - 5, cy + 3, 3, U8G2_DRAW_ALL);
      u8g2.drawCircle(cx + 5, cy + 3, 3, U8G2_DRAW_ALL);
      u8g2.drawRBox(cx - 2, cy - 8, 5, 13, 2);
      u8g2.drawCircle(cx, cy - 8, 2, U8G2_DRAW_ALL);
      u8g2.drawLine(cx - 2, cy - 9, cx + 2, cy - 9);
      break;
    case 2:
      u8g2.drawCircle(cx, cy, 6, U8G2_DRAW_ALL);
      u8g2.setDrawColor(0);
      u8g2.drawDisc(cx + 3, cy - 2, 5, U8G2_DRAW_ALL);
      u8g2.setDrawColor(1);
      break;
    case 3:
      u8g2.drawCircle(cx - 3, cy - 2, 2, U8G2_DRAW_ALL);
      u8g2.drawCircle(cx + 3, cy - 2, 2, U8G2_DRAW_ALL);
      u8g2.drawCircle(cx, cy + 2, 2, U8G2_DRAW_ALL);
      u8g2.drawCircle(cx - 3, cy + 5, 2, U8G2_DRAW_ALL);
      u8g2.drawCircle(cx + 3, cy + 5, 2, U8G2_DRAW_ALL);
      u8g2.drawLine(cx, cy - 4, cx + 2, cy - 8);
      break;
    default:
      u8g2.drawLine(cx - 5, cy - 5, cx + 5, cy + 5);
      u8g2.drawLine(cx + 5, cy - 5, cx - 5, cy + 5);
      break;
  }
}

void beep(int freq, int duration) {
  if (!soundEnabled) return;
  tone(BUZZER_PIN, freq, duration);
}

void startToneSeq(const ToneStep* seq, uint8_t len, bool returnToTest) {
  if (!soundEnabled || seq == nullptr || len == 0) {
    activeSeq = nullptr; activeSeqLen = 0; activeSeqIdx = 0; returnToTestModeAfterSeq = false;
    if (returnToTest) gameState = ST_TEST_MODE;
    return;
  }
  stopSongPlayback();
  metroRunning = false;
  activeSeq = seq; activeSeqLen = len; activeSeqIdx = 0; returnToTestModeAfterSeq = returnToTest;
  tone(BUZZER_PIN, seq[0].freq, seq[0].dur);
  nextToneAtMs = millis() + (unsigned long)seq[0].dur + (unsigned long)seq[0].gap;
}

void updateToneSeq(unsigned long now) {
  if (activeSeq == nullptr || activeSeqLen == 0) return;
  if ((unsigned long)(now - nextToneAtMs) < 0x80000000UL && now < nextToneAtMs) return;
  activeSeqIdx++;
  if (activeSeqIdx >= activeSeqLen) {
    bool goTest = returnToTestModeAfterSeq;
    activeSeq = nullptr; activeSeqLen = 0; activeSeqIdx = 0; returnToTestModeAfterSeq = false;
    noTone(BUZZER_PIN);
    if (goTest) { testExitLockUntil = millis() + 300; gameState = ST_TEST_MODE; markDirty(); }
    return;
  }
  const ToneStep& st = activeSeq[activeSeqIdx];
  tone(BUZZER_PIN, st.freq, st.dur);
  nextToneAtMs = now + (unsigned long)st.dur + (unsigned long)st.gap;
}

void playWinSound()    { startToneSeq(WIN_SEQ, sizeof(WIN_SEQ) / sizeof(WIN_SEQ[0]), false); }
void playLoseSound()   { startToneSeq(LOSE_SEQ, sizeof(LOSE_SEQ) / sizeof(LOSE_SEQ[0]), false); }
void playJackpotSound(){ startToneSeq(JACKPOT_SEQ, sizeof(JACKPOT_SEQ) / sizeof(JACKPOT_SEQ[0]), false); }
void playJingleBells() { startToneSeq(JINGLE_SEQ, sizeof(JINGLE_SEQ) / sizeof(JINGLE_SEQ[0]), true); }
