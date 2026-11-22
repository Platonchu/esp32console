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
const char* FW_VERSION = "v2.1.5";
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
  ST_BET_KEYBOARD,
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
long spinCost            = 10;

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

int kbRow = 0;
int kbCol = 0;
char inputPassword[33] = "";
char inputBetStr[10]   = "";
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

const int ARCH_LOGO_W = 32;
const int ARCH_LOGO_H = 32;
float archX = 20.0f;
float archY = 12.0f;
float archVX = 1.45f;
float archVY = 1.15f;
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
  {'<',' ','O','O','O','X',' ',' ',' ',' '}
};

const char NUM_KB_LAYOUT[5][3] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'0', '<', 'X'},
  {'O', 'O', 'O'}
};

const uint8_t ACH_COUNT = 6;
Achievement achievements[ACH_COUNT] = {
  {"Новичок",    "Первый спин",       false},
  {"Лудоман",    "50 спинов",         false},
  {"Джекпот!",   "Три семерки",       false},
  {"Магнат",     "1000+ монет",       false},
  {"Змеелов",    "50 очков в Змейке", false},
  {"Архитектор", "500 очков Тетрис",  false}
};

int activeAchPopup = -1;
unsigned long achPopupTimer = 0;

int8_t slot1 = 0, slot2 = 0, slot3 = 0;
int8_t prevSlot1 = 1, prevSlot2 = 2, prevSlot3 = 3;
int8_t targetSlot1 = 0, targetSlot2 = 0, targetSlot3 = 0;
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
unsigned long bottleAnimTime = 0;
int bottleScore = 0, bottleRecord = 0;

float flappyY = 32.0, flappyVelocity = 0.0;
const float FLAPPY_GRAVITY = 0.55;
const float FLAPPY_JUMP    = -3.8;
float pipeX = 128.0;
int pipeGapY = 28;
const int PIPE_W = 14;
const int PIPE_GAP_H = 26;
int flappyScore = 0, flappyRecord = 0;
unsigned long flappyTimer = 0;

float paddleX = 50.0;
const int PADDLE_W = 28;
const int PADDLE_H = 4;
float ballX = 64.0, ballY = 18.0;
float ballVX = 1.8, ballVY = 1.6;
int pongScore = 0, pongRecord = 0;
unsigned long pongTimer = 0;

const int DINO_GROUND_Y = 54;
const int DINO_X        = 16;
float dinoY             = DINO_GROUND_Y - 12;
float dinoVY            = 0.0;
bool dinoJumping        = false;
bool dinoDucking        = false;
const float DINO_GRAV   = 0.65;
const float DINO_JUMP_V = -6.4;
float cactusX           = 128.0;
int cactusW             = 6;
int cactusH             = 12;
float dinoSpeed         = 2.6;
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
unsigned long idleTimer = 0;
unsigned long lastFrameTime = 0;
unsigned long lastBtnActionMs = 0;
const unsigned long BTN_COOLDOWN_MS = 145;
unsigned long testExitLockUntil = 0;

unsigned long selectPressStartMs = 0;
bool selectLongPressHandled = false;
const unsigned long LONG_PRESS_MS = 550;

float reelOffset1 = 0, reelOffset2 = 0, reelOffset3 = 0;
bool reel1Stopped = false, reel2Stopped = false, reel3Stopped = false;
float bounce1 = 0, bounce2 = 0, bounce3 = 0;

bool upPressed = false, downPressed = false, leftPressed = false, rightPressed = false, selectPressed = false;

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
void drawSymbolAt(int type, int cx, int cy);
void drawArchLogo(int x, int y);
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
void syncNtpTime();
void fetchWeatherIfNeeded(bool force = false);
String wmoCodeToCondition(int code);
void runUrlOtaUpdate();
void scanWifiNetworksSafe();
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
    fsInitMsg = fsReady ? "LittleFS отформатирован и готов" : "Ошибка LittleFS";
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
    case ST_MAIN_MENU:         return 9;
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
  archVX = (random(0, 2) == 0) ? 1.45f : -1.45f;
  archVY = (random(0, 2) == 0) ? 1.15f : -1.15f;
  archTimer = millis();
  markDirty();
}

void updateArchScreensaver() {
  if (!screenSleeping) return;
  unsigned long now = millis();
  if ((unsigned long)(now - archTimer) < 25) return;
  archTimer = now;

  archX += archVX;
  archY += archVY;

  if (archX <= 0.0f) { archX = 0.0f; archVX = -archVX; }
  else if (archX >= (float)(128 - ARCH_LOGO_W)) { archX = (float)(128 - ARCH_LOGO_W); archVX = -archVX; }

  if (archY <= 0.0f) { archY = 0.0f; archVY = -archVY; }
  else if (archY >= (float)(64 - ARCH_LOGO_H)) { archY = (float)(64 - ARCH_LOGO_H); archVY = -archVY; }
}

void drawArchLogo(int x, int y) {
  const int N = 28;
  int lxPrev = -1, lyPrev = -1, rxPrev = -1, ryPrev = -1;
  for (int i = 0; i <= N; i++) {
    float t = (float)i / (float)N;
    float lx = 15.0f - sinf(t * 1.35f) * 11.5f;
    float ly = 2.0f + t * 26.0f;
    int ix = x + (int)roundf(lx);
    int iy = y + (int)roundf(ly);
    if (i > 0 && lxPrev >= 0) {
      u8g2.drawLine(lxPrev, lyPrev, ix, iy);
      u8g2.drawLine(lxPrev + 1, lyPrev, ix + 1, iy);
    }
    lxPrev = ix; lyPrev = iy;

    float rx = 16.5f + sinf(t * 1.35f) * 11.5f;
    float ry = 2.0f + t * 26.0f;
    int jx = x + (int)roundf(rx);
    int jy = y + (int)roundf(ry);
    bool gap = (t > 0.45f && t < 0.85f);
    if (i > 0 && rxPrev >= 0 && !gap) {
      u8g2.drawLine(rxPrev, ryPrev, jx, jy);
      u8g2.drawLine(rxPrev + 1, ryPrev, jx + 1, jy);
    }
    rxPrev = jx; ryPrev = jy;
  }
  u8g2.drawLine(x + 14, y + 20, x + 25, y + 25);
  u8g2.drawLine(x + 14, y + 21, x + 25, y + 26);
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
  if ((long)(now - nextSongStepMs) < 0) return;
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
  if ((long)(now - nextMetroMs) < 0) return;
  unsigned long intervalMs = max(120UL, 60000UL / (unsigned long)constrain(metroBpm, MIN_BPM, MAX_BPM));
  nextMetroMs = now + intervalMs;
  metroBeat = (metroBeat + 1) % 4;
  if (soundEnabled) tone(BUZZER_PIN, (metroBeat == 0) ? 1480 : 980, 35);
}

void scanWifiNetworksSafe() {
  gameState = ST_WIFI_SCAN;
  WiFi.scanDelete();
  if (WiFi.getMode() == WIFI_OFF) { WiFi.mode(WIFI_STA); delay(50); }
  else if (!(WiFi.getMode() & WIFI_STA)) { WiFi.mode(WIFI_AP_STA); delay(50); }
  int found = WiFi.scanNetworks(false, true);
  if (found < 0) { wifiNetCount = 0; lastWebStatus = "Ошибка скана Wi-Fi (" + String(found) + ")"; }
  else { wifiNetCount = found; lastWebStatus = "Найдено сетей: " + String(wifiNetCount); }
  menuCursor = 0;
  gameState = ST_WIFI_LIST;
  markDirty();
}

void syncNtpTime() {
  if (WiFi.status() == WL_CONNECTED && !ntpConfigured) {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
    ntpConfigured = true;
  }
}

String wmoCodeToCondition(int code) {
  if (code == 0) return "Солнечно";
  if (code == 1 || code == 2) return "Малооблачно";
  if (code == 3) return "Облачно";
  if (code == 45 || code == 48) return "Туман";
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return "Дождливо";
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "Снег";
  if (code >= 95) return "Грозливо";
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
  http.setTimeout(3500);
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
    case 0: return "🍒";
    case 1: return "7️⃣";
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

String buildWebPage() {
  String ipNow = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "ОФФЛАЙН";
  String coinsDisp = godMode ? "∞ (БОГ)" : String(coins);
  String html;
  html.reserve(32000);
  html += "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>\n";
  html += "<title>ESP32 Console Hub</title><style>\n";
  html += "*{box-sizing:border-box;}\n";
  html += "body{font-family:system-ui,-apple-system,sans-serif;margin:0;padding:16px;background:#0c0e13;color:#f1f5f9;}\n";
  html += ".wrap{max-width:720px;margin:auto;}\n";
  html += ".card{background:linear-gradient(180deg,#161b26,#11151d);padding:16px;border-radius:15px;margin-bottom:14px;border:1px solid #252e42;box-shadow:0 10px 26px rgba(0,0,0,.38);}\n";
  html += "h1{margin:0 0 8px;font-size:20px;letter-spacing:.2px;display:flex;align-items:center;gap:8px;flex-wrap:wrap;}\n";
  html += ".badge{font-size:12px;background:#1e293b;color:#7dd3fc;border:1px solid #334155;padding:3px 9px;border-radius:999px;}\n";
  html += "h3{margin:0 0 10px;font-size:15px;color:#cbd5e1;}\n";
  html += ".meta{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px;}\n";
  html += ".pill{background:#1b2232;border:1px solid #2b364d;padding:6px 10px;border-radius:999px;font-size:13px;}\n";
  html += ".pill b{color:#60a5fa;}\n";
  html += ".status{margin-top:10px;font-size:13px;color:#94a3b8;}\n";
  html += ".tabs{display:flex;gap:8px;margin-bottom:14px;flex-wrap:wrap;}\n";
  html += ".tabBtn{flex:1;min-width:110px;padding:10px;border-radius:12px;border:1px solid #293347;background:#141822;color:#cbd5e1;font-weight:600;cursor:pointer;}\n";
  html += ".tabBtn.active{background:#2563eb;border-color:#3b82f6;color:#fff;}\n";
  html += ".tabBtn.hiddenTab{display:none;}\n";
  html += ".pane{display:none;}\n";
  html += ".pane.active{display:block;}\n";
  html += ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(122px,1fr));gap:8px;}\n";
  html += ".recGrid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:10px;}\n";
  html += ".recBox label{display:block;font-size:12px;color:#94a3b8;margin-bottom:4px;}\n";
  html += "button,a.btn{display:inline-flex;align-items:center;justify-content:center;text-decoration:none;width:100%;padding:10px 12px;font-size:13px;font-weight:600;border-radius:10px;border:1px solid transparent;background:#2563eb;color:#fff;cursor:pointer;user-select:none;-webkit-tap-highlight-color:transparent;transition:transform .08s ease,background .12s ease,opacity .15s ease;}\n";
  html += "button:hover,a.btn:hover{background:#3b82f6;transform:translateY(-1px);}\n";
  html += "button:active,button.pressed,a.btn:active{transform:scale(.96);}\n";
  html += "button:disabled{opacity:.55;cursor:not-allowed;transform:none;}\n";
  html += "button.sec,a.btn.sec{background:#1c2230;border-color:#2f394f;color:#e2e8f0;}\n";
  html += "button.sec:hover,a.btn.sec:hover{background:#263044;}\n";
  html += "button.gold{background:linear-gradient(135deg,#d97706,#f59e0b);color:#111;}\n";
  html += "button.teal{background:#0f766e;border-color:#14b8a6;color:#f0fdfa;}\n";
  html += "button.teal:hover{background:#0d9488;}\n";
  html += "button.plus{width:auto;padding:6px 12px;font-size:15px;border-radius:9px;}\n";
  html += "button.del{width:auto;padding:6px 10px;font-size:12px;background:#2a1920;border-color:#5c2836;color:#fda4af;}\n";
  html += "button.del:hover{background:#42202c;}\n";
  html += "button.playBtn{width:auto;padding:6px 11px;font-size:12px;background:#166534;border-color:#22c55e;color:#dcfce7;}\n";
  html += "button.playBtn:hover{background:#15803d;}\n";
  html += "button.editBtn{width:auto;padding:6px 10px;font-size:12px;background:#1e293b;border-color:#38bdf8;color:#bae6fd;}\n";
  html += ".reels{display:flex;justify-content:center;gap:10px;margin:12px 0;}\n";
  html += ".reel{width:64px;height:64px;display:flex;align-items:center;justify-content:center;font-size:32px;background:#0a0d12;border:2px solid #2b364d;border-radius:14px;box-shadow:inset 0 0 12px rgba(0,0,0,.75);}\n";
  html += ".reel.spin{animation:pulseReel .18s infinite alternate;border-color:#60a5fa;}\n";
  html += "@keyframes pulseReel{from{transform:translateY(-2px);}to{transform:translateY(2px);}}\n";
  html += ".row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;}\n";
  html += ".octRow{display:flex;gap:6px;margin-bottom:10px;flex-wrap:wrap;}\n";
  html += ".octBtn{flex:1;min-width:68px;padding:8px 6px;border-radius:10px;border:1px solid #2e3950;background:#151a25;color:#cbd5e1;font-weight:600;cursor:pointer;font-size:12px;}\n";
  html += ".octBtn.active{background:#0284c7;border-color:#38bdf8;color:#fff;}\n";
  html += ".pianoScroll{overflow-x:auto;padding-bottom:6px;background:#090c11;border:1px solid #263147;border-radius:14px;padding:12px;}\n";
  html += ".pianoStage{position:relative;height:142px;min-width:560px;user-select:none;}\n";
  html += ".whiteRow{display:flex;height:138px;gap:2px;}\n";
  html += ".wKey{position:relative;flex:1;height:100%;padding:0 2px 8px;border-radius:0 0 8px 8px;background:linear-gradient(180deg,#f8fafc,#e2e8f0);border:1px solid #94a3b8;color:#0f172a;font-size:11px;font-weight:700;display:flex;flex-direction:column;justify-content:flex-end;align-items:center;box-shadow:0 4px 0 #475569;}\n";
  html += ".wKey:hover{background:linear-gradient(180deg,#ffffff,#f1f5f9);}\n";
  html += ".wKey:active,.wKey.pressed{transform:translateY(2px);box-shadow:0 1px 0 #475569;background:#bae6fd;}\n";
  html += ".bKey{position:absolute;top:0;width:28px;height:84px;margin-left:-14px;z-index:2;padding:0 1px 6px;border-radius:0 0 6px 6px;background:linear-gradient(180deg,#1e293b,#090d16);border:1px solid #475569;color:#e2e8f0;font-size:10px;font-weight:700;display:flex;flex-direction:column;justify-content:flex-end;align-items:center;box-shadow:0 4px 0 #020617;}\n";
  html += ".bKey:hover{background:linear-gradient(180deg,#334155,#0f172a);}\n";
  html += ".bKey:active,.bKey.pressed{transform:translateY(2px);box-shadow:0 1px 0 #020617;background:#0284c7;color:#fff;}\n";
  html += ".pauseBar{margin-top:10px;height:44px;background:linear-gradient(180deg,#334155,#1e293b);border:1px solid #64748b;color:#f8fafc;font-size:13px;font-weight:700;border-radius:10px;}\n";
  html += ".pianoInfo{margin-top:10px;font-size:13px;color:#93c5fd;text-align:center;}\n";
  html += ".seqBox{background:#0a0d12;border:1px solid #273247;border-radius:11px;padding:10px;min-height:64px;margin:10px 0;font-size:13px;color:#e2e8f0;line-height:1.5;}\n";
  html += ".chip{display:inline-flex;align-items:center;gap:4px;background:#182030;border:1px solid #2e3b55;padding:3px 8px;border-radius:8px;margin:2px;font-size:12px;cursor:pointer;}\n";
  html += ".chip.pauseChip{background:#1b1d26;border-color:#475569;color:#cbd5e1;}\n";
  html += ".chip.sel{border-color:#f59e0b;background:#2a2212;color:#fde68a;}\n";
  html += ".backupBox{display:none;background:#0a0d12;border:1px solid #2b3850;border-radius:12px;padding:12px;margin-top:10px;}\n";
  html += ".backupBox.open{display:block;}\n";
  html += ".headRow{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;}\n";
  html += ".headRow h3{margin:0;}\n";
  html += "input,textarea,select{width:100%;padding:10px 12px;font-size:14px;font-family:inherit;border-radius:10px;border:1px solid #313c52;background:#0a0d12;color:#fff;margin-bottom:8px;}\n";
  html += "textarea{min-height:110px;resize:vertical;}\n";
  html += ".quick{display:flex;gap:6px;margin-top:8px;}\n";
  html += ".quick button{padding:7px 10px;font-size:12px;background:#1a2130;border-color:#2c374d;}\n";
  html += ".noteItem{background:#0d1118;border:1px solid #232c3d;border-radius:11px;padding:10px 12px;margin-top:8px;}\n";
  html += ".noteTop{display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap;}\n";
  html += ".noteTopic{font-weight:700;font-size:14px;color:#93c5fd;}\n";
  html += ".noteBody{margin-top:6px;font-size:13px;color:#cbd5e1;white-space:pre-wrap;line-height:1.35;}\n";
  html += ".emptyScroll{height:2200px;}\n";
  html += ".secretZone{display:flex;flex-direction:column;gap:10px;}\n";
  html += "button.secretGod{background:#5c1d24;border-color:#9f1239;color:#ffe4e6;}\n";
  html += "button.secretGod:hover{background:#7f1d1d;}\n";
  html += "button.secretOff{background:#3b2b10;border-color:#a16207;color:#fef08a;}\n";
  html += "button.secretOff:hover{background:#523b12;}\n";
  html += "</style></head><body><div class='wrap'>\n";
  html += "<div class='card'>\n";
  html += "  <h1>ESP32 Console <span class='badge'>" + String(FW_VERSION) + "</span></h1>\n";
  html += "  <div class='meta'>\n";
  html += "    <div class='pill'>Режим: <b id='wState'>" + String(stateToName(gameState)) + "</b></div>\n";
  html += "    <div class='pill'>IP: <b id='wIp'>" + ipNow + "</b></div>\n";
  html += "    <div class='pill'>Монет: <b id='wCoins'>" + coinsDisp + "</b></div>\n";
  html += "    <div class='pill'>Спинов: <b id='wSpins'>" + String(totalSpins) + "</b></div>\n";
  html += "    <div class='pill'>Файлы: <b id='wFs'>" + fsInitMsg + "</b></div>\n";
  html += "  </div>\n";
  html += "  <div class='status' id='wStatus'>" + lastWebStatus + "</div>\n";
  html += "</div>\n\n";
  html += "<div class='tabs'>\n";
  html += "  <button type='button' class='tabBtn active' id='tabGamesBtn' onclick=\"switchTab('games')\">Игры и пульт</button>\n";
  html += "  <button type='button' class='tabBtn' id='tabPianoBtn' onclick=\"switchTab('piano')\">Музыка</button>\n";
  html += "  <button type='button' class='tabBtn' id='tabNotesBtn' onclick=\"switchTab('notes')\">Шпаргалки</button>\n";
  html += "  <button type='button' class='tabBtn " + String(godMode ? "" : "hiddenTab") + "' id='tabRecordsBtn' onclick=\"switchTab('records')\">Рекорды</button>\n";
  html += "</div>\n\n";
  html += "<div class='pane active' id='paneGames'>\n";
  html += "  <div class='card'>\n";
  html += "    <h3>Управление экраном приставки</h3>\n";
  html += "    <div class='grid'>\n";
  html += "      <button class='sec' onclick=\"sendMode('menu')\">Главное меню</button>\n";
  html += "      <button onclick=\"sendMode('slots')\">Слоты</button>\n";
  html += "      <button onclick=\"sendMode('snake')\">Змейка</button>\n";
  html += "      <button onclick=\"sendMode('tetris')\">Тетрис</button>\n";
  html += "      <button onclick=\"sendMode('bottle')\">Баттл Флип</button>\n";
  html += "      <button onclick=\"sendMode('flappy')\">Flappy Bird</button>\n";
  html += "      <button onclick=\"sendMode('pong')\">Понг</button>\n";
  html += "      <button onclick=\"sendMode('dino')\">Динозаврик</button>\n";
  html += "      <button onclick=\"sendMode('doodle')\">Doodle Jump</button>\n";
  html += "      <button class='sec' onclick=\"sendMode('records')\">Рекорды</button>\n";
  html += "      <button class='sec' onclick=\"sendMode('clock')\">Часы и погода</button>\n";
  html += "      <button class='sec' onclick=\"sendMode('music')\">Музыка</button>\n";
  html += "      <button class='sec' onclick=\"sendMode('metro')\">Метроном</button>\n";
  html += "      <button class='sec' onclick=\"sendMode('calc')\">Калькулятор</button>\n";
  html += "      <button class='sec' onclick=\"sendMode('piano')\">Плеер песен</button>\n";
  html += "      <button class='sec' onclick=\"sendMode('notes')\">Шпаргалки</button>\n";
  html += "      <a class='btn sec' href='/update'>Загрузить .bin</a>\n";
  html += "    </div>\n";
  html += "  </div>\n\n";
  html += "  <div class='card'>\n";
  html += "    <h3>Слоты по Wi-Fi</h3>\n";
  html += "    <div class='reels'>\n";
  html += "      <div class='reel' id='r1'>" + String(symbolToChar(slot1)) + "</div>\n";
  html += "      <div class='reel' id='r2'>" + String(symbolToChar(slot2)) + "</div>\n";
  html += "      <div class='reel' id='r3'>" + String(symbolToChar(slot3)) + "</div>\n";
  html += "    </div>\n";
  html += "    <button class='gold' id='spinBtn' onclick='doSpin()'>Крутить спин</button>\n";
  html += "  </div>\n\n";
  html += "  <div class='card'>\n";
  html += "    <h3>Начислить монеты</h3>\n";
  html += "    <div class='row'>\n";
  html += "      <input type='number' id='coinInput' min='1' max='100000' value='100' style='margin:0;flex:1;min-width:130px;'>\n";
  html += "      <button onclick='addCoins()' style='width:auto;padding:10px 18px;'>Начислить</button>\n";
  html += "    </div>\n";
  html += "    <div class='quick'>\n";
  html += "      <button type='button' onclick='quickCoins(50)'>+50</button>\n";
  html += "      <button type='button' onclick='quickCoins(500)'>+500</button>\n";
  html += "      <button type='button' onclick='quickCoins(5000)'>+5000</button>\n";
  html += "    </div>\n";
  html += "  </div>\n\n";
  html += "  <div class='emptyScroll'></div>\n\n";
  html += "  <div class='card secretZone'>\n";
  html += "    <button type='button' class='secretGod' onclick='toggleGod()'>Режим бога</button>\n";
  html += "    <button type='button' class='secretOff' onclick='toggleDisable()'>Отключить</button>\n";
  html += "  </div>\n";
  html += "</div>\n\n";
  html += "<div class='pane' id='panePiano'>\n";
  html += "  <div class='card'>\n";
  html += "    <h3>Музыка: пианино и редактор песен (2 октавы)</h3>\n";
  html += "    <label for='songTitle' style='display:block;font-size:12px;color:#94a3b8;margin-bottom:4px;'>Название песни</label>\n";
  html += "    <input type='text' id='songTitle' maxlength='32' placeholder='Например: Ноктюрн в До миноре'>\n";
  html += "    <div class='octRow'>\n";
  html += "      <button type='button' class='octBtn' id='octSpan2' onclick='setOctSpan(2)'>Окт 2-3</button>\n";
  html += "      <button type='button' class='octBtn active' id='octSpan3' onclick='setOctSpan(3)'>Окт 3-4</button>\n";
  html += "      <button type='button' class='octBtn' id='octSpan4' onclick='setOctSpan(4)'>Окт 4-5</button>\n";
  html += "      <button type='button' class='octBtn' id='octSpan5' onclick='setOctSpan(5)'>Окт 5-6</button>\n";
  html += "    </div>\n";
  html += "    <div class='row' style='margin-bottom:10px;'>\n";
  html += "      <select id='durSelect' style='margin:0;flex:1;min-width:150px;'>\n";
  html += "        <option value='130'>1/8 Короткая (130 мс)</option>\n";
  html += "        <option value='260' selected>1/4 Обычная (260 мс)</option>\n";
  html += "        <option value='520'>1/2 Длинная (520 мс)</option>\n";
  html += "      </select>\n";
  html += "      <button type='button' class='sec' onclick='repeatLastStep()' style='width:auto;padding:10px 12px;'>Повторить</button>\n";
  html += "      <button type='button' class='sec' onclick='exportSongsJson()' style='width:auto;padding:10px 12px;'>Бэкап JSON</button>\n";
  html += "      <button type='button' class='sec' onclick='toggleBackupBox()' style='width:auto;padding:10px 12px;'>Загрузить бэкап</button>\n";
  html += "    </div>\n";
  html += "    <div class='backupBox' id='backupBox'>\n";
  html += "      <div style='font-size:12px;color:#93c5fd;margin-bottom:6px;'>Выбери файл <b>esp32_songs_backup.json</b> или вставь JSON ниже:</div>\n";
  html += "      <input type='file' id='backupFileInput' accept='.json,application/json' onchange='onBackupFilePicked(this)'>\n";
  html += "      <textarea id='backupJsonText' placeholder='[{\"title\":\"Мелодия\",\"steps\":[{\"freq\":440,\"dur\":260}]}]'></textarea>\n";
  html += "      <div class='row'>\n";
  html += "        <button type='button' class='teal' onclick='importBackupToBoard()' style='flex:1;'>Залить бэкап на консоль</button>\n";
  html += "        <button type='button' class='sec' onclick='toggleBackupBox()' style='width:auto;padding:10px 14px;'>Закрыть</button>\n";
  html += "      </div>\n";
  html += "    </div>\n";
  html += "    <div class='pianoScroll'>\n";
  html += "      <div class='pianoStage' id='pianoStage'></div>\n";
  html += "    </div>\n";
  html += "    <button type='button' class='pauseBar' onpointerdown='pressPauseKey(this)'>ПАУЗА (тишина / ритм)</button>\n";
  html += "    <div class='seqBox' id='seqPreview'>Нажимай белые и чёрные клавиши или ПАУЗУ - шаги запишутся сюда, затем нажми «Сохранить на консоль».</div>\n";
  html += "    <div class='row' style='margin-bottom:8px;'>\n";
  html += "      <button type='button' class='sec' onclick='moveDraftStep(-1)' style='flex:1;'>&#8592; Влево</button>\n";
  html += "      <button type='button' class='sec' onclick='moveDraftStep(1)' style='flex:1;'>Вправо &#8594;</button>\n";
  html += "      <button type='button' class='sec' onclick='deleteSelectedDraftStep()' style='flex:1;'>Удалить шаг</button>\n";
  html += "      <button type='button' class='sec' onclick='clearSongDraft()' style='flex:1;'>Очистить</button>\n";
  html += "    </div>\n";
  html += "    <div class='row'>\n";
  html += "      <button type='button' class='gold' id='saveSongBtn' onclick='uploadSongToBoard(false)' style='flex:1;'>Сохранить на консоль</button>\n";
  html += "      <button type='button' class='teal' id='savePlaySongBtn' onclick='uploadSongToBoard(true)' style='flex:1;'>Сохранить и слушать</button>\n";
  html += "    </div>\n";
  html += "    <div class='pianoInfo' id='pianoInfo'>Последний шаг: " + lastPlayedNoteName + "</div>\n";
  html += "  </div>\n\n";
  html += "  <div class='card'>\n";
  html += "    <h3>Песни на консоли (сохраняются в /songs.json)</h3>\n";
  html += "    <div id='songsListBox'>Загрузка...</div>\n";
  html += "  </div>\n";
  html += "</div>\n\n";
  html += "<div class='pane' id='paneNotes'>\n";
  html += "  <div class='card'>\n";
  html += "    <div class='headRow'>\n";
  html += "      <h3>Новая шпаргалка</h3>\n";
  html += "      <button type='button' class='plus' onclick='focusNewNote()' title='Новая тема'>+</button>\n";
  html += "    </div>\n";
  html += "    <label for='noteTopic' style='display:block;font-size:12px;color:#94a3b8;margin-bottom:4px;'>Тема</label>\n";
  html += "    <input type='text' id='noteTopic' maxlength='40' placeholder='Например: Бел лит'>\n";
  html += "    <label for='noteBody' style='display:block;font-size:12px;color:#94a3b8;margin-bottom:4px;'>Текст</label>\n";
  html += "    <textarea id='noteBody' maxlength='2500' placeholder='Вставь текст шпаргалки сюда...'></textarea>\n";
  html += "    <button type='button' onclick='uploadNote()'>Загрузить</button>\n";
  html += "  </div>\n\n";
  html += "  <div class='card'>\n";
  html += "    <h3>Сохранённые темы на консоли</h3>\n";
  html += "    <div id='notesListBox'>Загрузка...</div>\n";
  html += "  </div>\n";
  html += "</div>\n\n";
  html += "<div class='pane' id='paneRecords'>\n";
  html += "  <div class='card'>\n";
  html += "    <h3>Редактор рекордов (Режим бога)</h3>\n";
  html += "    <div class='recGrid'>\n";
  html += "      <div class='recBox'><label for='recSnakeInp'>Змейка</label><input type='number' id='recSnakeInp' min='0' max='99999' value='" + String(snakeRecord) + "'></div>\n";
  html += "      <div class='recBox'><label for='recTetrisInp'>Тетрис</label><input type='number' id='recTetrisInp' min='0' max='99999' value='" + String(tetrisRecord) + "'></div>\n";
  html += "      <div class='recBox'><label for='recFlappyInp'>Flappy Bird</label><input type='number' id='recFlappyInp' min='0' max='99999' value='" + String(flappyRecord) + "'></div>\n";
  html += "      <div class='recBox'><label for='recPongInp'>Понг</label><input type='number' id='recPongInp' min='0' max='99999' value='" + String(pongRecord) + "'></div>\n";
  html += "      <div class='recBox'><label for='recDinoInp'>Динозаврик</label><input type='number' id='recDinoInp' min='0' max='99999' value='" + String(dinoRecord) + "'></div>\n";
  html += "      <div class='recBox'><label for='recBottleInp'>Баттл Флип</label><input type='number' id='recBottleInp' min='0' max='99999' value='" + String(bottleRecord) + "'></div>\n";
  html += "      <div class='recBox'><label for='recDoodleInp'>Doodle Jump</label><input type='number' id='recDoodleInp' min='0' max='99999' value='" + String(doodleRecord) + "'></div>\n";
  html += "    </div>\n";
  html += "    <button type='button' class='gold' onclick='saveRecordsWeb()'>Сохранить рекорды</button>\n";
  html += "  </div>\n";
  html += "</div>\n";
  html += "</div>\n\n";
  html += "<script>\n";
  html += "const syms = ['🍒','7️⃣','🌙','🍇','✖️'];\n";
  html += "const semitoneNames = ['До','До#','Ре','Ре#','Ми','Фа','Фа#','Соль','Соль#','Ля','Ля#','Си'];\n";
  html += "const isBlackSemi = [false,true,false,true,false,false,true,false,true,false,true,false];\n";
  html += "let animTimer = null;\n";
  html += "let baseOctave = 3;\n";
  html += "let draftSteps = [];\n";
  html += "let selDraftIdx = -1;\n";
  html += "let latestSongs = [];\n";
  html += "let activeTab = 'games';\n";
  html += "let lastRev = -1;\n";
  html += "let godActive = " + String(godMode ? "true" : "false") + ";\n\n";
  html += "function syncGodUi(isGod) {\n";
  html += "  godActive = Boolean(isGod);\n";
  html += "  const recBtn = document.getElementById('tabRecordsBtn');\n";
  html += "  if (recBtn) recBtn.classList.toggle('hiddenTab', !godActive);\n";
  html += "  if (!godActive && activeTab === 'records') switchTab('games');\n";
  html += "}\n\n";
  html += "function setStatusMsg(msg, isErr) {\n";
  html += "  const el = document.getElementById('wStatus');\n";
  html += "  el.textContent = msg;\n";
  html += "  el.style.color = isErr ? '#fca5a5' : '#94a3b8';\n";
  html += "}\n\n";
  html += "function switchTab(t) {\n";
  html += "  if (t === 'records' && !godActive) t = 'games';\n";
  html += "  activeTab = t;\n";
  html += "  document.getElementById('tabGamesBtn').classList.toggle('active', t === 'games');\n";
  html += "  document.getElementById('tabPianoBtn').classList.toggle('active', t === 'piano');\n";
  html += "  document.getElementById('tabNotesBtn').classList.toggle('active', t === 'notes');\n";
  html += "  const recBtn = document.getElementById('tabRecordsBtn');\n";
  html += "  if (recBtn) recBtn.classList.toggle('active', t === 'records');\n";
  html += "  document.getElementById('paneGames').classList.toggle('active', t === 'games');\n";
  html += "  document.getElementById('panePiano').classList.toggle('active', t === 'piano');\n";
  html += "  document.getElementById('paneNotes').classList.toggle('active', t === 'notes');\n";
  html += "  document.getElementById('paneRecords').classList.toggle('active', t === 'records');\n";
  html += "  if (t === 'piano') { buildPianoKeyboard(); loadSongsList(); }\n";
  html += "  if (t === 'notes') { loadNotesList(); }\n";
  html += "}\n\n";
  html += "function midiToFreqJs(m) { return Math.max(65, Math.min(2400, Math.round(440 * Math.pow(2, (m - 69) / 12)))); }\n";
  html += "function midiToLabelJs(m) { return semitoneNames[((m % 12) + 12) % 12] + (Math.floor(m / 12) - 1); }\n\n";
  html += "function setOctSpan(o) {\n";
  html += "  baseOctave = o;\n";
  html += "  [2,3,4,5].forEach(x => document.getElementById('octSpan' + x).classList.toggle('active', x === o));\n";
  html += "  buildPianoKeyboard();\n";
  html += "}\n\n";
  html += "function buildPianoKeyboard() {\n";
  html += "  const stage = document.getElementById('pianoStage');\n";
  html += "  const startMidi = (baseOctave + 1) * 12;\n";
  html += "  const endMidi = startMidi + 24;\n";
  html += "  const whites = [];\n";
  html += "  for (let m = startMidi; m <= endMidi; m++) {\n";
  html += "    const semi = ((m % 12) + 12) % 12;\n";
  html += "    if (!isBlackSemi[semi]) whites.push({midi: m, label: midiToLabelJs(m)});\n";
  html += "  }\n";
  html += "  const totalW = whites.length;\n";
  html += "  let htmlKeys = `<div class='whiteRow'>`;\n";
  html += "  whites.forEach(w => { htmlKeys += `<button type='button' class='wKey' onpointerdown='pressMidiKey(${w.midi}, this)'><span>${w.label}</span></button>`; });\n";
  html += "  htmlKeys += `</div>`;\n";
  html += "  for (let m = startMidi; m < endMidi; m++) {\n";
  html += "    const semi = ((m % 12) + 12) % 12;\n";
  html += "    if (isBlackSemi[semi]) {\n";
  html += "      let leftWhites = 0;\n";
  html += "      for (let k = startMidi; k < m; k++) if (!isBlackSemi[((k % 12) + 12) % 12]) leftWhites++;\n";
  html += "      const pct = (leftWhites / totalW) * 100;\n";
  html += "      htmlKeys += `<button type='button' class='bKey' style='left:${pct.toFixed(3)}%' onpointerdown='event.stopPropagation();pressMidiKey(${m}, this)'><span>${midiToLabelJs(m)}</span></button>`;\n";
  html += "    }\n";
  html += "  }\n";
  html += "  stage.innerHTML = htmlKeys;\n";
  html += "}\n\n";
  html += "function flashKey(el) {\n";
  html += "  if (!el) return;\n";
  html += "  el.classList.add('pressed');\n";
  html += "  setTimeout(() => el.classList.remove('pressed'), 140);\n";
  html += "}\n\n";
  html += "function renderDraftSteps() {\n";
  html += "  const box = document.getElementById('seqPreview');\n";
  html += "  if (draftSteps.length === 0) {\n";
  html += "    selDraftIdx = -1;\n";
  html += "    box.textContent = 'Нажимай белые и чёрные клавиши или ПАУЗУ - шаги запишутся сюда, затем нажми «Сохранить на консоль».';\n";
  html += "    return;\n";
  html += "  }\n";
  html += "  if (selDraftIdx < 0 || selDraftIdx >= draftSteps.length) selDraftIdx = draftSteps.length - 1;\n";
  html += "  const chips = draftSteps.map((s, idx) => `<span class='chip${s.freq === 0 ? ' pauseChip' : ''}${idx === selDraftIdx ? ' sel' : ''}' onclick='pickDraftStep(${idx})'>${idx+1}. ${escHtml(s.label)}</span>`).join('');\n";
  html += "  box.innerHTML = `<div style='font-size:11px;color:#94a3b8;margin-bottom:4px;'>Шагов в черновике: ${draftSteps.length} (нажми на шаг для выбора)</div>` + chips;\n";
  html += "}\n\n";
  html += "function pickDraftStep(idx) { if (idx >= 0 && idx < draftSteps.length) { selDraftIdx = idx; renderDraftSteps(); } }\n";
  html += "function moveDraftStep(dir) {\n";
  html += "  if (selDraftIdx < 0 || draftSteps.length < 2) return;\n";
  html += "  const ni = selDraftIdx + dir;\n";
  html += "  if (ni < 0 || ni >= draftSteps.length) return;\n";
  html += "  const tmp = draftSteps[selDraftIdx]; draftSteps[selDraftIdx] = draftSteps[ni]; draftSteps[ni] = tmp; selDraftIdx = ni; renderDraftSteps();\n";
  html += "}\n";
  html += "function deleteSelectedDraftStep() {\n";
  html += "  if (selDraftIdx < 0 || selDraftIdx >= draftSteps.length) return;\n";
  html += "  draftSteps.splice(selDraftIdx, 1);\n";
  html += "  if (selDraftIdx >= draftSteps.length) selDraftIdx = draftSteps.length - 1;\n";
  html += "  renderDraftSteps();\n";
  html += "}\n\n";
  html += "async function pressMidiKey(midiNote, el) {\n";
  html += "  flashKey(el);\n";
  html += "  const dur = parseInt(document.getElementById('durSelect').value, 10) || 260;\n";
  html += "  const freq = midiToFreqJs(midiNote); const label = midiToLabelJs(midiNote);\n";
  html += "  draftSteps.push({freq, dur, label}); selDraftIdx = draftSteps.length - 1; renderDraftSteps();\n";
  html += "  try { const res = await fetch('/piano', {method:'POST', body: new URLSearchParams({freq, dur})}); if (res.ok) applyData(await res.json(), false); } catch (e) {}\n";
  html += "}\n\n";
  html += "async function pressPauseKey(el) {\n";
  html += "  flashKey(el);\n";
  html += "  const dur = parseInt(document.getElementById('durSelect').value, 10) || 260;\n";
  html += "  draftSteps.push({freq: 0, dur, label: 'Пауза'}); selDraftIdx = draftSteps.length - 1; renderDraftSteps();\n";
  html += "  try { const res = await fetch('/piano', {method:'POST', body: new URLSearchParams({pause: '1', dur})}); if (res.ok) applyData(await res.json(), false); } catch (e) {}\n";
  html += "}\n\n";
  html += "async function repeatLastStep() {\n";
  html += "  if (draftSteps.length === 0) return;\n";
  html += "  const last = draftSteps[draftSteps.length - 1];\n";
  html += "  draftSteps.push({freq: last.freq, dur: last.dur, label: last.label}); selDraftIdx = draftSteps.length - 1; renderDraftSteps();\n";
  html += "  try {\n";
  html += "    const body = (last.freq === 0) ? new URLSearchParams({pause: '1', dur: last.dur || 260}) : new URLSearchParams({freq: last.freq, dur: last.dur || 260});\n";
  html += "    const res = await fetch('/piano', {method:'POST', body}); if (res.ok) applyData(await res.json(), false);\n";
  html += "  } catch (e) {}\n";
  html += "}\n\n";
  html += "function clearSongDraft() { draftSteps = []; selDraftIdx = -1; renderDraftSteps(); }\n";
  html += "function exportSongsJson() {\n";
  html += "  const a = document.createElement('a');\n";
  html += "  a.href = 'data:application/json;charset=utf-8,' + encodeURIComponent(JSON.stringify(latestSongs, null, 2));\n";
  html += "  a.download = 'esp32_songs_backup.json'; document.body.appendChild(a); a.click(); a.remove();\n";
  html += "}\n";
  html += "function toggleBackupBox() { document.getElementById('backupBox').classList.toggle('open'); }\n";
  html += "function onBackupFilePicked(inp) {\n";
  html += "  const f = inp.files && inp.files[0]; if (!f) return;\n";
  html += "  const r = new FileReader(); r.onload = () => { document.getElementById('backupJsonText').value = String(r.result || ''); }; r.readAsText(f, 'utf-8');\n";
  html += "}\n\n";
  html += "async function importBackupToBoard() {\n";
  html += "  const raw = document.getElementById('backupJsonText').value.trim();\n";
  html += "  if (!raw) { alert('Выбери файл бэкапа или вставь JSON'); return; }\n";
  html += "  try {\n";
  html += "    const res = await fetch('/songs/import', {method:'POST', body: new URLSearchParams({json: raw})});\n";
  html += "    if (!res.ok) { setStatusMsg('Ошибка HTTP при импорте: ' + res.status, true); return; }\n";
  html += "    applyData(await res.json(), true); document.getElementById('backupBox').classList.remove('open');\n";
  html += "  } catch (e) { setStatusMsg('Ошибка связи при загрузке бэкапа', true); }\n";
  html += "}\n\n";
  html += "function loadSongIntoEditor(idx) {\n";
  html += "  const s = latestSongs[idx]; if (!s || !Array.isArray(s.steps)) return;\n";
  html += "  document.getElementById('songTitle').value = s.title || '';\n";
  html += "  draftSteps = s.steps.map(x => ({freq: x.freq, dur: x.dur, label: x.label || (x.freq > 0 ? x.freq + 'Гц' : 'Пауза')}));\n";
  html += "  selDraftIdx = draftSteps.length > 0 ? 0 : -1; switchTab('piano'); renderDraftSteps();\n";
  html += "}\n\n";
  html += "async function uploadSongToBoard(playAfter) {\n";
  html += "  const title = document.getElementById('songTitle').value.trim();\n";
  html += "  if (!title) { alert('Введи название песни'); return; }\n";
  html += "  if (draftSteps.length === 0) { alert('Нажми хотя бы одну клавишу или Паузу'); return; }\n";
  html += "  const b1 = document.getElementById('saveSongBtn'); const b2 = document.getElementById('savePlaySongBtn');\n";
  html += "  b1.disabled = true; b2.disabled = true;\n";
  html += "  try {\n";
  html += "    const res = await fetch('/songs/add', {method:'POST', body: new URLSearchParams({title, seq: draftSteps.map(s => s.freq + ':' + s.dur).join(','), play: playAfter ? '1' : '0'})});\n";
  html += "    if (!res.ok) { setStatusMsg('Ошибка сохранения (HTTP ' + res.status + ')', true); return; }\n";
  html += "    const data = await res.json(); applyData(data, true);\n";
  html += "    if (data.status && !data.status.toLowerCase().includes('ошиб')) { document.getElementById('songTitle').value = ''; draftSteps = []; selDraftIdx = -1; renderDraftSteps(); }\n";
  html += "  } catch (e) { setStatusMsg('Не удалось достучаться до ESP32 при сохранении', true); } finally { b1.disabled = false; b2.disabled = false; }\n";
  html += "}\n\n";
  html += "async function playSongOnBoard(idx) { try { const res = await fetch('/songs/play', {method:'POST', body: new URLSearchParams({idx})}); if (res.ok) applyData(await res.json(), false); } catch (e) {} }\n";
  html += "async function deleteSongOnBoard(idx) { try { const res = await fetch('/songs/delete', {method:'POST', body: new URLSearchParams({idx})}); if (res.ok) applyData(await res.json(), true); } catch (e) {} }\n";
  html += "function focusNewNote() { document.getElementById('noteTopic').value = ''; document.getElementById('noteBody').value = ''; document.getElementById('noteTopic').focus(); }\n";
  html += "function escHtml(s) { return String(s || '').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/\"/g,'&quot;'); }\n\n";
  html += "function renderSongs(list) {\n";
  html += "  latestSongs = Array.isArray(list) ? list : [];\n";
  html += "  const box = document.getElementById('songsListBox');\n";
  html += "  if (latestSongs.length === 0) { box.innerHTML = \"<div style='color:#94a3b8;font-size:13px;'>Пока песен нет. Сыграй на пианино выше и нажми «Сохранить на консоль» или загрузи бэкап.</div>\"; return; }\n";
  html += "  box.innerHTML = latestSongs.map((s, i) => `<div class='noteItem'><div class='noteTop'><span class='noteTopic'>${escHtml(s.title)} (${s.count} шагов)</span><div class='row' style='gap:6px;'><button type='button' class='playBtn' onclick='playSongOnBoard(${i})'>Слушать</button><button type='button' class='editBtn' onclick='loadSongIntoEditor(${i})'>В пианино</button><button type='button' class='del' onclick='deleteSongOnBoard(${i})'>Удалить</button></div></div><div class='noteBody'>${escHtml(Array.isArray(s.steps) ? s.steps.map(x => x.label).slice(0, 22).join(' - ') : '')}</div></div>`).join('');\n";
  html += "}\n\n";
  html += "function renderNotes(list) {\n";
  html += "  const box = document.getElementById('notesListBox');\n";
  html += "  if (!Array.isArray(list) || list.length === 0) { box.innerHTML = \"<div style='color:#94a3b8;font-size:13px;'>Пока тем нет. Нажми + и загрузи первую шпаргалку.</div>\"; return; }\n";
  html += "  box.innerHTML = list.map((n, i) => `<div class='noteItem'><div class='noteTop'><span class='noteTopic'>${escHtml(n.topic)}</span><button type='button' class='del' onclick='deleteNote(${i})'>Удалить</button></div><div class='noteBody'>${escHtml(n.body)}</div></div>`).join('');\n";
  html += "}\n\n";
  html += "async function loadNotesList() { try { const res = await fetch('/notes'); if (res.ok) renderNotes(await res.json()); } catch (e) {} }\n";
  html += "async function loadSongsList() { try { const res = await fetch('/songs'); if (res.ok) renderSongs(await res.json()); } catch (e) {} }\n\n";
  html += "function syncRecordsInputs(d) {\n";
  html += "  if (document.activeElement && document.activeElement.closest('#paneRecords')) return;\n";
  html += "  if (typeof d.recSnake === 'number') document.getElementById('recSnakeInp').value = d.recSnake;\n";
  html += "  if (typeof d.recTetris === 'number') document.getElementById('recTetrisInp').value = d.recTetris;\n";
  html += "  if (typeof d.recFlappy === 'number') document.getElementById('recFlappyInp').value = d.recFlappy;\n";
  html += "  if (typeof d.recPong === 'number') document.getElementById('recPongInp').value = d.recPong;\n";
  html += "  if (typeof d.recDino === 'number') document.getElementById('recDinoInp').value = d.recDino;\n";
  html += "  if (typeof d.recBottle === 'number') document.getElementById('recBottleInp').value = d.recBottle;\n";
  html += "  if (typeof d.recDoodle === 'number') document.getElementById('recDoodleInp').value = d.recDoodle;\n";
  html += "}\n\n";
  html += "async function saveRecordsWeb() {\n";
  html += "  const body = new URLSearchParams({ snake: document.getElementById('recSnakeInp').value || 0, tetris: document.getElementById('recTetrisInp').value || 0, flappy: document.getElementById('recFlappyInp').value || 0, pong: document.getElementById('recPongInp').value || 0, dino: document.getElementById('recDinoInp').value || 0, bottle: document.getElementById('recBottleInp').value || 0, doodle: document.getElementById('recDoodleInp').value || 0 });\n";
  html += "  try { const res = await fetch('/records/set', {method:'POST', body}); if (res.ok) applyData(await res.json(), false); } catch (e) {}\n";
  html += "}\n\n";
  html += "function applyData(d, reloadLists) {\n";
  html += "  if (typeof d.rev === 'number') lastRev = d.rev;\n";
  html += "  if (typeof d.god === 'boolean') syncGodUi(d.god);\n";
  html += "  document.getElementById('wState').textContent = d.state;\n";
  html += "  document.getElementById('wCoins').textContent = d.god ? '∞ (БОГ)' : d.coins;\n";
  html += "  document.getElementById('wSpins').textContent = d.totalSpins;\n";
  html += "  syncRecordsInputs(d);\n";
  html += "  setStatusMsg(d.status || '', Boolean(d.status && d.status.toLowerCase().includes('ошиб')));\n";
  html += "  if (d.pianoNote) document.getElementById('pianoInfo').textContent = d.pianoFreq > 0 ? ('Последняя нота: ' + d.pianoNote + ' (' + d.pianoFreq + ' Гц)') : 'Последний шаг: Пауза';\n";
  html += "  if (reloadLists) { if (activeTab === 'piano') loadSongsList(); if (activeTab === 'notes') loadNotesList(); }\n";
  html += "  if (!d.spinning) {\n";
  html += "    if (animTimer) { clearInterval(animTimer); animTimer = null; }\n";
  html += "    ['r1','r2','r3'].forEach(id => document.getElementById(id).classList.remove('spin'));\n";
  html += "    document.getElementById('r1').textContent = d.s1; document.getElementById('r2').textContent = d.s2; document.getElementById('r3').textContent = d.s3;\n";
  html += "    document.getElementById('spinBtn').disabled = false;\n";
  html += "  } else {\n";
  html += "    document.getElementById('spinBtn').disabled = true;\n";
  html += "    ['r1','r2','r3'].forEach(id => document.getElementById(id).classList.add('spin'));\n";
  html += "    if (!animTimer) animTimer = setInterval(() => { document.getElementById('r1').textContent = syms[Math.floor(Math.random()*syms.length)]; document.getElementById('r2').textContent = syms[Math.floor(Math.random()*syms.length)]; document.getElementById('r3').textContent = syms[Math.floor(Math.random()*syms.length)]; }, 90);\n";
  html += "  }\n";
  html += "}\n\n";
  html += "async function refreshStatus(force) {\n";
  html += "  if (document.hidden && !force) return;\n";
  html += "  try { const res = await fetch('/status'); if (!res.ok) return; const d = await res.json(); if (!force && d.rev === lastRev) return; applyData(d, true); } catch (e) {}\n";
  html += "}\n\n";
  html += "async function toggleGod() { try { const res = await fetch('/god', {method:'POST'}); if (res.ok) applyData(await res.json(), false); } catch (e) {} }\n";
  html += "async function toggleDisable() { try { const res = await fetch('/disable', {method:'POST'}); if (res.ok) applyData(await res.json(), false); } catch (e) {} }\n";
  html += "async function sendMode(m) { const res = await fetch('/mode', {method:'POST', body: new URLSearchParams({m})}); if (res.ok) applyData(await res.json(), false); }\n";
  html += "async function doSpin() { document.getElementById('spinBtn').disabled = true; ['r1','r2','r3'].forEach(id => document.getElementById(id).classList.add('spin')); const res = await fetch('/spin', {method:'POST'}); if (res.ok) applyData(await res.json(), false); }\n";
  html += "async function addCoins(customVal) { const val = customVal || document.getElementById('coinInput').value; const res = await fetch('/coins', {method:'POST', body: new URLSearchParams({amount: val})}); if (res.ok) applyData(await res.json(), false); }\n";
  html += "function quickCoins(v) { document.getElementById('coinInput').value = v; addCoins(v); }\n";
  html += "async function uploadNote() { const topic = document.getElementById('noteTopic').value.trim(); const bodyText = document.getElementById('noteBody').value.trim(); if (!topic) { alert('Введи тему шпаргалки'); return; } const res = await fetch('/notes/add', {method:'POST', body: new URLSearchParams({topic, body: bodyText})}); if (res.ok) { document.getElementById('noteTopic').value = ''; document.getElementById('noteBody').value = ''; applyData(await res.json(), true); } }\n";
  html += "async function deleteNote(idx) { const res = await fetch('/notes/delete', {method:'POST', body: new URLSearchParams({idx})}); if (res.ok) applyData(await res.json(), true); }\n\n";
  html += "document.addEventListener('visibilitychange', () => { if (!document.hidden) refreshStatus(false); });\n";
  html += "buildPianoKeyboard(); setInterval(() => refreshStatus(false), 6000); refreshStatus(true);\n";
  html += "</script></body></html>";
  return html;
}

String buildOtaPage() {
  String ipNow = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "ОФФЛАЙН";
  String html;
  html.reserve(2200);
  html += "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>\n";
  html += "<title>ESP32 OTA Update</title><style>\n";
  html += "body{font-family:system-ui,-apple-system,sans-serif;margin:0;padding:18px;background:#0d0f14;color:#f1f5f9;}\n";
  html += ".card{max-width:480px;margin:auto;background:#161b24;padding:18px;border-radius:14px;border:1px solid #283042;}\n";
  html += "h2{margin:0 0 8px;font-size:19px;} p{color:#94a3b8;font-size:13px;line-height:1.45;}\n";
  html += "input[type=file]{width:100%;margin:14px 0;padding:10px;background:#0b0e14;border:1px solid #333d52;border-radius:10px;color:#e2e8f0;}\n";
  html += "button,a{display:inline-block;text-align:center;text-decoration:none;width:100%;padding:11px;margin-top:8px;border-radius:10px;border:0;font-weight:600;font-size:14px;cursor:pointer;}\n";
  html += "button{background:#2563eb;color:#fff;} a{background:#1f2634;color:#cbd5e1;} #prog{margin-top:12px;font-size:13px;color:#60a5fa;}\n";
  html += "</style></head><body><div class='card'>\n";
  html += "  <h2>Обновление ПО (OTA)</h2>\n";
  html += "  <p>Версия сейчас: <b>" + String(FW_VERSION) + "</b> · IP: <b>" + ipNow + "</b><br>Выбери скомпилированный файл <b>.bin</b> и нажми «Прошить».</p>\n";
  html += "  <form id='otaForm' method='POST' action='/update' enctype='multipart/form-data'>\n";
  html += "    <input type='file' name='firmware' accept='.bin' required>\n";
  html += "    <button type='submit'>Прошить ESP32</button>\n";
  html += "  </form>\n";
  html += "  <div id='prog'></div>\n";
  html += "  <a href='/'>Назад в панель</a>\n";
  html += "</div>\n";
  html += "<script>\n";
  html += "const f = document.getElementById('otaForm'); const p = document.getElementById('prog');\n";
  html += "f.addEventListener('submit', e => { e.preventDefault(); const file = f.firmware.files[0]; if (!file) return; const xhr = new XMLHttpRequest(); xhr.open('POST', '/update', true); xhr.upload.onprogress = ev => { if (ev.lengthComputable) p.textContent = 'Загрузка: ' + Math.round((ev.loaded / ev.total) * 100) + '%'; }; xhr.onload = () => { p.textContent = xhr.responseText || 'Готово! Перезагрузка...'; }; xhr.onerror = () => { p.textContent = 'Ошибка передачи файла.'; }; const fd = new FormData(); fd.append('firmware', file, file.name); xhr.send(fd); });\n";
  html += "</script></body></html>";
  return html;
}

void handleRootWeb() { server.send(200, "text/html", buildWebPage()); }
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
    else if (m == "bottle")  { gameState = ST_BOTTLE_GAME; bottleState = 0; lastWebStatus = "Включен Баттл Флип"; }
    else if (m == "flappy")  { startFlappy(); lastWebStatus = "Включен Flappy Bird"; }
    else if (m == "pong")    { startPong(); lastWebStatus = "Включен Понг"; }
    else if (m == "dino")    { startDino(); lastWebStatus = "Включен Динозаврик"; }
    else if (m == "doodle")  { startDoodle(); lastWebStatus = "Включен Doodle Jump"; }
    else if (m == "records") { gameState = ST_RECORDS_SCREEN; lastWebStatus = "Открыты Рекорды"; }
    else if (m == "clock")   { syncNtpTime(); fetchWeatherIfNeeded(false); gameState = ST_NET_CLOCK; lastWebStatus = "Открыты Часы и погода"; }
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
  if (!godMode && coins < spinCost) { autoSpin = false; gameState = ST_NO_MONEY; stateTimer = millis(); lastWebStatus = "Не хватает монет"; }
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
  server.on("/update", HTTP_GET, []() { server.send(200, "text/html", buildOtaPage()); });
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
        while (!incoming.available() && ((unsigned long)(millis() - waitStart) < 150)) yield();
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
  return ((long)(now - timerTargetMs) >= 0) ? 0 : (timerTargetMs - now);
}

void adjustTimerField(int delta) {
  if (timerFieldCursor == 0) timerHoursSet = (timerHoursSet + delta + 24) % 24;
  else if (timerFieldCursor == 1) timerMinutesSet = (timerMinutesSet + delta + 60) % 60;
  else timerSecondsSet = (timerSecondsSet + delta + 60) % 60;
}

void resetSaveData() {
  godMode = false; disabledMode = false;
  coins = 150; displayCoins = 150; totalSpins = 0; maxWin = 0; spinCost = 10;
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
  u8g2.setBusClock(400000);
  u8g2.setContrast(220);

  randomSeed(analogRead(34) + micros());

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

  if (restartAtMs > 0 && (long)(now - restartAtMs) >= 0) ESP.restart();

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
  if (WiFi.status() == WL_CONNECTED && !ntpConfigured && gameState == ST_NET_CLOCK) syncNtpTime();
  if (coins >= 1000) unlockAchievement(3);

  if (timerRunning) {
    if ((long)(now - timerTargetMs) >= 0) {
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

  if (gameState == ST_SPLASH && ((unsigned long)(now - stateTimer) > 2500)) {
    gameState = ST_MAIN_MENU; menuCursor = 0;
  }

  if (gameState == ST_SLOT_GAME) {
    if (autoSpin) { wakeScreen(); startSpin(); }
    else if (!screenSleeping && ((unsigned long)(now - idleTimer) > 25000)) startArchScreensaver();
  }

  if (screenSleeping) updateArchScreensaver();

  if (gameState == ST_SPINNING) handleSpinning();
  if (gameState == ST_SNAKE_GAME) updateSnake();
  if (gameState == ST_TETRIS_GAME) updateTetris();
  if (gameState == ST_FLAPPY_GAME) updateFlappy();
  if (gameState == ST_PONG_GAME) updatePong();
  if (gameState == ST_DINO_GAME) updateDino();
  if (gameState == ST_DOODLE_GAME) updateDoodle();

  if (gameState == ST_WIN_ANIM && ((unsigned long)(now - stateTimer) > 1800)) gameState = ST_SLOT_GAME;
  if (gameState == ST_LOSE_ANIM && ((unsigned long)(now - stateTimer) > 1200)) gameState = ST_SLOT_GAME;
  if (gameState == ST_NO_MONEY && ((unsigned long)(now - stateTimer) > 1500)) gameState = ST_SLOT_GAME;
  if (gameState == ST_BOTTLE_GAME && bottleState >= 2 && ((unsigned long)(now - stateTimer) > 1200)) bottleState = 0;

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
      syncNtpTime(); fetchWeatherIfNeeded(true);
      gameState = ST_WIFI_CONNECTED_INFO;
    } else if ((unsigned long)(now - stateTimer) > 8000) {
      WiFi.disconnect(); gameState = ST_WIFI_ERROR; stateTimer = now;
    }
  }

  if (gameState == ST_WIFI_ERROR && ((unsigned long)(now - stateTimer) > 2500)) {
    gameState = ST_SETTINGS_MENU; menuCursor = 0;
  }

  if (godMode) displayCoins = GOD_COINS_VAL;
  else if (displayCoins != coins) {
    long diff = coins - displayCoins;
    long step = (abs(diff) > 40) ? diff / 4 : (diff > 0 ? 1 : -1);
    if (step == 0) step = (diff > 0 ? 1 : -1);
    displayCoins += step;
  }

  if (gameState != prevObservedState) { prevObservedState = gameState; markDirty(); }

  if ((unsigned long)(now - lastFrameTime) >= 33) { lastFrameTime = now; renderDisplay(); }

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
  dinoY = DINO_GROUND_Y - 12; dinoVY = 0.0; dinoJumping = false; dinoDucking = false; dinoSpeed = 2.6; dinoScore = 0;
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
    if (dinoY >= groundTop) { dinoY = groundTop; dinoVY = 0.0; dinoJumping = false; }
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
  if ((unsigned long)(now - flappyTimer) < 33) return;
  flappyTimer = now;

  flappyVelocity += FLAPPY_GRAVITY; flappyY += flappyVelocity; pipeX -= 2.5f;
  if (pipeX < -PIPE_W) {
    pipeX = 128.0f; pipeGapY = random(16, 38); flappyScore++;
    if (flappyScore > flappyRecord) { flappyRecord = flappyScore; if (!godMode) saveGameData(); }
    beep(1200, 30);
  }

  if (godMode) {
    if (flappyY <= 2.0f) { flappyY = 2.0f; flappyVelocity = 0.5f; }
    if (flappyY >= 58.0f) { flappyY = 58.0f; flappyVelocity = FLAPPY_JUMP * 0.7f; }
    if (30 + 6 >= pipeX && 30 <= pipeX + PIPE_W) {
      float centerGap = pipeGapY + PIPE_GAP_H * 0.5f;
      if (flappyY < pipeGapY + 2 || flappyY > pipeGapY + PIPE_GAP_H - 6) flappyY += (flappyY < centerGap) ? 2.2f : -2.2f;
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
  if (!godMode && coins < spinCost) {
    autoSpin = false; gameState = ST_NO_MONEY; stateTimer = now; lastWebStatus = "Не хватает монет";
    beep(140, 160); markDirty(); return;
  }

  beep(520, 35);
  if (!godMode) coins -= spinCost;
  totalSpins++;
  unlockAchievement(0);
  if (totalSpins >= 50) unlockAchievement(1);
  if (!godMode) saveGameData();

  if (godMode || random(0, 100) < 15) {
    int sym = godMode ? 1 : random(0, ICON_COUNT);
    targetSlot1 = targetSlot2 = targetSlot3 = sym;
  } else {
    targetSlot1 = random(0, ICON_COUNT); targetSlot2 = random(0, ICON_COUNT); targetSlot3 = random(0, ICON_COUNT);
    if (targetSlot1 == targetSlot2 && targetSlot2 == targetSlot3) targetSlot3 = (targetSlot3 + 1) % ICON_COUNT;
  }

  gameState = ST_SPINNING; spinStartTime = now; spinResolved = false;
  reel1Stopped = reel2Stopped = reel3Stopped = false;
  reelOffset1 = reelOffset2 = reelOffset3 = 0; bounce1 = bounce2 = bounce3 = 0;
  markDirty();
}

void handleSpinning() {
  unsigned long now = millis();
  unsigned long elapsed = now - spinStartTime;

  if (elapsed < 1100) { reelOffset1 += 14.0f; if (reelOffset1 >= 48.0f) { reelOffset1 -= 48.0f; prevSlot1 = slot1; slot1 = random(0, ICON_COUNT); } }
  else if (!reel1Stopped) { reel1Stopped = true; reelOffset1 = 0; bounce1 = 8.0f; slot1 = targetSlot1; beep(600, 25); }

  if (elapsed < 1700) { reelOffset2 += 15.5f; if (reelOffset2 >= 48.0f) { reelOffset2 -= 48.0f; prevSlot2 = slot2; slot2 = random(0, ICON_COUNT); } }
  else if (!reel2Stopped) { reel2Stopped = true; reelOffset2 = 0; bounce2 = 8.0f; slot2 = targetSlot2; beep(700, 25); }

  if (elapsed < 2300) { reelOffset3 += 17.0f; if (reelOffset3 >= 48.0f) { reelOffset3 -= 48.0f; prevSlot3 = slot3; slot3 = random(0, ICON_COUNT); } }
  else if (!reel3Stopped) { reel3Stopped = true; reelOffset3 = 0; bounce3 = 9.0f; slot3 = targetSlot3; beep(800, 30); }

  if (bounce1 > 0.2f) bounce1 *= 0.72f; else bounce1 = 0;
  if (bounce2 > 0.2f) bounce2 *= 0.72f; else bounce2 = 0;
  if (bounce3 > 0.2f) bounce3 *= 0.72f; else bounce3 = 0;

  if (elapsed >= 2450 && !spinResolved) { spinResolved = true; checkWin(); }
}

void checkWin() {
  if (slot1 == slot2 && slot2 == slot3) {
    int mult = (slot1 == 1) ? 50 : (slot1 == 3) ? 25 : (slot1 == 4) ? 20 : (slot1 == 2) ? 15 : 6;
    currentWin = spinCost * mult;
    if (godMode) { coins = GOD_COINS_VAL; displayCoins = GOD_COINS_VAL; }
    else { coins += currentWin; if (coins > MAX_COINS) coins = MAX_COINS; }
    if (currentWin > maxWin) maxWin = currentWin;
    if (slot1 == 1) unlockAchievement(2);
    if (!godMode) saveGameData();
    lastWebStatus = "Победа! Куш +" + String(currentWin);
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
    if (canTrigger && ((long)(now - testExitLockUntil) >= 0) && s && u) { lastBtnActionMs = now; beep(1000, 50); gameState = ST_SETTINGS_MENU; menuCursor = 4; testExitLockUntil = now + 300; }
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
      upPressed = u; downPressed = d; leftPressed = l; rightPressed = r; selectPressed = s;
      lastBtnActionMs = now; beep(800, 30); gameState = ST_WIFI_MANAGEMENT; menuCursor = 1;
    }
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
    else if (gameState == ST_DINO_GAME && !dinoJumping) { dinoJumping = true; dinoVY = DINO_JUMP_V; beep(950, 30); }
    else if (gameState == ST_TIMER_SET) { adjustTimerField(1); beep(1000, 20); }
    else if (gameState == ST_WIFI_LIST && wifiNetCount > 0) { menuCursor = (menuCursor - 1 + wifiNetCount) % wifiNetCount; beep(1000, 30); }
    else if (gameState == ST_WIFI_KEYBOARD || gameState == ST_BET_KEYBOARD || gameState == ST_COIN_KEYBOARD) { kbRow = (kbRow - 1 + 5) % 5; beep(1000, 20); }
    else if (gameState == ST_SNAKE_GAME && dy == 0) { dx = 0; dy = -1; beep(900, 20); }
    else if (gameState == ST_FLAPPY_GAME) { flappyVelocity = FLAPPY_JUMP; beep(600, 25); }
    else if (gameState == ST_BOTTLE_GAME && bottleState == 0) { bottleState = 1; bottleAnimTime = now; beep(700, 50); markDirty(); }
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
      if (timerRunning) { timerRemainingMs = ((long)(now - timerTargetMs) >= 0) ? 0 : (timerTargetMs - now); timerRunning = false; }
      else { timerRemainingMs = 0; gameState = ST_TIMER_SET; }
      beep(500, 30);
    } else if (gameState == ST_WIFI_LIST && wifiNetCount > 0) { menuCursor = (menuCursor + 1) % wifiNetCount; beep(800, 30); }
    else if (gameState == ST_WIFI_KEYBOARD || gameState == ST_BET_KEYBOARD || gameState == ST_COIN_KEYBOARD) { kbRow = (kbRow + 1) % 5; beep(800, 20); }
    else if (gameState == ST_SLOT_GAME) { autoSpin = true; beep(880, 80); startSpin(); }
    else if (gameState == ST_SNAKE_GAME && dy == 0) { dx = 0; dy = 1; beep(900, 20); }
    else if (gameState == ST_TETRIS_GAME && !checkTetrisCollision(tetrisX, tetrisY + 1, tetrisCurrRot)) tetrisY++;
  } else if (!d) downPressed = false;

  if (canTrigger && l && !leftPressed) {
    leftPressed = true; lastBtnActionMs = now; wakeScreen();
    if (gameState == ST_NOTE_VIEW) { gameState = ST_NOTES_LIST; beep(500, 35); }
    else if (gameState == ST_NOTES_LIST) { gameState = ST_MAIN_MENU; menuCursor = 8; beep(500, 40); }
    else if (gameState == ST_PIANO_MODE) { stopSongPlayback(); gameState = ST_MUSIC_MENU; menuCursor = 0; beep(500, 40); }
    else if (gameState == ST_METRONOME) { stopMetronome(true); beep(500, 40); }
    else if (gameState == ST_MUSIC_MENU) { gameState = ST_MAIN_MENU; menuCursor = 9; beep(500, 40); }
    else if (gameState == ST_RECORDS_SCREEN) { gameState = ST_MAIN_MENU; menuCursor = 7; beep(500, 40); }
    else if (gameState == ST_CALCULATOR) {
      if (strlen(calcExpr) > 0) { backspaceCalcExpr(); beep(700, 25); }
      else { gameState = ST_MAIN_MENU; menuCursor = 6; beep(500, 40); }
    } else if (gameState == ST_GAME_SELECT) { gameState = ST_MAIN_MENU; menuCursor = 0; beep(500, 40); }
    else if (gameState == ST_TIMER_SET) {
      if (timerFieldCursor > 0) { timerFieldCursor--; beep(700, 20); }
      else { gameState = ST_CLOCK_MENU; menuCursor = 2; beep(500, 40); }
    } else if (gameState == ST_NET_CLOCK || gameState == ST_STOPWATCH || gameState == ST_TIMER_RUNNING) {
      int curIdx = (gameState == ST_NET_CLOCK) ? 0 : (gameState == ST_STOPWATCH) ? 1 : 2;
      if (gameState == ST_TIMER_RUNNING && timerRunning) { timerRemainingMs = ((long)(now - timerTargetMs) >= 0) ? 0 : (timerTargetMs - now); timerRunning = false; }
      gameState = ST_CLOCK_MENU; menuCursor = curIdx; beep(500, 40);
    } else if (gameState == ST_SLOT_GAME) { gameState = ST_GAME_SELECT; menuCursor = 0; beep(500, 40); }
    else if (gameState == ST_INFO_SCREEN || gameState == ST_ACHIEVEMENTS_MENU) { menuCursor = (gameState == ST_INFO_SCREEN) ? 3 : 2; gameState = ST_MAIN_MENU; beep(500, 40); }
    else if (gameState == ST_WIFI_LIST || gameState == ST_WIFI_MANAGEMENT) { gameState = ST_SETTINGS_MENU; menuCursor = 0; beep(500, 40); }
    else if (gameState == ST_SETTINGS_MENU || gameState == ST_CREDITS_MENU || gameState == ST_CLOCK_MENU) { menuCursor = (gameState == ST_SETTINGS_MENU) ? 4 : (gameState == ST_CREDITS_MENU) ? 3 : 5; gameState = ST_MAIN_MENU; beep(500, 40); }
    else if (gameState == ST_WIFI_KEYBOARD) { kbCol = (kbCol - 1 + 10) % 10; beep(900, 20); }
    else if (gameState == ST_BET_KEYBOARD || gameState == ST_COIN_KEYBOARD) { kbCol = (kbCol - 1 + 3) % 3; beep(900, 20); }
    else if (gameState == ST_SNAKE_GAME && dx == 0) { dx = -1; dy = 0; beep(900, 20); }
    else if (gameState == ST_TETRIS_GAME && !checkTetrisCollision(tetrisX - 1, tetrisY, tetrisCurrRot)) { tetrisX--; beep(800, 20); }
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
    else if (gameState == ST_BET_KEYBOARD || gameState == ST_COIN_KEYBOARD) { kbCol = (kbCol + 1) % 3; beep(900, 20); }
    else if (gameState == ST_SNAKE_GAME && dx == 0) { dx = 1; dy = 0; beep(900, 20); }
    else if (gameState == ST_TETRIS_GAME && !checkTetrisCollision(tetrisX + 1, tetrisY, tetrisCurrRot)) { tetrisX++; beep(800, 20); }
  } else if (!r) rightPressed = false;

  if (canTrigger && s && !selectPressed) {
    selectPressed = true; lastBtnActionMs = now; wakeScreen();

    if (gameState == ST_MAIN_MENU) {
      beep(900, 50);
      if (menuCursor == 0) { gameState = ST_GAME_SELECT; menuCursor = 0; }
      else if (menuCursor == 1) { inputBetStr[0] = '\0'; kbRow = 0; kbCol = 0; gameState = ST_BET_KEYBOARD; }
      else if (menuCursor == 2) { gameState = ST_ACHIEVEMENTS_MENU; menuCursor = 0; }
      else if (menuCursor == 3) { gameState = ST_INFO_SCREEN; }
      else if (menuCursor == 4) { gameState = ST_SETTINGS_MENU; menuCursor = 0; }
      else if (menuCursor == 5) { syncNtpTime(); fetchWeatherIfNeeded(false); gameState = ST_CLOCK_MENU; menuCursor = 0; }
      else if (menuCursor == 6) { evaluateCalcExpression(calcExpr); gameState = ST_CALCULATOR; }
      else if (menuCursor == 7) { gameState = ST_RECORDS_SCREEN; }
      else if (menuCursor == 8) { if (noteCursor >= (int)notesList.size()) noteCursor = 0; gameState = ST_NOTES_LIST; }
      else if (menuCursor == 9) { gameState = ST_MUSIC_MENU; menuCursor = 0; }
    } else if (gameState == ST_MUSIC_MENU) {
      beep(900, 50);
      if (menuCursor == 0) { if (pianoSongCursor >= (int)songsList.size()) pianoSongCursor = 0; gameState = ST_PIANO_MODE; }
      else if (menuCursor == 1) { metroRunning = true; metroBeat = 0; nextMetroMs = now; gameState = ST_METRONOME; }
      else { gameState = ST_MAIN_MENU; menuCursor = 9; }
    } else if (gameState == ST_RECORDS_SCREEN) {
      gameState = ST_MAIN_MENU; menuCursor = 7; beep(700, 35);
    } else if (gameState == ST_INFO_SCREEN) {
      gameState = ST_MAIN_MENU; menuCursor = 3; beep(700, 35);
    } else if (gameState == ST_ACHIEVEMENTS_MENU) {
      gameState = ST_MAIN_MENU; menuCursor = 2; beep(700, 35);
    } else if (gameState == ST_CREDITS_MENU) {
      gameState = ST_MAIN_MENU; menuCursor = 4; beep(700, 35);
    } else if (gameState == ST_NOTES_LIST) {
      if (!notesList.empty() && noteCursor >= 0 && noteCursor < (int)notesList.size()) { activeNoteIdx = noteCursor; noteScrollLine = 0; gameState = ST_NOTE_VIEW; beep(1050, 40); }
      else { gameState = ST_MAIN_MENU; menuCursor = 8; beep(700, 35); }
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
      if (menuCursor == 0) { syncNtpTime(); fetchWeatherIfNeeded(false); gameState = ST_NET_CLOCK; }
      else if (menuCursor == 1) gameState = ST_STOPWATCH;
      else if (menuCursor == 2) { if (timerRunning || timerRemainingMs > 0) gameState = ST_TIMER_RUNNING; else { timerFieldCursor = 0; gameState = ST_TIMER_SET; } }
      else { gameState = ST_MAIN_MENU; menuCursor = 5; }
    } else if (gameState == ST_NET_CLOCK) {
      syncNtpTime(); fetchWeatherIfNeeded(true); beep(1000, 40);
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
      else if (menuCursor == 3) { gameState = ST_BOTTLE_GAME; bottleState = 0; bottleScore = 0; }
      else if (menuCursor == 4) startFlappy();
      else if (menuCursor == 5) startPong();
      else if (menuCursor == 6) startDino();
      else if (menuCursor == 7) startDoodle();
      else { gameState = ST_MAIN_MENU; menuCursor = 0; }
    } else if (gameState == ST_SNAKE_GAME || gameState == ST_SNAKE_OVER || gameState == ST_TETRIS_GAME || gameState == ST_TETRIS_OVER ||
               gameState == ST_BOTTLE_GAME || gameState == ST_FLAPPY_GAME || gameState == ST_FLAPPY_OVER ||
               gameState == ST_PONG_GAME || gameState == ST_PONG_OVER || gameState == ST_DINO_GAME || gameState == ST_DINO_OVER ||
               gameState == ST_DOODLE_GAME || gameState == ST_DOODLE_OVER) {
      bottleState = 0; gameState = ST_GAME_SELECT; menuCursor = 0; beep(700, 40);
    } else if (gameState == ST_BET_KEYBOARD || gameState == ST_COIN_KEYBOARD) {
      beep(1100, 40);
      bool isBet = (gameState == ST_BET_KEYBOARD);
      char* buf = isBet ? inputBetStr : inputCoinStr;
      if (kbRow == 4) {
        long val = atol(buf);
        if (val > 0) {
          if (isBet) spinCost = constrain(val, 1L, MAX_COINS);
          else if (!godMode) { coins += val; if (coins > MAX_COINS) coins = MAX_COINS; saveGameData(); markDirty(); }
        }
        buf[0] = '\0'; gameState = isBet ? ST_MAIN_MENU : ST_SETTINGS_MENU; menuCursor = isBet ? 1 : 2;
      } else {
        char ch = NUM_KB_LAYOUT[kbRow][kbCol]; int len = strlen(buf);
        if (ch == '<') { if (len > 0) buf[len - 1] = '\0'; }
        else if (ch == 'X') { buf[0] = '\0'; gameState = isBet ? ST_MAIN_MENU : ST_SETTINGS_MENU; menuCursor = isBet ? 1 : 2; }
        else if (ch != 'O' && len < 6 && ch >= '0' && ch <= '9') { buf[len] = ch; buf[len + 1] = '\0'; }
      }
    } else if (gameState == ST_SETTINGS_MENU) {
      beep(1000, 50);
      if (menuCursor == 0) { gameState = ST_WIFI_MANAGEMENT; menuCursor = 0; }
      else if (menuCursor == 1) { soundEnabled = !soundEnabled; if (!soundEnabled) { noTone(BUZZER_PIN); stopSongPlayback(); stopMetronome(false); } }
      else if (menuCursor == 2) { inputCoinStr[0] = '\0'; kbRow = 0; kbCol = 0; gameState = ST_COIN_KEYBOARD; }
      else if (menuCursor == 3) gameState = ST_RESET_STEP_1;
      else if (menuCursor == 4) gameState = ST_TEST_CONFIRM;
      else if (menuCursor == 5) runUrlOtaUpdate();
      else { gameState = ST_MAIN_MENU; menuCursor = 4; }
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
      else if (kbRow == 4 && kbCol >= 2 && kbCol <= 4) { strcpy(currentSsid, targetSsid); strcpy(currentPass, inputPassword); saveGameData(); ntpConfigured = false; WiFi.mode(WIFI_STA); WiFi.setTxPower(WIFI_POWER_11dBm); WiFi.begin(currentSsid, currentPass); stateTimer = now; gameState = ST_WIFI_CONNECTING; }
      else if (kbRow == 4 && kbCol >= 5) gameState = ST_WIFI_LIST;
      else { char ch = KB_LAYOUT[kbRow][kbCol]; int len = strlen(inputPassword); if (len < 31) { inputPassword[len] = ch; inputPassword[len + 1] = '\0'; } }
    } else if (gameState == ST_SLOT_GAME) {
      startSpin();
    }
  } else if (!s) selectPressed = false;
}

void renderDisplay() {
  unsigned long nowMs = millis();
  u8g2.clearBuffer();

  if (disabledMode) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(14, 28, "РЕЖИМ ОТКЛЮЧЕН");
    u8g2.setFont(u8g2_font_5x7_tf); u8g2.drawStr(14, 46, "WEB: /disable"); u8g2.sendBuffer(); return;
  }

  if (screenSleeping) {
    drawArchLogo((int)archX, (int)archY);
    u8g2.setFont(u8g2_font_5x7_tf); u8g2.drawStr(2, 62, "ARCH LINUX"); u8g2.sendBuffer(); return;
  }

  if (gameState == ST_SPLASH) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(24, 24, "SLOT MACHINE"); u8g2.drawUTF8(32, 44, "Загрузка...");
  } else if (gameState == ST_RECOVERY_MENU) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(14, 11, (String("RECOVERY ") + FW_VERSION).c_str()); u8g2.drawLine(0, 14, 128, 14);
    const char* rItems[4] = {"1. OTA по ссылке", "2. OTA из браузера", "3. Сброс сейвов", "4. Инфо"};
    for (int i = 0; i < 4; i++) {
      int yPos = 16 + (i * 12);
      if (menuCursor == i) { u8g2.drawRBox(2, yPos - 1, 124, 11, 2); u8g2.setDrawColor(0); }
      u8g2.drawUTF8(8, yPos + 9, rItems[i]); u8g2.setDrawColor(1);
    }
  } else if (gameState == ST_OTA_WAIT) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(18, 11, "OTA ОБНОВЛЕНИЕ"); u8g2.drawLine(0, 14, 128, 14); u8g2.drawUTF8(4, 26, "Адрес в браузере:");
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 38, (WiFi.status() == WL_CONNECTED) ? (String("http://") + WiFi.localIP().toString() + "/update").c_str() : "Wi-Fi offline");
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(4, 61, "[<] Назад");
  } else if (gameState == ST_OTA_UPDATING) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(14, 14, "ПРОШИВКА WI-FI"); u8g2.drawStr(48, 34, (String(otaProgressPct) + "%").c_str());
    u8g2.drawRFrame(10, 42, 108, 10, 2);
    int fillW = map(constrain(otaProgressPct, 0, 100), 0, 100, 0, 104);
    if (fillW > 0) u8g2.drawBox(12, 44, fillW, 6);
  } else if (gameState == ST_OTA_DONE) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(20, 26, "ПО ОБНОВЛЕНО!"); u8g2.drawUTF8(16, 44, "Перезагрузка...");
  } else if (gameState == ST_MAIN_MENU) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(24, 11, godMode ? "МЕНЮ [БОГ]" : "ГЛАВНОЕ МЕНЮ"); u8g2.drawLine(0, 13, 128, 13);
    const char* items[10] = {"1. Играть", "2. Своя ставка", "3. Достижения", "4. Информация", "5. Настройки", "6. Часы", "7. Калькулятор", "8. Рекорды", "9. Шпаргалки", "10. Музыка"};
    int startIdx = constrain(menuCursor - 2, 0, 6);
    for (int i = 0; i < 4 && (startIdx + i) < 10; i++) {
      int idx = startIdx + i; int yPos = 16 + (i * 12);
      if (menuCursor == idx) { u8g2.drawRBox(2, yPos - 1, 124, 11, 2); u8g2.setDrawColor(0); }
      u8g2.drawUTF8(8, yPos + 9, items[idx]); u8g2.setDrawColor(1);
    }
  } else if (gameState == ST_MUSIC_MENU) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(36, 11, "МУЗЫКА"); u8g2.drawLine(0, 14, 128, 14);
    const char* mItems[3] = {"1. Песни", "2. Метроном", "3. Назад"};
    for (int i = 0; i < 3; i++) {
      int yPos = 18 + (i * 13);
      if (menuCursor == i) { u8g2.drawRBox(2, yPos - 1, 124, 11, 2); u8g2.setDrawColor(0); }
      u8g2.drawUTF8(8, yPos + 9, mItems[i]); u8g2.setDrawColor(1);
    }
  } else if (gameState == ST_RECORDS_SCREEN) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(32, 11, "РЕКОРДЫ"); u8g2.drawLine(0, 14, 128, 14);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 23, (String("SNAKE: ") + snakeRecord + "   TETRIS: " + tetrisRecord).c_str());
    u8g2.drawStr(4, 32, (String("FLAPPY: ") + flappyRecord + "  PONG:   " + pongRecord).c_str());
    u8g2.drawStr(4, 41, (String("DINO: ") + dinoRecord + "    FLIP:   " + bottleRecord).c_str());
    u8g2.drawStr(4, 50, (String("DOODLE: ") + doodleRecord).c_str());
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(4, 62, "OK: Назад");
  } else if (gameState == ST_PIANO_MODE) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(20, 11, "ПЛЕЕР ПЕСЕН"); u8g2.drawLine(0, 14, 128, 14);
    if (songsList.empty()) {
      u8g2.drawUTF8(4, 28, "Песен пока нет"); u8g2.drawUTF8(4, 42, "Добавь их в веб-меню"); u8g2.drawUTF8(2, 62, "OK или < Назад");
    } else if (songPlaying && activeSongIdx >= 0 && activeSongIdx < (int)songsList.size()) {
      u8g2.drawUTF8(4, 27, utf8Truncate(songsList[activeSongIdx].title, 18).c_str());
      u8g2.drawUTF8(4, 41, (String("Шаг: ") + activeSongStepLabel).c_str());
      if (!soundEnabled) u8g2.drawUTF8(4, 51, "(Звук выкл)");
      u8g2.drawUTF8(2, 62, "OK Стоп  < Назад");
    } else {
      int total = (int)songsList.size(), cur = constrain(pianoSongCursor, 0, total - 1), startIdx = constrain(cur - 1, 0, max(0, total - 2));
      for (int i = 0; i < 2 && (startIdx + i) < total; i++) {
        int idx = startIdx + i; int yPos = 17 + (i * 12);
        if (cur == idx) { u8g2.drawRBox(2, yPos - 1, 124, 11, 2); u8g2.setDrawColor(0); }
        u8g2.drawUTF8(8, yPos + 9, utf8Truncate(songsList[idx].title, 17).c_str()); u8g2.setDrawColor(1);
      }
      if (!soundEnabled) u8g2.drawUTF8(4, 51, "(Звук выкл)");
      u8g2.drawUTF8(2, 62, "^v Выбор OK Слушать");
    }
    } else if (gameState == ST_METRONOME) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(28, 11, "МЕТРОНОМ");
    u8g2.drawLine(0, 14, 128, 14);
    u8g2.drawUTF8(14, 28, (String("Темп: ") + String(metroBpm) + " BPM").c_str());
    u8g2.drawUTF8(14, 42, metroRunning ? (String("Доля: ") + String(metroBeat + 1) + "/4").c_str() : "Остановлен");
    u8g2.drawUTF8(2, 62, metroRunning ? "OK:Стоп  < или ДолгOK" : "±1 ±10 OK:Старт <Вых");
    } else if (gameState == ST_NOTES_LIST) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(28, 11, "ШПАРГАЛКИ"); u8g2.drawLine(0, 14, 128, 14);
    if (notesList.empty()) {
      u8g2.drawUTF8(8, 30, "Шпаргалок пока нет"); u8g2.drawUTF8(8, 44, "Добавь в веб-меню"); u8g2.drawUTF8(8, 61, "OK или < Назад");
    } else {
      int total = (int)notesList.size(), startIdx = constrain(noteCursor - 1, 0, max(0, total - 3));
      for (int i = 0; i < 3 && (startIdx + i) < total; i++) {
        int idx = startIdx + i; int yPos = 17 + (i * 12);
        if (noteCursor == idx) { u8g2.drawRBox(2, yPos - 1, 124, 11, 2); u8g2.setDrawColor(0); }
        u8g2.drawUTF8(8, yPos + 9, utf8Truncate(notesList[idx].topic, 16).c_str()); u8g2.setDrawColor(1);
      }
      u8g2.drawUTF8(4, 62, "OK открыть  < вых");
    }
  } else if (gameState == ST_NOTE_VIEW) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    if (activeNoteIdx < 0 || activeNoteIdx >= (int)notesList.size()) {
      u8g2.drawUTF8(8, 32, "Тема не найдена"); u8g2.drawUTF8(8, 61, "OK или < Назад");
    } else {
      u8g2.drawUTF8(4, 11, utf8Truncate(notesList[activeNoteIdx].topic, 18).c_str()); u8g2.drawLine(0, 14, 128, 14);
      std::vector<String> wrapped; buildNoteLines(notesList[activeNoteIdx].body, wrapped, 20);
      const int visibleRows = 4;
      int maxScroll = max(0, (int)wrapped.size() - visibleRows);
      noteScrollLine = constrain(noteScrollLine, 0, maxScroll);
      for (int i = 0; i < visibleRows && (noteScrollLine + i) < (int)wrapped.size(); i++) {
        u8g2.drawUTF8(4, 25 + (i * 9), wrapped[noteScrollLine + i].c_str());
      }
      u8g2.drawUTF8(4, 63, "^v лист  OK выход");
    }
  } else if (gameState == ST_CALCULATOR) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(20, 11, "КАЛЬКУЛЯТОР");
    u8g2.drawStr(102, 11, (String("[") + CALC_CHARS[calcCharIdx] + "]").c_str()); u8g2.drawLine(0, 14, 128, 14);
    String exprDisp = (strlen(calcExpr) > 0) ? String(calcExpr) : String("(пусто)");
    if (exprDisp.length() > 19) exprDisp = exprDisp.substring(exprDisp.length() - 19);
    u8g2.drawUTF8(4, 27, exprDisp.c_str());
    if (strlen(calcExpr) == 0) u8g2.drawStr(4, 43, "= 0");
    else if (calcError) u8g2.drawUTF8(4, 43, (String("! ") + calcErrMsg).c_str());
    else if (calcHasResult) {
      long asLong = (long)calcResult;
      String resStr = (fabs(calcResult - (double)asLong) < 0.0001 && fabs(calcResult) < 1e9) ? ("= " + String(asLong)) : ("= " + String(calcResult, 3));
      u8g2.drawStr(4, 43, resStr.c_str());
    }
    u8g2.drawUTF8(2, 61, "^v Симв >+ <-Стр");
  } else if (gameState == ST_GAME_SELECT) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(28, 11, "ВЫБОР ИГРЫ"); u8g2.drawLine(0, 14, 128, 14);
    const char* gItems[9] = {"1. Слоты", "2. Змейка", "3. Тетрис", "4. Баттл Флип", "5. Flappy Bird", "6. Понг", "7. Динозаврик", "8. Doodle Jump", "9. Назад"};
    int startIdx = constrain(menuCursor - 2, 0, 5);
    for (int i = 0; i < 4 && (startIdx + i) < 9; i++) {
      int idx = startIdx + i; int yPos = 16 + (i * 12);
      if (menuCursor == idx) { u8g2.drawRBox(2, yPos - 1, 124, 11, 2); u8g2.setDrawColor(0); }
      u8g2.drawUTF8(8, yPos + 9, gItems[idx]); u8g2.setDrawColor(1);
    }
  } else if (gameState == ST_DOODLE_GAME) {
    u8g2.drawLine(0, 10, 128, 10); u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 8, (String("SCORE: ") + String(doodleScore)).c_str()); u8g2.drawStr(74, 8, (String("BEST: ") + String(doodleRecord)).c_str());
    for (int i = 0; i < DOODLE_PLAT_COUNT; i++) {
      int px = (int)doodlePlats[i].x, py = (int)doodlePlats[i].y;
      if (py >= 11 && py <= 63) u8g2.drawRBox(px, py, doodlePlats[i].w, 3, 1);
    }
    int dxPos = (int)doodleX, dyPos = (int)doodleY;
    u8g2.drawBox(dxPos + 1, dyPos, 5, 5); u8g2.drawBox(dxPos, dyPos + 5, 7, 3);
  } else if (gameState == ST_DINO_GAME) {
    u8g2.drawLine(0, 10, 128, 10); u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 8, (String("SCORE: ") + String(dinoScore)).c_str()); u8g2.drawStr(74, 8, (String("BEST: ") + String(dinoRecord)).c_str());
    u8g2.drawLine(0, DINO_GROUND_Y, 128, DINO_GROUND_Y);
    int dyPos = (int)dinoY;
    if (dinoDucking) { u8g2.drawBox(DINO_X, dyPos, 10, 7); u8g2.drawBox(DINO_X + 7, dyPos - 2, 4, 3); }
    else {
      u8g2.drawBox(DINO_X, dyPos, 8, 9); u8g2.drawBox(DINO_X + 4, dyPos - 3, 5, 4);
      if ((nowMs / 110) % 2 == 0) { u8g2.drawBox(DINO_X + 1, dyPos + 9, 2, 3); u8g2.drawBox(DINO_X + 5, dyPos + 8, 2, 2); }
      else { u8g2.drawBox(DINO_X + 1, dyPos + 8, 2, 2); u8g2.drawBox(DINO_X + 5, dyPos + 9, 2, 3); }
    }
    int cxPos = (int)cactusX, cTop = DINO_GROUND_Y - cactusH;
    u8g2.drawBox(cxPos + cactusW / 2 - 1, cTop, 3, cactusH);
    if (cactusW >= 7) u8g2.drawBox(cxPos, cTop + 4, 2, 4);
    if (cactusW >= 10) u8g2.drawBox(cxPos + cactusW - 2, cTop + 3, 2, 5);
  } else if (gameState == ST_CLOCK_MENU) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(40, 11, "ЧАСЫ"); u8g2.drawLine(0, 14, 128, 14);
    const char* cItems[4] = {"1. Время по сети", "2. Секундомер", "3. Таймер", "4. Назад"};
    for (int i = 0; i < 4; i++) {
      int yPos = 16 + (i * 12);
      if (menuCursor == i) { u8g2.drawRBox(2, yPos - 1, 124, 11, 2); u8g2.setDrawColor(0); }
      u8g2.drawUTF8(8, yPos + 9, cItems[i]); u8g2.setDrawColor(1);
    }
  } else if (gameState == ST_NET_CLOCK) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(22, 11, "ВРЕМЯ ПО СЕТИ"); u8g2.drawLine(0, 14, 128, 14);
    struct tm timeinfo;
    if (WiFi.status() == WL_CONNECTED && getLocalTime(&timeinfo, 25)) {
      char buf[16], dBuf[16];
      strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo); u8g2.setFont(u8g2_font_10x20_tn); u8g2.drawStr(20, 35, buf);
      strftime(dBuf, sizeof(dBuf), "%d.%m.%Y", &timeinfo); u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawStr(32, 49, dBuf);
      if (weatherValid) {
        int tRound = (int)roundf(weatherTempC);
        u8g2.drawUTF8(8, 61, utf8Truncate((tRound > 0 ? "+" : "") + String(tRound) + "C " + weatherCondition, 19).c_str());
      } else u8g2.drawUTF8(18, 61, "OK - погода");
    } else {
      u8g2.drawUTF8(16, 30, (WiFi.status() == WL_CONNECTED) ? "Синхронизация..." : "Нет Wi-Fi");
      u8g2.drawUTF8(12, 46, "< Назад");
    }
  } else if (gameState == ST_STOPWATCH) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(30, 11, "СЕКУНДОМЕР"); u8g2.drawLine(0, 14, 128, 14);
    unsigned long ms = getCurrentStopwatchMs();
    char swBuf[16]; snprintf(swBuf, sizeof(swBuf), "%02u:%02u.%u", (unsigned int)((ms / 60000UL) % 100UL), (unsigned int)((ms / 1000UL) % 60UL), (unsigned int)((ms / 100UL) % 10UL));
    u8g2.setFont(u8g2_font_10x20_tn); u8g2.drawStr(22, 37, swBuf);
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(6, 56, stopwatchRunning ? "OK:Стоп" : "OK:Старт"); u8g2.drawUTF8(70, 56, "v:Сброс");
  } else if (gameState == ST_TIMER_SET) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(20, 11, "УСТАНОВКА ТАЙМЕРА"); u8g2.drawLine(0, 14, 128, 14);
    char hBuf[4], mBuf[4], sBuf[4];
    snprintf(hBuf, sizeof(hBuf), "%02d", timerHoursSet); snprintf(mBuf, sizeof(mBuf), "%02d", timerMinutesSet); snprintf(sBuf, sizeof(sBuf), "%02d", timerSecondsSet);
    u8g2.setFont(u8g2_font_10x20_tn);
    if (timerFieldCursor == 0) { u8g2.drawBox(16, 20, 24, 22); u8g2.setDrawColor(0); } u8g2.drawStr(18, 38, hBuf); u8g2.setDrawColor(1); u8g2.drawStr(42, 38, ":");
    if (timerFieldCursor == 1) { u8g2.drawBox(52, 20, 24, 22); u8g2.setDrawColor(0); } u8g2.drawStr(54, 38, mBuf); u8g2.setDrawColor(1); u8g2.drawStr(78, 38, ":");
    if (timerFieldCursor == 2) { u8g2.drawBox(88, 20, 24, 22); u8g2.setDrawColor(0); } u8g2.drawStr(90, 38, sBuf); u8g2.setDrawColor(1);
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(6, 56, "<>:Поле"); u8g2.drawUTF8(68, 56, "OK:Старт");
  } else if (gameState == ST_TIMER_RUNNING) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(42, 11, "ТАЙМЕР"); u8g2.drawLine(0, 14, 128, 14);
    unsigned long totalSec = (getCurrentTimerRemainingMs() + 999UL) / 1000UL;
    char tBuf[16]; snprintf(tBuf, sizeof(tBuf), "%02u:%02u:%02u", (unsigned int)((totalSec / 3600UL) % 100UL), (unsigned int)((totalSec / 60UL) % 60UL), (unsigned int)(totalSec % 60UL));
    u8g2.setFont(u8g2_font_10x20_tn); u8g2.drawStr(16, 37, tBuf);
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(6, 56, timerRunning ? "OK:Пауза" : "OK:Прод."); u8g2.drawUTF8(70, 56, "v:Сброс");
  } else if (gameState == ST_TIMER_DONE) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(26, 26, "ВРЕМЯ ВЫШЛО!"); u8g2.drawUTF8(14, 44, "Нажми кнопку...");
  } else if (gameState == ST_BET_KEYBOARD || gameState == ST_COIN_KEYBOARD) {
    bool isBet = (gameState == ST_BET_KEYBOARD); char* buf = isBet ? inputBetStr : inputCoinStr;
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(4, 10, isBet ? "СТАВКА:" : "МОНЕТ:"); u8g2.drawStr(62, 10, buf); u8g2.drawLine(0, 12, 128, 12);
    for (int r = 0; r < 5; r++) for (int c = 0; c < 3; c++) {
      char ch = NUM_KB_LAYOUT[r][c]; int x = 16 + c * 36, y = 14 + r * 10;
      if (r == kbRow && c == kbCol) { u8g2.drawBox(x - 2, y, 22, 9); u8g2.setDrawColor(0); }
      char label[3] = {ch, '\0', '\0'};
      if (r == 4) { strcpy(label, "ОК"); if (c == 1) x += 12; else if (c == 2) x += 24; }
      u8g2.drawUTF8(x, y + 7, label); u8g2.setDrawColor(1);
    }
  } else if (gameState == ST_ACHIEVEMENTS_MENU) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(28, 11, "ДОСТИЖЕНИЯ"); u8g2.drawLine(0, 14, 128, 14);
    int startIdx = constrain(menuCursor - 1, 0, ACH_COUNT - 2);
    for (int i = 0; i < 2; i++) {
      int idx = startIdx + i; int y = 18 + i * 22;
      if (menuCursor == idx) u8g2.drawRFrame(2, y - 2, 124, 20, 2);
      u8g2.drawUTF8(6, y + 7, achievements[idx].title);
      u8g2.drawUTF8(6, y + 17, achievements[idx].unlocked ? "[ОТКРЫТО]" : achievements[idx].desc);
    }
  } else if (gameState == ST_INFO_SCREEN) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(24, 11, "ИНФОРМАЦИЯ"); u8g2.drawLine(0, 14, 128, 14);
    u8g2.drawStr(4, 24, (String("VER: ") + FW_VERSION).c_str());
    u8g2.drawStr(4, 34, (String("SPINS: ") + totalSpins + " WIN: " + maxWin).c_str());
    u8g2.drawStr(4, 44, (String("SNK:") + snakeRecord + " TET:" + tetrisRecord + " DOO:" + doodleRecord).c_str());
    u8g2.drawStr(4, 54, (String("IP: ") + ((WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "OFF")).c_str());
  } else if (gameState == ST_CREDITS_MENU) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(24, 16, "СИСТЕМА ГОТОВА"); u8g2.drawUTF8(32, 34, "ESP32 OLED"); u8g2.drawUTF8(28, 52, "OK: Назад");
  } else if (gameState == ST_SETTINGS_MENU) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(32, 11, "НАСТРОЙКИ"); u8g2.drawLine(0, 14, 128, 14);
    const char* sItems[7] = {"1. Wi-Fi сеть", soundEnabled ? "2. Звук: ВКЛ" : "2. Звук: ВЫКЛ", "3. Добавить монет", "4. Сброс сейвов", "5. Тест режим", "6. OTA с GitHub", "7. Назад"};
    int startIdx = constrain(menuCursor - 2, 0, 3);
    for (int i = 0; i < 4 && (startIdx + i) < 7; i++) {
      int idx = startIdx + i; int yPos = 16 + (i * 12);
      if (menuCursor == idx) { u8g2.drawRBox(2, yPos - 1, 124, 11, 2); u8g2.setDrawColor(0); }
      u8g2.drawUTF8(8, yPos + 9, sItems[idx]); u8g2.setDrawColor(1);
    }
  } else if (gameState == ST_WIFI_MANAGEMENT) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(28, 11, "УПРАВЛЕНИЕ WI-FI"); u8g2.drawLine(0, 14, 128, 14);
    const char* wItems[5] = {(WiFi.status() == WL_CONNECTED) ? "1. Сменить сеть" : "1. Подключить Wi-Fi", "2. Показать IP", (WiFi.status() == WL_CONNECTED) ? "3. Отключиться" : "3. (Не в сети)", "4. Сканировать", "5. Назад"};
    int startIdx = constrain(menuCursor - 1, 0, 1);
    for (int i = 0; i < 4 && (startIdx + i) < 5; i++) {
      int idx = startIdx + i; int yPos = 16 + (i * 12);
      if (menuCursor == idx) { u8g2.drawRBox(2, yPos - 1, 124, 11, 2); u8g2.setDrawColor(0); }
      u8g2.drawUTF8(8, yPos + 9, wItems[idx]); u8g2.setDrawColor(1);
    }
  } else if (gameState == ST_WIFI_CONNECTED_INFO) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(18, 11, "СТАТУС ПОДКЛЮЧЕНИЯ"); u8g2.drawLine(0, 14, 128, 14);
    if (WiFi.status() == WL_CONNECTED) {
      u8g2.drawUTF8(4, 25, (String("Сеть: ") + WiFi.SSID()).c_str()); u8g2.drawStr(4, 37, (String("IP: ") + WiFi.localIP().toString()).c_str()); u8g2.drawUTF8(4, 49, "Подключено!");
    } else { u8g2.drawUTF8(4, 28, "Состояние: ОФФЛАЙН"); u8g2.drawUTF8(4, 42, "Нет активной сети"); }
    u8g2.drawUTF8(4, 61, "Нажми кнопку...");
  } else if (gameState == ST_RESET_STEP_1 || gameState == ST_RESET_STEP_2 || gameState == ST_RESET_STEP_3) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(20, 12, gameState == ST_RESET_STEP_1 ? "СБРОС (ШАГ 1/3)" : gameState == ST_RESET_STEP_2 ? "ТОЧНО? (ШАГ 2/3)" : "ПОСЛЕДНИЙ ШАНС!");
    u8g2.drawUTF8(10, 30, "Удалить все сейвы?"); u8g2.drawUTF8(10, 44, "^: ДА"); u8g2.drawUTF8(64, 44, "v/<: НЕТ");
  } else if (gameState == ST_TEST_CONFIRM) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(24, 16, "ТЕСТОВЫЙ РЕЖИМ"); u8g2.drawUTF8(12, 32, "Запустить мелодию?");
    u8g2.drawUTF8(12, 46, "OK: ДА"); u8g2.drawUTF8(68, 46, "^: НЕТ");
  } else if (gameState == ST_TEST_MELODY) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(18, 28, "ИГРАЕТ МЕЛОДИЯ"); u8g2.drawUTF8(16, 44, "Jingle Bells...");
  } else if (gameState == ST_TEST_MODE) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(24, 12, "ТЕСТОВЫЙ РЕЖИМ"); u8g2.drawLine(0, 15, 128, 15);
    u8g2.drawStr(6, 28, (String("U:") + (digitalRead(BTN_UP_PIN)==LOW?"1":"0") + " D:" + (digitalRead(BTN_DOWN_PIN)==LOW?"1":"0") + " L:" + (digitalRead(BTN_LEFT_PIN)==LOW?"1":"0")).c_str());
    u8g2.drawStr(6, 40, (String("R:") + (digitalRead(BTN_RIGHT_PIN)==LOW?"1":"0") + " OK:" + (digitalRead(BTN_SELECT_PIN)==LOW?"1":"0")).c_str());
    u8g2.drawUTF8(6, 56, "Выход: OK + ^");
  } else if (gameState == ST_WIFI_SCAN) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(20, 26, "СКАНИРОВАНИЕ"); u8g2.drawUTF8(16, 42, "Поиск сетей Wi-Fi...");
  } else if (gameState == ST_WIFI_LIST) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(28, 11, "СПИСОК СЕТЕЙ"); u8g2.drawLine(0, 14, 128, 14);
    if (wifiNetCount <= 0) { u8g2.drawUTF8(12, 32, "Сети не найдены"); u8g2.drawUTF8(12, 48, "< Назад"); }
    else {
      int startIdx = constrain(menuCursor - 1, 0, max(0, wifiNetCount - 3));
      for (int i = 0; i < 3 && (startIdx + i) < wifiNetCount; i++) {
        int idx = startIdx + i; int yPos = 17 + (i * 12);
        if (menuCursor == idx) { u8g2.drawRBox(2, yPos - 1, 124, 11, 2); u8g2.setDrawColor(0); }
        u8g2.drawUTF8(8, yPos + 9, WiFi.SSID(idx).substring(0, 16).c_str()); u8g2.setDrawColor(1);
      }
    }
  } else if (gameState == ST_WIFI_KEYBOARD) {
    u8g2.setFont(u8g2_font_5x7_tf); u8g2.drawStr(4, 7, (String("PASS: ") + inputPassword).c_str()); u8g2.drawLine(0, 9, 128, 9);
    for (int r = 0; r < 5; r++) for (int c = 0; c < 10; c++) {
      char ch = KB_LAYOUT[r][c]; if (ch == ' ' && r != 4) continue;
      int x = 4 + c * 12, y = 11 + r * 10;
      if (r == kbRow && c == kbCol) { u8g2.drawBox(x - 1, y, (r == 4 && c >= 2 && c <= 4) ? 30 : 10, 9); u8g2.setDrawColor(0); }
      if (r == 4 && c == 2) u8g2.drawUTF8(x, y + 7, "ОК");
      else if (r == 4 && c >= 3 && c <= 4) { }
      else { char buf[2] = {ch, '\0'}; u8g2.drawStr(x + 2, y + 7, buf); }
      u8g2.setDrawColor(1);
    }
  } else if (gameState == ST_WIFI_CONNECTING) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(18, 24, "ПОДКЛЮЧЕНИЕ..."); u8g2.drawUTF8(18, 42, targetSsid);
  } else if (gameState == ST_WIFI_ERROR) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(20, 26, "ОШИБКА СЕТИ!"); u8g2.drawUTF8(16, 42, "Неверный пароль");
  } else if (gameState == ST_SNAKE_GAME) {
    for (int i = 0; i < snakeLen; i++) u8g2.drawBox(snake[i].x * 8 + 1, snake[i].y * 8 + 1, 6, 6);
    u8g2.drawFrame(food.x * 8 + 1, food.y * 8 + 1, 6, 6);
    u8g2.setFont(u8g2_font_5x7_tf); u8g2.drawStr(2, 7, (String("SCORE: ") + snakeScore).c_str());
  } else if (gameState == ST_SNAKE_OVER || gameState == ST_TETRIS_OVER || gameState == ST_FLAPPY_OVER || gameState == ST_PONG_OVER || gameState == ST_DINO_OVER || gameState == ST_DOODLE_OVER) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(26, 18, "ИГРА ОКОНЧЕНА!");
    int sc = (gameState == ST_SNAKE_OVER) ? snakeScore : (gameState == ST_TETRIS_OVER) ? tetrisScore : (gameState == ST_FLAPPY_OVER) ? flappyScore : (gameState == ST_PONG_OVER) ? pongScore : (gameState == ST_DINO_OVER) ? dinoScore : doodleScore;
    int rc = (gameState == ST_SNAKE_OVER) ? snakeRecord : (gameState == ST_TETRIS_OVER) ? tetrisRecord : (gameState == ST_FLAPPY_OVER) ? flappyRecord : (gameState == ST_PONG_OVER) ? pongRecord : (gameState == ST_DINO_OVER) ? dinoRecord : doodleRecord;
    u8g2.drawStr(24, 34, (String("SCORE: ") + sc).c_str()); u8g2.drawStr(24, 46, (String("BEST: ") + rc).c_str());
    u8g2.drawUTF8(24, 60, "OK: Выход");
  } else if (gameState == ST_TETRIS_GAME) {
    const int ox = 34, oy = 2, cs = 3;
    u8g2.drawFrame(ox - 1, oy - 1, TETRIS_W * cs + 2, TETRIS_H * cs + 2);
    for (int r = 0; r < TETRIS_H; r++) for (int c = 0; c < TETRIS_W; c++) if (tetrisBoard[r][c]) u8g2.drawBox(ox + c * cs, oy + r * cs, cs, cs);
    byte mat[4][4]; getRotatedShape(tetrisCurrShape, tetrisCurrRot, mat);
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) if (mat[r][c]) {
      int bx = tetrisX + c, by = tetrisY + r;
      if (by >= 0 && by < TETRIS_H && bx >= 0 && bx < TETRIS_W) u8g2.drawBox(ox + bx * cs, oy + by * cs, cs, cs);
    }
    u8g2.setFont(u8g2_font_5x7_tf); u8g2.drawStr(72, 12, "TETRIS"); u8g2.drawStr(72, 24, (String("SC:") + tetrisScore).c_str()); u8g2.drawStr(72, 34, (String("LV:") + tetrisLevel).c_str()); u8g2.drawStr(72, 46, (String("HI:") + tetrisRecord).c_str());
  } else if (gameState == ST_BOTTLE_GAME) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(22, 11, "БАТТЛ ФЛИП"); u8g2.drawLine(0, 14, 128, 14);
    int bY = 34;
    if (bottleState == 1) {
      if ((unsigned long)(nowMs - bottleAnimTime) < 600) { bY = 18; u8g2.drawBox(60, bY, 8, 16); }
      else { bottleState = (random(0, 100) < 65) ? 2 : 3; stateTimer = nowMs; if (bottleState == 2) { bottleScore++; if (bottleScore > bottleRecord) { bottleRecord = bottleScore; if (!godMode) saveGameData(); } beep(1200, 100); } else { bottleScore = 0; beep(200, 200); } }
    }
    if (bottleState == 0 || bottleState == 2) u8g2.drawBox(60, bY, 8, 16);
    else if (bottleState == 3) u8g2.drawBox(56, bY + 6, 16, 8);
    u8g2.setFont(u8g2_font_5x7_tf); u8g2.drawStr(4, 58, (String("SCORE: ") + bottleScore + "  BEST: " + bottleRecord).c_str());
  } else if (gameState == ST_FLAPPY_GAME) {
    u8g2.drawBox(30, (int)flappyY, 6, 6);
    int px = (int)pipeX;
    u8g2.drawBox(px, 0, PIPE_W, pipeGapY);
    u8g2.drawBox(px, pipeGapY + PIPE_GAP_H, PIPE_W, 64 - (pipeGapY + PIPE_GAP_H));
    u8g2.setFont(u8g2_font_5x7_tf); u8g2.drawStr(4, 10, (String("SCORE: ") + flappyScore).c_str());
  } else if (gameState == ST_PONG_GAME) {
    u8g2.drawBox((int)paddleX, 58, PADDLE_W, PADDLE_H); u8g2.drawBox((int)ballX, (int)ballY, 3, 3);
    u8g2.setFont(u8g2_font_5x7_tf); u8g2.drawStr(4, 10, (String("SCORE: ") + pongScore).c_str());
  } else {
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 8, godMode ? "GOD" : (String("COINS:") + displayCoins).c_str());
    u8g2.drawStr(74, 8, (String("BET:") + spinCost).c_str());
    if (autoSpin) u8g2.drawStr(54, 16, "AUTO");

    const int reelY = 18, reelH = 36, reelW = 38, x1 = 4, x2 = 45, x3 = 86;
    u8g2.drawRFrame(x1, reelY, reelW, reelH, 4); u8g2.drawRFrame(x2, reelY, reelW, reelH, 4); u8g2.drawRFrame(x3, reelY, reelW, reelH, 4);

    if (gameState == ST_SPINNING) {
      u8g2.setClipWindow(x1 + 2, reelY + 2, x1 + reelW - 3, reelY + reelH - 3);
      drawSymbolAt(slot1, x1 + 18, 35 + (int)reelOffset1 - (int)bounce1);
      if (!reel1Stopped) drawSymbolAt(prevSlot1, x1 + 18, 35 + (int)reelOffset1 - 48);
      u8g2.setMaxClipWindow();

      u8g2.setClipWindow(x2 + 2, reelY + 2, x2 + reelW - 3, reelY + reelH - 3);
      drawSymbolAt(slot2, x2 + 18, 35 + (int)reelOffset2 - (int)bounce2);
      if (!reel2Stopped) drawSymbolAt(prevSlot2, x2 + 18, 35 + (int)reelOffset2 - 48);
      u8g2.setMaxClipWindow();

      u8g2.setClipWindow(x3 + 2, reelY + 2, x3 + reelW - 3, reelY + reelH - 3);
      drawSymbolAt(slot3, x3 + 18, 35 + (int)reelOffset3 - (int)bounce3);
      if (!reel3Stopped) drawSymbolAt(prevSlot3, x3 + 18, 35 + (int)reelOffset3 - 48);
      u8g2.setMaxClipWindow();
    } else {
      drawSymbolAt(slot1, x1 + 18, 35 - (int)bounce1);
      drawSymbolAt(slot2, x2 + 18, 35 - (int)bounce2);
      drawSymbolAt(slot3, x3 + 18, 35 - (int)bounce3);
    }

    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    if (gameState == ST_WIN_ANIM) u8g2.drawUTF8(22, 62, (String("КУШ +") + currentWin + "!").c_str());
    else if (gameState == ST_LOSE_ANIM) u8g2.drawUTF8(30, 62, "МИМО :(");
    else if (gameState == ST_NO_MONEY) u8g2.drawUTF8(20, 62, "МАЛО МОНЕТ");
    else u8g2.drawUTF8(24, 62, "OK: КРУТИТЬ");
  }

  if (activeAchPopup >= 0 && ((unsigned long)(nowMs - achPopupTimer) < 2200)) {
    u8g2.drawRBox(10, 14, 108, 36, 4); u8g2.setDrawColor(0); u8g2.drawRFrame(12, 16, 104, 32, 3);
    u8g2.setFont(u8g2_font_6x12_t_cyrillic); u8g2.drawUTF8(20, 28, "АЧИВКА!"); u8g2.drawUTF8(20, 42, achievements[activeAchPopup].title);
    u8g2.setDrawColor(1);
  } else {
    activeAchPopup = -1;
  }

  u8g2.sendBuffer();
}

void drawSymbolAt(int type, int cx, int cy) {
  switch (type) {
    case 0:
      u8g2.drawDisc(cx - 5, cy + 4, 3); u8g2.drawDisc(cx + 5, cy + 5, 3);
      u8g2.drawLine(cx - 5, cy + 2, cx, cy - 8); u8g2.drawLine(cx + 5, cy + 3, cx, cy - 8);
      break;
    case 1:
      u8g2.setFont(u8g2_font_9x15_tf); u8g2.drawStr(cx - 4, cy + 6, "7");
      break;
    case 2:
      u8g2.drawDisc(cx, cy, 6); u8g2.setDrawColor(0); u8g2.drawDisc(cx + 3, cy - 2, 5); u8g2.setDrawColor(1);
      break;
    case 3:
      u8g2.drawDisc(cx - 4, cy - 3, 2); u8g2.drawDisc(cx + 4, cy - 3, 2);
      u8g2.drawDisc(cx - 2, cy + 2, 2); u8g2.drawDisc(cx + 2, cy + 2, 2); u8g2.drawDisc(cx, cy + 6, 2);
      break;
    default:
      u8g2.drawLine(cx - 5, cy - 5, cx + 5, cy + 5); u8g2.drawLine(cx + 5, cy - 5, cx - 5, cy + 5);
      break;
  }
}

void beep(int freq, int duration) {
  if (soundEnabled && freq > 0 && duration > 0) tone(BUZZER_PIN, freq, duration);
}

void startToneSeq(const ToneStep* seq, uint8_t len, bool returnToTest) {
  noTone(BUZZER_PIN); activeSeq = nullptr; activeSeqLen = 0; activeSeqIdx = 0; returnToTestModeAfterSeq = false;
  if (!soundEnabled || !seq || len == 0) {
    if (returnToTest) { testMelodyStarted = false; gameState = ST_TEST_MODE; testExitLockUntil = millis() + 300; }
    return;
  }
  activeSeq = seq; activeSeqLen = len; activeSeqIdx = 0; returnToTestModeAfterSeq = returnToTest;
  tone(BUZZER_PIN, seq[0].freq, seq[0].dur);
  nextToneAtMs = millis() + (unsigned long)seq[0].dur + (unsigned long)seq[0].gap;
}

void updateToneSeq(unsigned long now) {
  if (!activeSeq || activeSeqIdx >= activeSeqLen) return;
  if (!soundEnabled) { noTone(BUZZER_PIN); activeSeq = nullptr; activeSeqLen = 0; activeSeqIdx = 0; return; }
  if ((long)(now - nextToneAtMs) < 0) return;

  activeSeqIdx++;
  if (activeSeqIdx >= activeSeqLen) {
    bool goTest = returnToTestModeAfterSeq;
    activeSeq = nullptr; activeSeqLen = 0; activeSeqIdx = 0; returnToTestModeAfterSeq = false;
    noTone(BUZZER_PIN);
    if (goTest && gameState == ST_TEST_MELODY) { testMelodyStarted = false; gameState = ST_TEST_MODE; testExitLockUntil = now + 300; }
    return;
  }
  tone(BUZZER_PIN, activeSeq[activeSeqIdx].freq, activeSeq[activeSeqIdx].dur);
  nextToneAtMs = now + (unsigned long)activeSeq[activeSeqIdx].dur + (unsigned long)activeSeq[activeSeqIdx].gap;
}

void playWinSound()    { startToneSeq(WIN_SEQ,     sizeof(WIN_SEQ)     / sizeof(ToneStep), false); }
void playLoseSound()   { startToneSeq(LOSE_SEQ,    sizeof(LOSE_SEQ)    / sizeof(ToneStep), false); }
void playJackpotSound(){ startToneSeq(JACKPOT_SEQ, sizeof(JACKPOT_SEQ) / sizeof(ToneStep), false); }
void playJingleBells() { startToneSeq(JINGLE_SEQ,  sizeof(JINGLE_SEQ)  / sizeof(ToneStep), true);  }
