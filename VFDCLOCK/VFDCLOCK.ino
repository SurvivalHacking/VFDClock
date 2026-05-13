// VFD Clock V1.0 by Davide Gatti - Survival Hacking - www.survivalhacking.it - 12/2025
// 
// Implementazione software per il dispositivo hardware aliexpress: https://is.gd/vfdclock 
// E' stato fatto un reverse engineering dei collegamenti dei vari dispositivi e riscritto un firmware più completo e configurabile 
// per un orologio / sveglia collegato alla rete WIFI per il recupero data/ora viam NTP e utilizzo dell'RTC come backup.
// E' stata implementata anche una pagina WEB per la configurazione avanzata.
// 
// Funzionamento:
// * Al primo avvio, usando un telefonino, collegarsi alla rete VFDClock e impostare la connseeione WIFI. Se una volta collegato
//   non apparirà la schermata di impostazioni, andare con il browser all'indirizzo 192.16.4.1 
// * Premendo il pulsante è possibile vedere in sequenza Data/Sveglia/Stato sveglia/Indirizzo IP
// * Premendo il pulsante per 2 secondi si passa alla modalità di regolazione della sveglia
//   in quella modalità premendo il pulsante brevemente si potrà incrementare l'elemento selezionato. Tenendo premuto a lungo, si passerà
//   all'elemento successivo. Premendo nuovamente a lungo sarà possibile selezionare se attivare o disattivare la sveglia.
//   Premendo nuovamente a lungo sarà possibile ritornare alla modalità orologio.
// * Premendo per oltre 8 secondi il pulsante si eseguirà il reset delle impostaizoni del WIFI.
// 
// Se la sveglia è attiva i puntini separatori dell'orario, lampeggiano, se invece non c'è nessuna sveglia i puntini separatori rimarranno fissi.
// 
// Tramite pagina WEB sarà possibile fare ulteriori regolazioni
// * Commutazione automatica ora/data con cadenza programmabile
// * Regolazione luminosità su tre fasce orarie.
// * Impostazione fuso orario e ora legale
// * impostazione allarme
// * impostazione animazione modalità orologio
// * reset / impostazione WIFI
// 
// 
// 
// 
// Software implementation for the Aliexpress hardware device: https://is.gd/vfdclock
// The connections of the various devices were reverse engineered and a more complete and configurable firmware was rewritten 
// for a clock/alarm connected to the WiFi network for date/time retrieval via NTP and use of the RTC as a backup.
// A web page for advanced configuration has also been implemented.
// 
// Operation:
// * When starting up for the first time, use a cell phone to connect to the VFDClock network and set up the WiFi connection. If the settings screen does not appear once connected,
//   go to 192.16.4.1 in your browser. 
// * Pressing the button allows you to view the date, alarm, alarm status, and IP address in sequence.
// * Pressing the button for 2 seconds will switch to alarm adjustment mode.
//   In this mode, pressing the button briefly will increase the selected item. Holding it down for a long time will switch
//   to the next item. Pressing and holding again will allow you to select whether to activate or deactivate the alarm.
//   Pressing and holding again will return you to clock mode.
// * Pressing the button for more than 8 seconds will reset the WIFI settings.
// 
// If the alarm is active, the dots separating the time will flash; if there is no alarm, the dots will remain steady.
//
// Further adjustments can be made via the web page
// * Automatic time/date changeover at programmable intervals
// * Brightness adjustment in three time bands.
// * Time zone and daylight saving time settings
// * Alarm settings
// * Clock mode animation settings
// * Reset/WIFI settings
//
//
//
// ============================================================================
// LIBRERIE / LIBRARIES
// ============================================================================
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Wire.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <EEPROM.h>

// ============================================================================
// DEFINIZIONI PIN / PIN DEFINITIONS
// ============================================================================
#define VFD_MOSI 14         // Pin MOSI per VFD / VFD MOSI pin
#define VFD_CLK 12          // Pin Clock per VFD / VFD Clock pin
#define VFD_CS 13           // Pin Chip Select per VFD / VFD Chip Select pin
#define VFD_RST 16          // Pin Reset per VFD / VFD Reset pin
#define BUTTON_PIN 0        // Pulsante multifunzione / Multifunction button
#define BUZZER_PIN 0        // Buzzer per sveglia / Alarm buzzer

#define SDA_PIN 5           // Pin I2C SDA per RTC / I2C SDA pin for RTC
#define SCL_PIN 4           // Pin I2C SCL per RTC / I2C SCL pin for RTC
#define RTC_ADDRESS 0x32    // Indirizzo I2C RX8025T / RX8025T I2C address
#define WIFI_TIMEOUT 60000  // Timeout connessione WiFi (ms) / WiFi connection timeout (ms)

// ============================================================================
// CONFIGURAZIONE TIMEZONE ITALIA / ITALY TIMEZONE CONFIGURATION
// ============================================================================
TimeChangeRule CEST = { "CEST", Last, Sun, Mar, 2, 120 };  // Ora legale / Daylight saving time
TimeChangeRule CET = { "CET", Last, Sun, Oct, 3, 60 };     // Ora solare / Standard time
Timezone italyTZ(CEST, CET);

// ============================================================================
// OGGETTI GLOBALI / GLOBAL OBJECTS
// ============================================================================
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);
ESP8266WebServer server(80);
DNSServer dnsServer;

// ============================================================================
// VARIABILI GLOBALI / GLOBAL VARIABLES
// ============================================================================
static char VFD_Dimming = 0x8f;          // Livello luminosità VFD / VFD brightness level
unsigned long lastNTPSync = 0;            // Ultimo sync NTP / Last NTP sync
unsigned long lastDisplayUpdate = 0;      // Ultimo aggiornamento display / Last display update
unsigned long lastDateDisplay = 0;        // Ultima visualizzazione data / Last date display
bool wifiConnected = false;               // Stato connessione WiFi / WiFi connection status
bool apMode = false;                      // Modalità Access Point / Access Point mode
unsigned long apModeStart = 0;            // Inizio modalità AP / AP mode start time
bool rtcFound = false;                    // RTC rilevato / RTC detected
bool webServerStarted = false;            // Web server avviato / Web server started
bool showingDate = false;                 // Mostrando data / Showing date
unsigned long dateShowStart = 0;          // Inizio visualizzazione data / Date display start

// ============================================================================
// GESTIONE SVEGLIA / ALARM MANAGEMENT
// ============================================================================
enum AlarmMode {
  ALARM_IDLE,         // Modalità normale / Normal mode
  ALARM_SET_HOUR,     // Imposta ora / Set hour
  ALARM_SET_MINUTE,   // Imposta minuto / Set minute
  ALARM_SET_ONOFF     // Attiva/Disattiva / Enable/Disable
};

AlarmMode alarmMode = ALARM_IDLE;
unsigned long lastBlinkTime = 0;
bool blinkState = false;
bool alarmIsRinging = false;              // Sveglia in corso / Alarm ringing
bool blinkSeparators = false;             // Lampeggio separatori ":" / Colon separators blinking
unsigned long alarmRingingStart = 0;
int alarmBeepPhase = 0;                   // Fase beep crescente / Crescendo beep phase
unsigned long lastBeepTime = 0;

// ============================================================================
// STRUTTURA CONFIGURAZIONE / CONFIGURATION STRUCTURE
// ============================================================================
struct ClockConfig {
  int tzOffsetMinutes;              // Offset fuso orario / Timezone offset
  uint8_t dstEnabled;               // Ora legale auto / Auto DST
  uint8_t b1Hour;                   // Fascia 1: ora / Band 1: hour
  uint8_t b1Minute;                 // Fascia 1: minuto / Band 1: minute
  uint8_t b1Level;                  // Fascia 1: luminosità / Band 1: brightness
  uint8_t b2Hour;                   // Fascia 2: ora / Band 2: hour
  uint8_t b2Minute;                 // Fascia 2: minuto / Band 2: minute
  uint8_t b2Level;                  // Fascia 2: luminosità / Band 2: brightness
  uint8_t b3Hour;                   // Fascia 3: ora / Band 3: hour
  uint8_t b3Minute;                 // Fascia 3: minuto / Band 3: minute
  uint8_t b3Level;                  // Fascia 3: luminosità / Band 3: brightness
  uint8_t showDateEnabled;          // Mostra data periodicamente / Show date periodically
  uint16_t dateIntervalSeconds;     // Intervallo visualizzazione data / Date display interval
  uint8_t scrollAnimationEnabled;   // Animazione scroll / Scroll animation
  uint8_t alarmHour;                // Ora sveglia / Alarm hour
  uint8_t alarmMinute;              // Minuto sveglia / Alarm minute
  uint8_t alarmEnabled;             // Sveglia attiva / Alarm enabled
  uint8_t wifiResetFlag;            // Flag reset WiFi / WiFi reset flag
  uint8_t reserved[1];              // Riservato / Reserved
};

ClockConfig cfg;

const int EEPROM_SIZE = 512;
const int EEPROM_CFG_ADDR = 100;

// ============================================================================
// STATI BOOT / BOOT STATES
// ============================================================================
enum BootState {
  BOOT_SCROLLING_INTRO,   // Mostra intro / Show intro
  BOOT_WIFI_CONNECTING,   // Connessione WiFi / WiFi connecting
  BOOT_WIFI_OK,           // WiFi connesso / WiFi connected
  BOOT_SHOW_IP,           // Mostra IP / Show IP
  BOOT_NTP_SYNC,          // Sincronizzazione NTP / NTP sync
  BOOT_DONE               // Boot completato / Boot done
};

BootState bootState = BOOT_SCROLLING_INTRO;
unsigned long bootStateTime = 0;
unsigned long wifiConnectStart = 0;
int wifiDots = 0;

// ============================================================================
// COMUNICAZIONE SPI VFD / VFD SPI COMMUNICATION
// ============================================================================
// Trasferimento SPI bit-banging / SPI bit-banging transfer
void SPI_transfer_bitbang(uint8_t data) {
  for (int i = 0; i < 8; i++) {
    digitalWrite(VFD_MOSI, bitRead(data, i));
    digitalWrite(VFD_CLK, HIGH);
    delayMicroseconds(1);
    digitalWrite(VFD_CLK, LOW);
    delayMicroseconds(1);
  }
}

// ============================================================================
// PATTERN CARATTERI VFD / VFD CHARACTER PATTERNS
// ============================================================================
// Pattern 5x7 per cifre 0-9 e caratteri speciali / 5x7 patterns for digits 0-9 and special chars
const uint8_t digitPatterns[14][5] = {
  { 0x3E, 0x51, 0x49, 0x45, 0x3E },  // 0
  { 0x00, 0x42, 0x7F, 0x40, 0x00 },  // 1
  { 0x42, 0x61, 0x51, 0x49, 0x46 },  // 2
  { 0x21, 0x41, 0x45, 0x4B, 0x31 },  // 3
  { 0x18, 0x14, 0x12, 0x7F, 0x10 },  // 4
  { 0x27, 0x45, 0x45, 0x45, 0x39 },  // 5
  { 0x3C, 0x4A, 0x49, 0x49, 0x30 },  // 6
  { 0x01, 0x71, 0x09, 0x05, 0x03 },  // 7
  { 0x36, 0x49, 0x49, 0x49, 0x36 },  // 8
  { 0x06, 0x49, 0x49, 0x29, 0x1E },  // 9
  { 0x00, 0x36, 0x36, 0x00, 0x00 },  // : (due punti)
  { 0x14, 0x08, 0x3E, 0x08, 0x14 },  // * (stella)
  { 0x40, 0x30, 0x0C, 0x03, 0x00 },  // / (slash)
  { 0x00, 0x00, 0x00, 0x00, 0x00 }   // (spazio)
};

// Converte carattere in indice pattern / Convert character to pattern index
uint8_t getPatternIndex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c == ':') return 10;
  if (c == '*') return 11;
  if (c == '/' || c == '\\') return 12;
  return 13;
}

// ============================================================================
// FUNZIONI VFD / VFD FUNCTIONS
// ============================================================================

// Scrive carattere custom in memoria VFD / Write custom character to VFD memory
void VFD_WriteCustomChar(uint8_t addr, const uint8_t pattern[5]) {
  if (addr > 7) return;
  digitalWrite(VFD_CS, LOW);
  SPI_transfer_bitbang(0x40 + addr * 5);
  for (int i = 0; i < 5; i++) {
    SPI_transfer_bitbang(pattern[i]);
  }
  digitalWrite(VFD_CS, HIGH);
  delayMicroseconds(50);
}

// Mostra carattere custom su posizione / Show custom character at position
void VFD_ShowCustomChar(uint8_t pos, uint8_t customAddr) {
  if (pos > 7 || customAddr > 7) return;
  digitalWrite(VFD_CS, LOW);
  SPI_transfer_bitbang(0x20 + pos);
  SPI_transfer_bitbang(customAddr);
  digitalWrite(VFD_CS, HIGH);
}

// ============================================================================
// ANIMAZIONE SCROLL / SCROLL ANIMATION
// ============================================================================

// Stato animazione / Animation state
struct AnimationState {
  char currentDisplay[8];
  char targetDisplay[8];
  int8_t frameOffset[8];
  bool animating[8];
};

AnimationState animState = {
  { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
  { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
  { 0, 0, 0, 0, 0, 0, 0, 0 },
  { false, false, false, false, false, false, false, false }
};

// Crea pattern intermedio per scroll / Create intermediate pattern for scroll
void createScrollPattern(const uint8_t oldPattern[5], const uint8_t newPattern[5], uint8_t offset, uint8_t output[5]) {
  for (int col = 0; col < 5; col++) {
    uint8_t oldByte = oldPattern[col];
    uint8_t newByte = newPattern[col];
    uint8_t result;
    if (offset == 0) {
      result = oldByte;
    } else if (offset >= 7) {
      result = newByte;
    } else {
      uint8_t oldShifted = (oldByte << offset) & 0x7F;
      uint8_t newShifted = (newByte >> (7 - offset)) & 0x7F;
      result = oldShifted | newShifted;
    }
    output[col] = result;
  }
}

// Avvia animazione scroll / Start scroll animation
void startScrollAnimation(const char newDisplay[8]) {
  for (int i = 0; i < 8; i++) {
    if (newDisplay[i] != animState.currentDisplay[i]) {
      animState.targetDisplay[i] = newDisplay[i];
      animState.frameOffset[i] = 0;
      animState.animating[i] = true;
    } else {
      animState.animating[i] = false;
      animState.targetDisplay[i] = animState.currentDisplay[i];
    }
  }
}

// Aggiorna frame animazione / Update animation frame
bool updateScrollAnimation() {
  bool stillAnimating = false;
  for (int pos = 0; pos < 8; pos++) {
    // Gestione separatori ":" / Handle ":" separators
    if (pos == 2 || pos == 5) {
      char sepChar = ':';
      // Lampeggio se sveglia attiva e non in corso / Blink if alarm enabled and not ringing
      if (cfg.alarmEnabled && !alarmIsRinging) {
        sepChar = blinkSeparators ? ' ' : ':';
      }
      digitalWrite(VFD_CS, LOW);
      SPI_transfer_bitbang(0x20 + pos);
      SPI_transfer_bitbang(sepChar);
      digitalWrite(VFD_CS, HIGH);
      animState.currentDisplay[pos] = sepChar;
      continue;
    }

    // Mapping posizioni custom char / Custom char position mapping
    uint8_t customChar;
    if (pos == 0) customChar = 0;
    else if (pos == 1) customChar = 2;
    else if (pos == 3) customChar = 1;
    else if (pos == 4) customChar = 5;
    else if (pos == 6) customChar = 6;
    else if (pos == 7) customChar = 4;

    // Mapping posizioni fisiche / Physical position mapping
    uint8_t physicalPos = pos;
    if (pos == 3) physicalPos = 4;
    else if (pos == 4) physicalPos = 3;

    // Animazione carattere / Character animation
    if (animState.animating[pos]) {
      stillAnimating = true;
      uint8_t oldIdx = getPatternIndex(animState.currentDisplay[pos]);
      uint8_t newIdx = getPatternIndex(animState.targetDisplay[pos]);
      uint8_t scrollPattern[5];
      createScrollPattern(digitPatterns[oldIdx], digitPatterns[newIdx], animState.frameOffset[pos], scrollPattern);
      VFD_WriteCustomChar(customChar, scrollPattern);
      VFD_ShowCustomChar(physicalPos, customChar);
      animState.frameOffset[pos]++;
      if (animState.frameOffset[pos] >= 7) {
        animState.animating[pos] = false;
        animState.currentDisplay[pos] = animState.targetDisplay[pos];
        VFD_WriteCustomChar(customChar, digitPatterns[newIdx]);
        VFD_ShowCustomChar(physicalPos, customChar);
      }
    } else {
      uint8_t idx = getPatternIndex(animState.currentDisplay[pos]);
      VFD_WriteCustomChar(customChar, digitPatterns[idx]);
      VFD_ShowCustomChar(physicalPos, customChar);
    }
  }
  // Attiva display / Enable display
  digitalWrite(VFD_CS, LOW);
  SPI_transfer_bitbang(0xe8);
  digitalWrite(VFD_CS, HIGH);
  return stillAnimating;
}

// Mostra stringa con scroll / Display string with scroll
void VFD_DISP_Scroll(const char data[8]) {
  startScrollAnimation(data);
  while (updateScrollAnimation()) {
    delay(40);
  }
}

// ============================================================================
// CONVERSIONI BCD / BCD CONVERSIONS
// ============================================================================
uint8_t bcd2dec(uint8_t val) {
  return ((val / 16 * 10) + (val % 16));
}

uint8_t dec2bcd(uint8_t val) {
  return ((val / 10 * 16) + (val % 10));
}

// ============================================================================
// GESTIONE RTC / RTC MANAGEMENT
// ============================================================================

// Legge registro RTC / Read RTC register
uint8_t rtcRead(uint8_t reg) {
  Wire.beginTransmission(RTC_ADDRESS);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom((uint8_t)RTC_ADDRESS, (uint8_t)1);
  if (Wire.available()) {
    return Wire.read();
  }
  return 0xFF;
}

// Scrive registro RTC / Write RTC register
void rtcWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(RTC_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

// Legge data/ora completa da RTC / Read full date/time from RTC
void rtcReadTime(int &hour, int &minute, int &second, int &day, int &month, int &year) {
  uint8_t sec = rtcRead(0x00);
  uint8_t min = rtcRead(0x01);
  uint8_t hr = rtcRead(0x02);
  uint8_t wday = rtcRead(0x03);
  uint8_t d = rtcRead(0x04);
  uint8_t mon = rtcRead(0x05);
  uint8_t yr = rtcRead(0x06);
  second = bcd2dec(sec);
  minute = bcd2dec(min);
  hour = bcd2dec(hr);
  day = bcd2dec(d);
  month = bcd2dec(mon);
  year = bcd2dec(yr) + 2000;
}

// Imposta data/ora su RTC / Set date/time on RTC
void rtcSetTime(int year, int month, int day, int hour, int minute, int second) {
  rtcWrite(0x00, dec2bcd(second));
  rtcWrite(0x01, dec2bcd(minute));
  rtcWrite(0x02, dec2bcd(hour));
  rtcWrite(0x03, 0);
  rtcWrite(0x04, dec2bcd(day));
  rtcWrite(0x05, dec2bcd(month));
  rtcWrite(0x06, dec2bcd(year - 2000));
  delay(100);
}

// ============================================================================
// FUNZIONI DISPLAY VFD / VFD DISPLAY FUNCTIONS
// ============================================================================

// Mostra stringa senza animazione / Display string without animation
void VFD_DISP(char data[8]) {
  for (uint8_t i = 0; i < 8; i++) {
    digitalWrite(VFD_CS, LOW);
    SPI_transfer_bitbang(0x20 + i);
    SPI_transfer_bitbang(data[i]);
    digitalWrite(VFD_CS, HIGH);
  }
  digitalWrite(VFD_CS, LOW);
  SPI_transfer_bitbang(0xe8);
  digitalWrite(VFD_CS, HIGH);
}

// Imposta luminosità VFD / Set VFD brightness
void VFD_SetDimming(char dimming) {
  digitalWrite(VFD_CS, LOW);
  SPI_transfer_bitbang(0xe4);
  SPI_transfer_bitbang(dimming);
  digitalWrite(VFD_CS, HIGH);
}

// ============================================================================
// INIZIALIZZAZIONE I2C E RTC / I2C AND RTC INITIALIZATION
// ============================================================================

// Scansione bus I2C / I2C bus scan
void scanI2C() {
  Serial.println("\n=== I2C Scan ===");
  Serial.printf("SDA: GPIO%d, SCL: GPIO%d\n", SDA_PIN, SCL_PIN);
  byte count = 0;
  for (byte i = 1; i < 127; i++) {
    Wire.beginTransmission(i);
    if (Wire.endTransmission() == 0) {
      Serial.printf("Found I2C: 0x%02X\n", i);
      count++;
      if (i == RTC_ADDRESS) {
        Serial.println("  ^ RTC detected!");
        rtcFound = true;
      }
    }
  }
  Serial.printf("Total: %d device(s)\n\n", count);
}

// Inizializza RTC / Initialize RTC
void initializeRTC() {
  scanI2C();
  if (!rtcFound) {
    Serial.println("ERROR: RTC not found!");
    return;
  }
  int h, m, s, d, mon, y;
  rtcReadTime(h, m, s, d, mon, y);
  Serial.printf("RTC: %04d-%02d-%02d %02d:%02d:%02d\n", y, mon, d, h, m, s);
  // Verifica validità data / Validate date
  if (y < 2025 || y > 2100 || h > 23 || m > 59 || s > 59) {
    Serial.println("Invalid RTC, initializing...");
    rtcSetTime(2025, 1, 1, 0, 0, 0);
  }
}

// ============================================================================
// GESTIONE CONFIGURAZIONE EEPROM / EEPROM CONFIGURATION MANAGEMENT
// ============================================================================

// Carica configurazione default / Load default configuration
void loadDefaultConfig() {
  cfg.tzOffsetMinutes = 60;
  cfg.dstEnabled = 1;
  cfg.b1Hour = 7;
  cfg.b1Minute = 0;
  cfg.b1Level = 0xC0;
  cfg.b2Hour = 22;
  cfg.b2Minute = 0;
  cfg.b2Level = 0x30;
  cfg.b3Hour = 1;
  cfg.b3Minute = 0;
  cfg.b3Level = 0x01;
  cfg.showDateEnabled = 0;
  cfg.dateIntervalSeconds = 60;
  cfg.scrollAnimationEnabled = 1;
  cfg.alarmHour = 7;
  cfg.alarmMinute = 0;
  cfg.alarmEnabled = 0;
  cfg.wifiResetFlag = 0;
}

// Carica configurazione da EEPROM / Load configuration from EEPROM
void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(EEPROM_CFG_ADDR, cfg);
  // Valida configurazione / Validate configuration
  if (cfg.tzOffsetMinutes < -720 || cfg.tzOffsetMinutes > 840 || 
      cfg.b1Hour > 23 || cfg.b2Hour > 23 || cfg.b3Hour > 23 || 
      cfg.alarmHour > 23 || cfg.alarmMinute > 59 || cfg.alarmEnabled > 1 || 
      cfg.dateIntervalSeconds < 1 || cfg.dateIntervalSeconds > 999) {
    Serial.println("Invalid EEPROM, loading defaults");
    loadDefaultConfig();
    EEPROM.put(EEPROM_CFG_ADDR, cfg);
    EEPROM.commit();
  } else {
    Serial.printf("Alarm: %02d:%02d %s\n", cfg.alarmHour, cfg.alarmMinute, cfg.alarmEnabled ? "ON" : "OFF");
  }
  EEPROM.end();
}

// Salva configurazione in EEPROM / Save configuration to EEPROM
void saveConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(EEPROM_CFG_ADDR, cfg);
  EEPROM.commit();
  EEPROM.end();
  Serial.println("Config saved");
}

// Cancella credenziali WiFi / Clear WiFi credentials
void clearWiFiCredentials() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 96; i++) EEPROM.write(i, 0xFF);
  EEPROM.commit();
  EEPROM.end();
}

// ============================================================================
// GESTIONE SVEGLIA / ALARM MANAGEMENT
// ============================================================================

// Controlla trigger sveglia / Check alarm trigger
void checkAlarmTrigger() {
  if (!cfg.alarmEnabled || alarmIsRinging) return;
  int h, m, s, d, mon, y;
  rtcReadTime(h, m, s, d, mon, y);
  if (h == cfg.alarmHour && m == cfg.alarmMinute && s == 0) {
    alarmIsRinging = true;
    alarmRingingStart = millis();
    alarmBeepPhase = 0;
    Serial.println("ALARM TRIGGERED!");
  }
}

// Suona beep sveglia / Play alarm beep
void playAlarmBeep(int beepsPerSecond) {
  pinMode(BUZZER_PIN, OUTPUT);
  unsigned long startBeep = millis();
  while (millis() - startBeep < 100) {
    digitalWrite(BUZZER_PIN, HIGH);
    delayMicroseconds(250);
    digitalWrite(BUZZER_PIN, LOW);
    delayMicroseconds(250);
  }
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  delay(50);
}

// Aggiorna suono sveglia (crescendo) / Update alarm sound (crescendo)
void updateAlarmSound() {
  if (!alarmIsRinging) return;
  unsigned long elapsed = millis() - alarmRingingStart;
  int currentPhase = elapsed / 10000;  // Fase ogni 10s / Phase every 10s

  if (currentPhase >= 5) {  // Auto-stop dopo 50s / Auto-stop after 50s
    stopAlarm();
    return;
  }

  if (currentPhase != alarmBeepPhase) {
    alarmBeepPhase = currentPhase;
    lastBeepTime = 0;
  }

  int beepsPerSecond = alarmBeepPhase + 1;  // Crescendo 1-5 beep/s / Crescendo 1-5 beep/s
  unsigned long beepInterval = 1000 / beepsPerSecond;

  if (millis() - lastBeepTime >= beepInterval) {
    lastBeepTime = millis();
    playAlarmBeep(beepsPerSecond);
  }
}

// Ferma sveglia / Stop alarm
void stopAlarm() {
  alarmIsRinging = false;
  alarmBeepPhase = 0;
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.println("Alarm stopped");
}

// ============================================================================
// SINCRONIZZAZIONE NTP / NTP SYNCHRONIZATION
// ============================================================================

// Sincronizza RTC con server NTP / Synchronize RTC with NTP server
bool syncNTPtoRTC() {
  if (!rtcFound || WiFi.status() != WL_CONNECTED) return false;
  Serial.println("\n=== NTP ===");
  timeClient.forceUpdate();
  delay(500);
  time_t utc = timeClient.getEpochTime();
  if (utc < 1000000000) {
    Serial.println("Invalid NTP");
    return false;
  }
  // Applica offset timezone / Apply timezone offset
  time_t localTime = utc + (cfg.tzOffsetMinutes * 60);
  // Applica DST se abilitato / Apply DST if enabled
  if (cfg.dstEnabled) {
    struct tm *ti = gmtime(&utc);
    int month = ti->tm_mon + 1;
    if (month >= 4 && month <= 9) {  // Aprile-Settembre / April-September
      localTime += 3600;
      Serial.println("DST +1h");
    }
  }
  struct tm *timeinfo = localtime(&localTime);
  rtcSetTime(timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  Serial.println("NTP OK\n");
  return true;
}

// ============================================================================
// FUNZIONI UTILITÀ / UTILITY FUNCTIONS
// ============================================================================

// Formatta ora HH:MM / Format time HH:MM
String formatTime(uint8_t h, uint8_t m) {
  String result = "";
  if (h < 10) result += "0";
  result += String(h) + ":";
  if (m < 10) result += "0";
  result += String(m);
  return result;
}

// Scroll testo completo / Scroll complete text
void scrollTextComplete(const String &txt, unsigned long durationMs) {
  unsigned long start = millis();
  String paddedText = "        " + txt + "        ";
  int paddedLen = paddedText.length();
  int pos = 0;
  int maxPos = paddedLen - 8;
  while (millis() - start < durationMs && pos <= maxPos) {
    char buf[8];
    for (int i = 0; i < 8; i++) {
      int idx = pos + i;
      buf[i] = (idx < paddedLen) ? paddedText[idx] : ' ';
    }
    VFD_DISP(buf);
    pos++;
    delay(150);
  }
}

// ============================================================================
// GESTIONE PULSANTE SVEGLIA / ALARM BUTTON HANDLING
// ============================================================================

// Gestisce pulsante per impostazione sveglia / Handle button for alarm setup
void handleAlarmButton() {
  if (alarmIsRinging) return;

  unsigned long now = millis();
  int btnState = digitalRead(BUTTON_PIN);

  static bool btnPressed = false;
  static unsigned long btnPressTime = 0;
  static bool longPressHandled = false;

  // Rileva pressione / Detect press
  if (btnState == LOW && !btnPressed) {
    btnPressed = true;
    btnPressTime = now;
    longPressHandled = false;
  }

  // Gestione pressione lunga (>2s) / Handle long press (>2s)
  if (btnState == LOW && btnPressed && !longPressHandled) {
    if ((now - btnPressTime) > 2000) {
      longPressHandled = true;
      // Cambio modalità / Change mode
      if (alarmMode == ALARM_IDLE) {
        alarmMode = ALARM_SET_HOUR;
      } else if (alarmMode == ALARM_SET_HOUR) {
        alarmMode = ALARM_SET_MINUTE;
      } else if (alarmMode == ALARM_SET_MINUTE) {
        alarmMode = ALARM_SET_ONOFF;
      } else if (alarmMode == ALARM_SET_ONOFF) {
        alarmMode = ALARM_IDLE;
        saveConfig();
        lastDisplayUpdate = 0;
      }
      lastBlinkTime = now;
      blinkState = true;
    }
  }

  // Gestione rilascio / Handle release
  if (btnState == HIGH && btnPressed) {
    unsigned long dur = now - btnPressTime;
    btnPressed = false;
    // Pressione breve = incrementa valore / Short press = increment value
    if (dur < 2000 && !longPressHandled) {
      if (alarmMode == ALARM_SET_HOUR) {
        cfg.alarmHour = (cfg.alarmHour + 1) % 24;
      } else if (alarmMode == ALARM_SET_MINUTE) {
        cfg.alarmMinute = (cfg.alarmMinute + 1) % 60;
      } else if (alarmMode == ALARM_SET_ONOFF) {
        cfg.alarmEnabled = !cfg.alarmEnabled;
      }
    }
  }

  // Visualizzazione modalità impostazione / Display setup mode
  switch (alarmMode) {
    case ALARM_IDLE:
      break;

    case ALARM_SET_HOUR:
    case ALARM_SET_MINUTE:
      {
        if (now - lastBlinkTime > 500) {
          lastBlinkTime = now;
          blinkState = !blinkState;
        }
        char ad[9];
        if (alarmMode == ALARM_SET_HOUR) {
          if (blinkState) {
            sprintf(ad, "AL=%02d:%02d", cfg.alarmHour, cfg.alarmMinute);
          } else {
            sprintf(ad, "AL=  :%02d", cfg.alarmMinute);
          }
        } else {
          if (blinkState) {
            sprintf(ad, "AL=%02d:%02d", cfg.alarmHour, cfg.alarmMinute);
          } else {
            sprintf(ad, "AL=%02d:  ", cfg.alarmHour);
          }
        }
        VFD_DISP(ad);
        break;
      }

    case ALARM_SET_ONOFF:
      {
        if (now - lastBlinkTime > 500) {
          lastBlinkTime = now;
          blinkState = !blinkState;
        }
        char ad[9];
        if (blinkState) {
          strcpy(ad, cfg.alarmEnabled ? "AL=ON   " : "AL=OFF  ");
        } else {
          strcpy(ad, "AL=     ");
        }
        VFD_DISP(ad);
        break;
      }
  }
}

// ============================================================================
// WEB INTERFACE - HTML / INTERFACCIA WEB - HTML
// ============================================================================

// Header HTML / HTML header
String htmlHeader() {
  String h = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">";
  h += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  h += "<title>VFD Clock</title><style>";
  h += "body{font-family:Arial,sans-serif;background:#f0f0f0;margin:0;padding:0;}";
  h += ".container{max-width:600px;margin:20px auto;background:#fff;padding:25px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}";
  h += "h1{margin:0 0 20px;color:#333;text-align:center;font-size:28px;}";
  h += "h2{margin:20px 0 15px;color:#555;font-size:20px;border-bottom:2px solid #4CAF50;padding-bottom:8px;}";
  h += "label{display:block;margin:12px 0 5px;font-weight:bold;color:#333;}";
  h += "input[type=text],input[type=password],input[type=number],input[type=time],select{width:100%;padding:10px;box-sizing:border-box;border-radius:5px;border:1px solid #ccc;font-size:14px;}";
  h += "input[type=checkbox]{width:20px;height:20px;margin-right:10px;cursor:pointer;}";
  h += ".checkbox-label{display:flex;align-items:center;margin:15px 0;}";
  h += "button{width:100%;padding:12px;margin-top:15px;border:none;border-radius:5px;background:#4CAF50;color:#fff;font-size:16px;cursor:pointer;font-weight:bold;}";
  h += "button:hover{background:#45a049;}";
  h += ".btn-secondary{background:#2196F3;}.btn-secondary:hover{background:#0b7dda;}";
  h += ".band{background:#f9f9f9;padding:15px;margin:10px 0;border-radius:8px;border-left:4px solid #4CAF50;}";
  h += ".band-row{display:flex;align-items:center;gap:15px;margin-top:8px;}";
  h += ".slider-container{flex:1;display:flex;align-items:center;gap:10px;}";
  h += "input[type=range]{flex:1;height:6px;border-radius:5px;background:#ddd;}";
  h += ".slider-value{min-width:50px;text-align:right;font-weight:bold;color:#4CAF50;}";
  h += ".alarm-box{background:#fff3cd;padding:15px;border-radius:8px;border-left:4px solid #ffc107;margin:10px 0;}";
  h += ".info{background:#e3f2fd;padding:12px;border-radius:5px;margin:15px 0;color:#1976d2;}";
  h += "button[type='button']:hover{background:#e8f5e9 !important;border-color:#4CAF50 !important;}";
  h += ".network-btn{transition:all 0.2s ease;}";
  h += "</style>";
  h += "<script>";
  h += "function updateSlider(id,val){document.getElementById(id).innerText=val+'%';}";
  h += "function toggleDateInterval(){var c=document.getElementById('showDate');";
  h += "document.getElementById('dateIntervalGroup').style.display=c.checked?'block':'none';}";
  h += "</script></head><body><div class='container'>";
  return h;
}

String htmlFooter() {
  return "</div></body></html>";
}

// ============================================================================
// WEB INTERFACE - HANDLERS / INTERFACCIA WEB - GESTORI
// ============================================================================

// Handler pagina principale / Main page handler
void handleRoot() {
  if (apMode) handleWiFiPage();
  else handleConfigPage();
}

// Pagina configurazione / Configuration page
void handleConfigPage() {
  String page = htmlHeader();
  page += "<h1>&#128336; VFD Clock</h1>";
  int h, m, s, d, mon, y;
  if (rtcFound) {
    rtcReadTime(h, m, s, d, mon, y);
    page += "<div class='info'>&#128197; " + String(d) + "/" + String(mon) + "/" + String(y);
    page += " &nbsp; &#128336; " + formatTime(h, m) + ":" + (s < 10 ? "0" : "") + String(s) + "</div>";
  }
  page += "<form action='/save' method='POST'>";
  page += "<h2>&#9200; Sveglia</h2><div class='alarm-box'>";
  page += "<label>Orario:</label><input type='time' name='alarmTime' value='" + formatTime(cfg.alarmHour, cfg.alarmMinute) + "'>";
  page += "<div class='checkbox-label'><input type='checkbox' name='alarmEnabled' value='1'";
  if (cfg.alarmEnabled) page += " checked";
  page += "><span>Attiva</span></div></div>";
  page += "<h2>&#9200; Timezone</h2><label>Fuso:</label><select name='tz'>";
  int tzs[] = { -720, -660, -600, -540, -480, -420, -360, -300, -240, -180, -120, -60, 0, 60, 120, 180, 240, 300, 330, 360, 420, 480, 540, 600, 660, 720, 780, 840 };
  for (int i = 0; i < 28; i++) {
    String sel = (cfg.tzOffsetMinutes == tzs[i]) ? " selected" : "";
    page += "<option value='" + String(tzs[i]) + "'" + sel + ">UTC";
    if (tzs[i] >= 0) page += "+";
    page += String(tzs[i] / 60) + "</option>";
  }
  page += "</select>";
  page += "<div class='checkbox-label'><input type='checkbox' name='dst' value='1'";
  if (cfg.dstEnabled) page += " checked";
  page += "><span>Ora Legale</span></div>";
  page += "<h2>&#128197; Data</h2><div class='checkbox-label'>";
  page += "<input type='checkbox' id='showDate' name='showDate' value='1' onchange='toggleDateInterval()'";
  if (cfg.showDateEnabled) page += " checked";
  page += "><span>Mostra periodicamente</span></div>";
  page += "<div id='dateIntervalGroup' style='display:" + String(cfg.showDateEnabled ? "block" : "none") + ";'>";
  page += "<label>Ogni:</label><input type='number' name='dateInterval' min='1' max='999' value='" + String(cfg.dateIntervalSeconds) + "'> secondi</div>";
  page += "<h2>&#127916; Animazione</h2><div class='checkbox-label'>";
  page += "<input type='checkbox' name='scrollAnim' value='1'";
  if (cfg.scrollAnimationEnabled) page += " checked";
  page += "><span>Scroll</span></div>";
  page += "<h2>&#128161; Luminosità</h2>";
  for (int i = 1; i <= 3; i++) {
    uint8_t bh = (i == 1) ? cfg.b1Hour : (i == 2) ? cfg.b2Hour : cfg.b3Hour;
    uint8_t bm = (i == 1) ? cfg.b1Minute : (i == 2) ? cfg.b2Minute : cfg.b3Minute;
    uint8_t bl = (i == 1) ? cfg.b1Level : (i == 2) ? cfg.b2Level : cfg.b3Level;
    int pct = map(bl, 0, 240, 0, 100);
    page += "<div class='band'><strong>FASCIA " + String(i) + "</strong><div class='band-row'>";
    page += "Dalle: <input type='time' name='b" + String(i) + "t' value='" + formatTime(bh, bm) + "' style='width:100px;'>";
    page += "<div class='slider-container'><input type='range' name='b" + String(i) + "l' min='0' max='100' value='" + String(pct) + "' oninput=\"updateSlider('v" + String(i) + "',this.value)\">";
    page += "<span class='slider-value' id='v" + String(i) + "'>" + String(pct) + "%</span></div></div></div>";
  }
  page += "<button type='submit'>&#128190; Salva</button></form>";
  page += "<form action='/wifi'><button type='submit' class='btn-secondary'>&#128241; WiFi</button></form>";
  page += htmlFooter();
  server.send(200, "text/html; charset=UTF-8", page);
}

// Pagina WiFi / WiFi page
void handleWiFiPage() {
  int n = WiFi.scanNetworks(false, true);
  
  // Filtra duplicati SSID (per reti mesh)
  String uniqueSSIDs[15];
  int uniqueCount = 0;
  
  for (int i = 0; i < n && i < 15; i++) {
    String currentSSID = WiFi.SSID(i);
    bool isDuplicate = false;
    
    // Verifica se SSID già presente
    for (int j = 0; j < uniqueCount; j++) {
      if (uniqueSSIDs[j] == currentSSID) {
        isDuplicate = true;
        break;
      }
    }
    
    // Aggiungi solo se non duplicato
    if (!isDuplicate && currentSSID.length() > 0) {
      uniqueSSIDs[uniqueCount] = currentSSID;
      uniqueCount++;
    }
  }
  
  String html = htmlHeader();
  html += "<h1>&#128336; VFD Clock</h1><h2>&#128241; WiFi</h2>";
  html += "<script>";
  html += "function fillSSID(ssid){document.getElementById('ssidInput').value=ssid;}";
  html += "</script>";
  
  if (uniqueCount > 0) {
    html += "<div style='margin-bottom:20px;'><strong>Reti disponibili (tap per selezionare):</strong></div>";
    
    for (int i = 0; i < uniqueCount; i++) {
      // Trova il miglior RSSI per questo SSID
      int bestRSSI = -999;
      for (int j = 0; j < n; j++) {
        if (WiFi.SSID(j) == uniqueSSIDs[i]) {
          if (WiFi.RSSI(j) > bestRSSI) {
            bestRSSI = WiFi.RSSI(j);
          }
        }
      }
      
      // Calcola qualità segnale in percentuale
      int quality;
      if (bestRSSI <= -100) quality = 0;
      else if (bestRSSI >= -50) quality = 100;
      else quality = 2 * (bestRSSI + 100);
      
      // Icona segnale
      String signalIcon = "&#128246;"; // Segnale basso
      if (quality > 70) signalIcon = "&#128246;"; // Segnale forte
      else if (quality > 40) signalIcon = "&#128245;"; // Segnale medio

      html += "<button type='button' onclick=\"fillSSID('" + uniqueSSIDs[i] + "')\" ";
      html += "style='width:100%;padding:12px;margin:5px 0;text-align:left;background:#f9f9f9;";
      html += "border:2px solid #ddd;border-radius:5px;cursor:pointer;font-size:14px;color:#333;'>";
      html += signalIcon + " <strong>" + uniqueSSIDs[i] + "</strong> (" + String(quality) + "%)";
      html += "</button>";
    }
  }
  
  html += "<form action='/savewifi' method='POST'><label>SSID:</label>";
  html += "<input type='text' id='ssidInput' name='ssid' required>";
  html += "<label>Password:</label>";
  html += "<input type='password' name='password'>";
  html += "<button type='submit'>Connetti</button></form>";
  
  if (!apMode) {
    html += "<form action='/'><button type='submit' class='btn-secondary'>Indietro</button></form>";
  }
  
  html += htmlFooter();
  server.send(200, "text/html; charset=UTF-8", html);
}

// Parser tempo HH:MM / Parse time HH:MM
int parseTimeToHM(const String &s, uint8_t &h, uint8_t &m) {
  int sep = s.indexOf(':');
  if (sep < 0) return 0;
  h = s.substring(0, sep).toInt();
  m = s.substring(sep + 1).toInt();
  if (h > 23 || m > 59) return 0;
  return 1;
}

// Salva configurazione da web / Save configuration from web
void handleSave() {
  if (server.hasArg("alarmTime")) {
    uint8_t h, m;
    if (parseTimeToHM(server.arg("alarmTime"), h, m)) {
      cfg.alarmHour = h;
      cfg.alarmMinute = m;
    }
  }
  cfg.alarmEnabled = server.hasArg("alarmEnabled") ? 1 : 0;
  if (server.hasArg("tz")) cfg.tzOffsetMinutes = server.arg("tz").toInt();
  cfg.dstEnabled = server.hasArg("dst") ? 1 : 0;
  cfg.showDateEnabled = server.hasArg("showDate") ? 1 : 0;
  if (server.hasArg("dateInterval")) {
    int iv = server.arg("dateInterval").toInt();
    if (iv >= 1 && iv <= 999) cfg.dateIntervalSeconds = iv;
  }
  cfg.scrollAnimationEnabled = server.hasArg("scrollAnim") ? 1 : 0;
  for (int i = 1; i <= 3; i++) {
    uint8_t h, m;
    if (parseTimeToHM(server.arg("b" + String(i) + "t"), h, m)) {
      if (i == 1) {
        cfg.b1Hour = h;
        cfg.b1Minute = m;
      } else if (i == 2) {
        cfg.b2Hour = h;
        cfg.b2Minute = m;
      } else {
        cfg.b3Hour = h;
        cfg.b3Minute = m;
      }
    }
    if (server.hasArg("b" + String(i) + "l")) {
      int pct = server.arg("b" + String(i) + "l").toInt();
      uint8_t lv = map(pct, 0, 100, 0, 240);
      if (i == 1) cfg.b1Level = lv;
      else if (i == 2) cfg.b2Level = lv;
      else cfg.b3Level = lv;
    }
  }
  saveConfig();
  String pg = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta http-equiv='refresh' content='2;url=/'>";
  pg += "<style>body{text-align:center;margin-top:50px;}</style></head><body><h1>&#10004; Salvato!</h1></body></html>";
  server.send(200, "text/html; charset=UTF-8", pg);
}

// Salva WiFi e riavvia / Save WiFi and restart
void handleSaveWiFi() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  
  Serial.println("Saving WiFi credentials:");
  Serial.println("SSID: " + ssid);
  
  EEPROM.begin(EEPROM_SIZE);
  // Cancella area WiFi
  for (int i = 0; i < 96; i++) EEPROM.write(i, 0);
  // Salva SSID
  for (int i = 0; i < ssid.length() && i < 32; i++) {
    EEPROM.write(i, ssid[i]);
  }
  // Salva password
  for (int i = 0; i < password.length() && i < 64; i++) {
    EEPROM.write(32 + i, password[i]);
  }
  EEPROM.commit();
  EEPROM.end();
  
  String pg = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">";
  pg += "<style>body{text-align:center;margin-top:50px;}</style>";
  pg += "</head><body><h1>Salvato! Riavvio...</h1>";
  pg += "<p>Attendi 10 secondi e riconnettiti alla tua rete WiFi.</p></body></html>";
  server.send(200, "text/html; charset=UTF-8", pg);
  
  delay(1000);
  Serial.println("Restarting...");
  ESP.restart();
}

// Reset WiFi / WiFi reset
void handleResetWiFi() {
  clearWiFiCredentials();
  server.send(200, "text/html", "<html><body><h1>Reset!</h1></body></html>");
  delay(1000);
  ESP.restart();
}

// ============================================================================
// AVVIO ACCESS POINT / START ACCESS POINT
// ============================================================================
void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP("VFDClock");
  dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
  server.on("/", handleRoot);
  server.on("/wifi", handleWiFiPage);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/savewifi", HTTP_POST, handleSaveWiFi);
  server.on("/resetwifi", HTTP_POST, handleResetWiFi);
  server.onNotFound(handleRoot);
  server.begin();
  apMode = true;
  webServerStarted = true;
  Serial.println("AP ready");
}

// ============================================================================
// AVVIO WEB SERVER / START WEB SERVER
// ============================================================================
void startWebServer() {
  if (!webServerStarted) {
    server.on("/", handleRoot);
    server.on("/wifi", handleWiFiPage);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/savewifi", HTTP_POST, handleSaveWiFi);
    server.on("/resetwifi", HTTP_POST, handleResetWiFi);
    server.begin();
    webServerStarted = true;
    Serial.println("Web server started");
  }
}

// ============================================================================
// GESTIONE LUMINOSITÀ AUTOMATICA / AUTOMATIC BRIGHTNESS MANAGEMENT
// ============================================================================

// Converte ora in minuti / Convert time to minutes
int timeToMinutes(int h, int m) {
  return h * 60 + m;
}

// Calcola luminosità per ora corrente / Calculate brightness for current time
uint8_t getDimmingForCurrentTime(int h, int m) {
  int currentMinutes = timeToMinutes(h, m);
  struct TimeBand {
    int minutes;
    uint8_t level;
  };
  TimeBand bands[3] = {
    { timeToMinutes(cfg.b1Hour, cfg.b1Minute), cfg.b1Level },
    { timeToMinutes(cfg.b2Hour, cfg.b2Minute), cfg.b2Level },
    { timeToMinutes(cfg.b3Hour, cfg.b3Minute), cfg.b3Level }
  };
  // Ordina fasce per orario / Sort bands by time
  for (int i = 0; i < 2; i++) {
    for (int j = i + 1; j < 3; j++) {
      if (bands[j].minutes < bands[i].minutes) {
        TimeBand temp = bands[i];
        bands[i] = bands[j];
        bands[j] = temp;
      }
    }
  }
  uint8_t level = bands[2].level;
  for (int i = 2; i >= 0; i--) {
    if (currentMinutes >= bands[i].minutes) {
      level = bands[i].level;
      break;
    }
  }
  if (currentMinutes < bands[0].minutes) level = bands[2].level;
  if (level > 0xF0) level = 0xF0;
  return level;
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== VFD CLOCK ===\n");

  // Inizializza pin / Initialize pins
  pinMode(VFD_MOSI, OUTPUT);
  pinMode(VFD_CLK, OUTPUT);
  pinMode(VFD_CS, OUTPUT);
  pinMode(VFD_RST, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Beep avvio / Startup beep
  for (int j = 0; j < 400; j++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delayMicroseconds(250);
    digitalWrite(BUZZER_PIN, LOW);
    delayMicroseconds(250);
  }
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Inizializza VFD / Initialize VFD
  digitalWrite(VFD_CLK, LOW);
  digitalWrite(VFD_CS, HIGH);
  digitalWrite(VFD_RST, LOW);
  delay(500);
  digitalWrite(VFD_RST, HIGH);
  delay(100);
  digitalWrite(VFD_CS, LOW);
  SPI_transfer_bitbang(0xe0);
  SPI_transfer_bitbang(0x07);
  digitalWrite(VFD_CS, HIGH);
  VFD_SetDimming(VFD_Dimming);

  // Inizializza I2C e RTC / Initialize I2C and RTC
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);
  initializeRTC();
  loadConfig();

  bootState = BOOT_SCROLLING_INTRO;
  bootStateTime = millis();
  Serial.println("\n=== BOOT START ===\n");
}

// ============================================================================
// LOOP PRINCIPALE / MAIN LOOP
// ============================================================================
void loop() {
  unsigned long now = millis();

  // Gestione web server / Web server handling
  if (webServerStarted) {
    server.handleClient();
    if (apMode) dnsServer.processNextRequest();
  }

  // Macchina a stati boot / Boot state machine
  switch (bootState) {

    // ========================================================================
    // INTRO SCROLLING / SCROLLING INTRO
    // ========================================================================
    case BOOT_SCROLLING_INTRO:
      {
        static bool shown = false;
        if (!shown) {
          scrollTextComplete("VFD CLOCK BY DAVIDE GATTI", 8000);
          shown = true;
          bootStateTime = now;
        }
        // Controlla credenziali WiFi salvate / Check saved WiFi credentials
        EEPROM.begin(EEPROM_SIZE);
        bool hasCred = (EEPROM.read(0) != 0 && EEPROM.read(0) != 0xFF);
        if (hasCred) {
          char ssid[32] = { 0 }, password[64] = { 0 };
          for (int i = 0; i < 32; i++) {
            ssid[i] = EEPROM.read(i);
            if (ssid[i] == 0) break;
          }
          for (int i = 0; i < 64; i++) {
            password[i] = EEPROM.read(32 + i);
            if (password[i] == 0) break;
          }
          EEPROM.end();
          Serial.print("Connecting: ");
          Serial.println(ssid);
          WiFi.mode(WIFI_STA);
          WiFi.begin(ssid, password);
          wifiConnectStart = now;
          wifiDots = 0;
          bootState = BOOT_WIFI_CONNECTING;
          bootStateTime = now;
        } else {
          EEPROM.end();
          startAccessPoint();
          bootState = BOOT_DONE;
        }
      }
      break;

    // ========================================================================
    // CONNESSIONE WIFI / WIFI CONNECTING
    // ========================================================================
    case BOOT_WIFI_CONNECTING:
      {
        if (now - bootStateTime >= 500) {
          bootStateTime = now;
          char c[8] = { 'W', 'I', 'F', 'I', ' ', ' ', ' ', ' ' };
          for (int i = 0; i < (wifiDots % 4); i++) c[5 + i] = '.';
          VFD_DISP(c);
          wifiDots++;
        }
        if (WiFi.status() == WL_CONNECTED) {
          wifiConnected = true;
          bootState = BOOT_WIFI_OK;
          bootStateTime = now;
          Serial.println("\nWiFi OK");
        } else if (now - wifiConnectStart > WIFI_TIMEOUT) {
          Serial.println("\nTimeout");
          WiFi.disconnect();
          startAccessPoint();
          bootState = BOOT_DONE;
        }
      }
      break;

    // ========================================================================
    // WIFI CONNESSO / WIFI CONNECTED
    // ========================================================================
    case BOOT_WIFI_OK:
      {
        static bool shown = false;
        if (!shown) {
          char ok[8] = { 'W', 'I', 'F', 'I', ' ', 'O', 'K', '!' };
          VFD_DISP(ok);
          shown = true;
        }
        if (now - bootStateTime >= 1000) {
          bootState = BOOT_SHOW_IP;
          bootStateTime = now;
        }
      }
      break;

    // ========================================================================
    // MOSTRA IP / SHOW IP
    // ========================================================================
    case BOOT_SHOW_IP:
      {
        static bool shown = false;
        if (!shown) {
          IPAddress ip = WiFi.localIP();
          scrollTextComplete("IP=" + ip.toString(), 5000);
          shown = true;
        }
        startWebServer();
        timeClient.begin();
        bootState = BOOT_NTP_SYNC;
        bootStateTime = now;
      }
      break;

    // ========================================================================
    // SINCRONIZZAZIONE NTP / NTP SYNCHRONIZATION
    // ========================================================================
    case BOOT_NTP_SYNC:
      {
        static int att = 0;
        static unsigned long lastAtt = 0;
        if (now - lastAtt > 2000 && att < 5) {
          lastAtt = now;
          att++;
          if (syncNTPtoRTC()) {
            lastNTPSync = millis();
            bootState = BOOT_DONE;
            att = 0;
          }
        } else if (att >= 5) {
          Serial.println("NTP failed");
          bootState = BOOT_DONE;
          att = 0;
        }
      }
      break;

    // ========================================================================
    // FUNZIONAMENTO NORMALE / NORMAL OPERATION
    // ========================================================================
    case BOOT_DONE:
      {
        // Stabilizzazione pulsante / Button stabilization
        static unsigned long bootDoneStart = 0;
        static bool bootDoneInit = false;
        if (!bootDoneInit) {
          bootDoneStart = millis();
          bootDoneInit = true;
        }
        bool stable = (millis() - bootDoneStart > 2000);

        // Modalità Access Point / Access Point mode
        if (apMode) {
          static unsigned long lastTog = 0;
          static bool showIP = false;
          if (millis() - lastTog > 2000) {
            lastTog = millis();
            showIP = !showIP;
            char d[8];
            if (showIP) strcpy(d, "192.4.1 ");
            else strcpy(d, "Portal  ");
            VFD_DISP(d);
          }
          break;
        }

        // Funzionamento stabile / Stable operation
        if (stable) {
          checkAlarmTrigger();

          // Lampeggio separatori se sveglia attiva / Separator blinking if alarm enabled
          static unsigned long lastBlinkUpdate = 0;
          if (cfg.alarmEnabled && (now - lastBlinkUpdate >= 500)) {
            lastBlinkUpdate = now;
            blinkSeparators = !blinkSeparators;
          }

          // ----------------------------------------------------------------
          // SVEGLIA IN CORSO / ALARM RINGING
          // ----------------------------------------------------------------
          if (alarmIsRinging) {
            updateAlarmSound();

            // Ferma sveglia con pulsante / Stop alarm with button
            static int lastBtnState = HIGH;
            int btnState = digitalRead(BUTTON_PIN);
            if (btnState == LOW && lastBtnState == HIGH) {
              stopAlarm();
              delay(300);
            }
            lastBtnState = btnState;

            // Aggiorna display durante sveglia / Update display during alarm
            unsigned long displayInterval = 500;
            if (now - lastDisplayUpdate >= displayInterval) {
              lastDisplayUpdate = now;
              int h, m, s, d, mon, y;
              rtcReadTime(h, m, s, d, mon, y);
              uint8_t lvl = getDimmingForCurrentTime(h, m);
              VFD_Dimming = lvl;
              VFD_SetDimming(VFD_Dimming);

              char t[9];
              char sep = cfg.alarmEnabled ? (blinkSeparators ? ' ' : ':') : ':';
              sprintf(t, "%02d%c%02d%c%02d", h, sep, m, sep, s);
              if (cfg.scrollAnimationEnabled) VFD_DISP_Scroll(t);
              else VFD_DISP(t);
            }

          // ----------------------------------------------------------------
          // FUNZIONAMENTO NORMALE / NORMAL OPERATION
          // ----------------------------------------------------------------
          } else {
            // Gestione pulsante per info / Button handling for info
            static int lastInfoBtnState = HIGH;
            static unsigned long infoBtnPressTime = 0;
            static bool infoBtnPressed = false;
            static int infoScreen = 0;  // 0=ora, 1=data, 2=alarm, 3=stato, 4=IP
            static unsigned long infoDisplayStart = 0;

            int infoBtnState = digitalRead(BUTTON_PIN);

            // Rileva pressione / Detect press
            if (infoBtnState == LOW && lastInfoBtnState == HIGH) {
              infoBtnPressed = true;
              infoBtnPressTime = now;
            }

            // Reset WiFi con pressione >8 secondi in schermata orologio
            if (infoBtnState == LOW && infoBtnPressed && infoScreen == 0 && alarmMode == ALARM_IDLE) {
              if (now - infoBtnPressTime > 8000) {
                char r[8] = { 'R', 'E', 'S', 'E', 'T', ' ', ' ', ' ' };
                VFD_DISP(r);
                clearWiFiCredentials();
                delay(2000);
                ESP.restart();
              }
            }

            // Rileva rilascio / Detect release
            if (infoBtnState == HIGH && lastInfoBtnState == LOW && infoBtnPressed) {
              unsigned long dur = now - infoBtnPressTime;

              // Pressione breve = cambia schermata / Short press = change screen
              if (dur < 1000 && alarmMode == ALARM_IDLE) {
                infoScreen = (infoScreen + 1) % 5;
                infoDisplayStart = now;

                if (infoScreen == 1) {
                  // Mostra data / Show date
                  int h, m, s, d, mon, y;
                  rtcReadTime(h, m, s, d, mon, y);
                  char dt[9];
                  sprintf(dt, "%02d/%02d/%02d", d, mon, y % 100);
                  VFD_DISP(dt);
                } else if (infoScreen == 2) {
                  // Mostra orario sveglia / Show alarm time
                  char ad[9];
                  sprintf(ad, "AL=%02d:%02d", cfg.alarmHour, cfg.alarmMinute);
                  VFD_DISP(ad);
                } else if (infoScreen == 3) {
                  // Mostra stato sveglia / Show alarm state
                  char ad[9];
                  strcpy(ad, cfg.alarmEnabled ? "AL=ON   " : "AL=OFF  ");
                  VFD_DISP(ad);
                } else if (infoScreen == 4) {
                  // Mostra IP / Show IP
                  if (WiFi.status() == WL_CONNECTED) {
                    IPAddress ip = WiFi.localIP();
                    scrollTextComplete("IP=" + ip.toString(), 5000);
                  } else {
                    char noWifi[8] = { 'N', 'O', ' ', 'W', 'i', 'F', 'i', ' ' };
                    VFD_DISP(noWifi);
                    delay(2000);
                  }
                  infoScreen = 0;
                  lastDisplayUpdate = 0;
                }
              }
              infoBtnPressed = false;
            }
            lastInfoBtnState = infoBtnState;

            // handleAlarmButton quando si visualizza l'allarme (infoScreen == 2)
            if (infoScreen == 2) {
              handleAlarmButton();
            }

            // Se in modalità impostazione sveglia, blocca tutto
            if (alarmMode != ALARM_IDLE) {
              break;
            }

            // Timeout schermate info / Info screens timeout
            if (infoScreen > 0 && infoScreen < 4 && (now - infoDisplayStart > 3000)) {
              infoScreen = 0;
              lastDisplayUpdate = 0;
            }

            // Display normale ora / Normal time display
            if (infoScreen == 0 && rtcFound) {
              unsigned long displayInterval = cfg.alarmEnabled ? 500 : 1000;

              // Gestione visualizzazione data periodica / Handle periodic date display
              if (showingDate) {
                if (now - dateShowStart >= 4000) {
                  showingDate = false;
                  lastDisplayUpdate = 0;
                }
              } else if (cfg.showDateEnabled && (now - lastDateDisplay >= (cfg.dateIntervalSeconds * 1000UL))) {
                int h, m, s, d, mon, y;
                rtcReadTime(h, m, s, d, mon, y);
                char dt[9];
                sprintf(dt, "%02d/%02d/%02d", d, mon, y % 100);
                VFD_DISP(dt);
                showingDate = true;
                dateShowStart = now;
                lastDateDisplay = now;

              // Aggiorna ora / Update time
              } else if (!showingDate && (now - lastDisplayUpdate >= displayInterval)) {
                lastDisplayUpdate = now;
                int h, m, s, d, mon, y;
                rtcReadTime(h, m, s, d, mon, y);
                uint8_t lvl = getDimmingForCurrentTime(h, m);
                VFD_Dimming = lvl;
                VFD_SetDimming(VFD_Dimming);

                char t[9];
                char sep = cfg.alarmEnabled ? (blinkSeparators ? ' ' : ':') : ':';
                sprintf(t, "%02d%c%02d%c%02d", h, sep, m, sep, s);

                if (cfg.scrollAnimationEnabled) VFD_DISP_Scroll(t);
                else VFD_DISP(t);

                // Sync NTP ogni ora / NTP sync every hour
                if (WiFi.status() == WL_CONNECTED && (millis() - lastNTPSync > 3600000)) {
                  syncNTPtoRTC();
                  lastNTPSync = millis();
                }
              }
            }
          }
        }
      }
      break;
  }

  delay(10);
}
