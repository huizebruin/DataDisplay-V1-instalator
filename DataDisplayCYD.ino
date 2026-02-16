//update funguje - prověřit, opravit vykreslovani a blikani progress baru


// oprava nacteni lokace po vybrani mista ze seznamu
//pridana volbe jednotky kmh/mph a jeji ukladani do pressetu do menu weather
// Invert pridano a ukladano 9.3.
// oprava zapominani timezone u manual po resetu 9.3.
// oprava zapominani zvoleného tématu po resetu 9.3.
// oprava zapominani nastaveni autodim po resetu 9.3.
// oprava zapominani nastaveni jednotky teploty po resetu 9.3.
// oprava zobrazeni o vyprseni timeoutu a bad answer z api
// lunar phase oprava
// NETHERLANDS ADDED

#include <WiFi.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include "time.h"
#include <XPT2046_Touchscreen.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_ota_ops.h>

// ================= GLOBÁLNÍ NASTAVENÍ (Musí být PRVNÍ) =================
TFT_eSPI tft = TFT_eSPI();
Preferences prefs;
bool isWhiteTheme = false;  // TEĎ JE TO TADY, TAKŽE TO VŠICHNI VIDÍ
// ================= NOVÉ PROMĚNNÉ PRO HODINY =================
bool isDigitalClock = false; // false = Analog, true = Digital
bool is12hFormat = false;    // false = 24h, true = 12h
bool invertColors = false;  // NOVÁ PROMĚNNÁ: Invertování barev pro CYD desky s invertovaným displejem

// ================= OTA UPDATE GLOBALS =================
const char* FIRMWARE_VERSION = "1.3";  // AKTUÁLNÍ VERZE
const char* VERSION_CHECK_URL = "https://raw.githubusercontent.com/lachimalaif/DataDisplay-V1-instalator/main/version.json";
const char* FIRMWARE_URL = "https://github.com/lachimalaif/DataDisplay-V1-instalator/releases/latest/download/DataDisplayCYD.ino.bin";

String availableVersion = "";  // Dostupná verze z GitHubu
String downloadURL = "";       // URL pro stažení firmware (z version.json)
bool updateAvailable = false;  // Je k dispozici aktualizace?
int otaInstallMode = 1;  // 0=Auto, 1=By user, 2=Manual
unsigned long lastVersionCheck = 0;
const unsigned long VERSION_CHECK_INTERVAL = 86400000;  // 24 hodin (pro testování změň na 30000 = 30s)

bool isUpdating = false;  // Probíhá aktualizace?
int updateProgress = 0;   // Progress 0-100%
String updateStatus = ""; // Status zpráva

// ================= TEMA NASTAVENI =================
int themeMode = 0; // 0 = BLACK, 1 = WHITE, 2 = BLUE, 3 = YELLOW
// POZN: U tématech BLACK a WHITE určuje isWhiteTheme: false=BLACK, true=WHITE
// U tématech BLUE a YELLOW je isWhiteTheme ignorován (pevné barvy)

float themeTransition = 0.0f; // Průběh přechodu (0.0 - 1.0)

// Barvy s přechody
uint16_t blueLight = 0x07FF;    // Světle modrá
uint16_t blueDark = 0x0010;     // Tmavě modrá
uint16_t yellowLight = 0xFFE0;  // Světle žlutá
uint16_t yellowDark = 0xCC00;   // Tmavě žlutá


// ================= WEATHER GLOBALS =================
String weatherCity = "Plzen"; 
float currentTemp = 0.0;
int currentHumidity = 0;
float currentWindSpeed = 0.0;
int currentWindDirection = 0;
int currentPressure = 0; 
int weatherCode = 0;
float lat = 0;
float lon = 0;
bool weatherUnitF = false; 
bool weatherUnitMph = false;  // false = km/h, true = mph
unsigned long lastWeatherUpdate = 0;
bool initialWeatherFetched = false;

struct ForecastData {
  int code;
  float tempMax;
  float tempMin;
};
ForecastData forecast[2]; 
// Proměnné pro dny předpovědi
String forecastDay1Name = "Mon";  // Zítra
String forecastDay2Name = "Tue";  // Pozítří

int moonPhaseVal = 0; 

// ================= SLUNCE A AUTO DIM (NOVÉ Z control.txt) =================
String sunriseTime = "--:--";
String sunsetTime = "--:--";
// ================= AUTODIM UI - NASTAVENÍ V MENU =================
int autoDimEditMode = 0;  // 0=none, 1=editing start, 2=editing end, 3=editing level
int autoDimTempStart = 22;
int autoDimTempEnd = 6;
int autoDimTempLevel = 20;
unsigned long lastBrightnessUpdate = 0;  // Aby se jas neměnil při každém loopu

bool autoDimEnabled = false;
int autoDimStart = 22; 
int autoDimEnd = 6;    
int autoDimLevel = 20; 
bool isDimmed = false; 

// Ikony Slunce
const unsigned char icon_sunrise[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x09, 0x90, 0x05, 0xa0, 0x03, 0xc0,
  0x01, 0x80, 0x7f, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
const unsigned char icon_sunset[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xfe, 0x01, 0x80, 0x03, 0xc0, 0x05, 0xa0,
  0x09, 0x90, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ================= MODERNÍ VEKTOROVÉ IKONY =================

void drawCloudVector(int x, int y, uint32_t color) {
  tft.fillCircle(x + 10, y + 15, 8, color);
  tft.fillCircle(x + 18, y + 10, 10, color);
  tft.fillCircle(x + 28, y + 15, 8, color);
  tft.fillRoundRect(x + 10, y + 15, 20, 8, 4, color);
}

void drawWeatherIconVector(int code, int x, int y) {
  // Barvy ikon se adaptují na téma
  uint16_t cloudCol = TFT_SILVER; 
  uint16_t shadowCol = isWhiteTheme ? 0x8410 : 0x4208; // Stín v modrém/žlutém tématu
  
  switch (code) {
    case 0: // Jasno
      // Sluníčko s odstínem
      tft.fillCircle(x + 16, y + 16, 10, TFT_YELLOW);
      tft.drawCircle(x + 16, y + 16, 11, shadowCol); // Stín
      for (int i = 0; i < 360; i += 45) {
        float rad = i * 0.01745;
        tft.drawLine(x+16+cos(rad)*11, y+16+sin(rad)*11, x+16+cos(rad)*16, y+16+sin(rad)*16, TFT_YELLOW);
      }
      break;
    
    case 1: case 2: case 3: // Polojasno
      tft.fillCircle(x + 22, y + 10, 8, TFT_YELLOW);
      tft.drawCircle(x + 22, y + 10, 9, shadowCol); // Stín
      drawCloudVector(x, y + 5, cloudCol);
      break;
    
    case 45: case 48: // Mlha
      for(int i=0; i<3; i++) {
        tft.fillRoundRect(x+4, y+12+(i*6), 24, 3, 2, TFT_SILVER);
        tft.drawRoundRect(x+4, y+12+(i*6), 24, 3, 2, shadowCol); // Stín
      }
      break;
    
    case 51: case 53: case 55: case 61: case 63: case 65: // Déšť
      drawCloudVector(x, y + 2, TFT_SILVER);
      for(int i=0; i<3; i++) {
        tft.fillRoundRect(x+10+(i*6), y+22, 2, 6, 1, TFT_BLUE);
        tft.drawRoundRect(x+10+(i*6), y+22, 2, 6, 1, shadowCol); // Stín kapky
      }
      break;
    
    case 71: case 73: case 75: case 77: // Sníh
      drawCloudVector(x, y + 2, cloudCol);
      tft.setTextColor(TFT_SKYBLUE);
      tft.drawString("*", x + 12, y + 22); 
      tft.drawString("*", x + 22, y + 22);
      break;
    
    case 80: case 81: case 82: // Přeháňky
      tft.fillCircle(x + 22, y + 10, 7, TFT_YELLOW);
      tft.drawCircle(x + 22, y + 10, 8, shadowCol); // Stín
      drawCloudVector(x, y + 2, TFT_SILVER);
      tft.fillRoundRect(x + 16, y + 22, 2, 6, 1, TFT_BLUE);
      break;
    
    case 95: case 96: case 99: // Bouřka
      drawCloudVector(x, y + 2, shadowCol); // Tmavý mrak
      tft.drawLine(x+18, y+20, x+14, y+28, TFT_YELLOW);
      tft.drawLine(x+14, y+28, x+20, y+28, TFT_YELLOW);
      tft.drawLine(x+20, y+28, x+16, y+36, TFT_YELLOW);
      break;
    
    default:
      drawCloudVector(x, y + 5, TFT_SILVER);
      break;
  }
}

// ============================================
// NOVÁ FUNKCE: Zmenšené ikony pro forecast
// ============================================

void drawWeatherIconVectorSmall(int code, int x, int y) {
  // Zmenšená verze pro forecast, ale s lepšími proporcemi
  // Některé prvky zůstávají proporčnější
  
  uint16_t cloudCol = TFT_SILVER; 
  uint16_t shadowCol = isWhiteTheme ? 0x8410 : 0x4208;
  
  switch (code) {
    case 0: // Jasno
      // Slunko - stejná velikost jako v normální verzi (malý radius)
      tft.fillCircle(x + 16, y + 16, 9, TFT_YELLOW);
      tft.drawCircle(x + 16, y + 16, 10, shadowCol);
      for (int i = 0; i < 360; i += 45) {
        float rad = i * 0.01745;
        tft.drawLine(x+16+cos(rad)*10, y+16+sin(rad)*10, x+16+cos(rad)*14, y+16+sin(rad)*14, TFT_YELLOW);
      }
      break;
    
    case 1: case 2: case 3: // Polojasno - SLUNKO + MRAK
      // Slunko - větší, viditelné
      tft.fillCircle(x + 20, y + 10, 7, TFT_YELLOW);
      tft.drawCircle(x + 20, y + 10, 8, shadowCol);
      // Mrak - menší proporce
      tft.fillCircle(x + 8, y + 14, 6, cloudCol);
      tft.fillCircle(x + 14, y + 11, 7, cloudCol);
      tft.fillCircle(x + 20, y + 14, 5, cloudCol);
      tft.fillRoundRect(x + 8, y + 14, 15, 5, 2, cloudCol);
      break;
    
    case 45: case 48: // Mlha
      for(int i=0; i<3; i++) {
        tft.fillRoundRect(x+4, y+12+(i*5), 20, 2, 1, TFT_SILVER);
        tft.drawRoundRect(x+4, y+12+(i*5), 20, 2, 1, shadowCol);
      }
      break;
    
    case 51: case 53: case 55: case 61: case 63: case 65: // Déšť
      // Mrak - 80% velikosti
      tft.fillCircle(x + 9, y + 13, 6, cloudCol);
      tft.fillCircle(x + 15, y + 10, 8, cloudCol);
      tft.fillCircle(x + 22, y + 13, 6, cloudCol);
      tft.fillRoundRect(x + 9, y + 13, 16, 6, 3, cloudCol);
      // Kapky deště - 80% velikosti
      for(int i=0; i<3; i++) {
        tft.fillRoundRect(x+10+(i*5), y+21, 2, 5, 1, TFT_BLUE);
        tft.drawRoundRect(x+10+(i*5), y+21, 2, 5, 1, shadowCol);
      }
      break;
    
    case 71: case 73: case 75: case 77: // Sníh
      // Mrak - 80% velikosti
      tft.fillCircle(x + 9, y + 13, 6, cloudCol);
      tft.fillCircle(x + 15, y + 10, 8, cloudCol);
      tft.fillCircle(x + 22, y + 13, 6, cloudCol);
      tft.fillRoundRect(x + 9, y + 13, 16, 6, 3, cloudCol);
      // Sníh - stejné hvězdičky
      tft.setTextColor(TFT_SKYBLUE);
      tft.drawString("*", x + 11, y + 21); 
      tft.drawString("*", x + 19, y + 21);
      break;
    
    case 80: case 81: case 82: // Přeháňky - SLUNKO + MRAK
      // Slunko - viditelné
      tft.fillCircle(x + 20, y + 10, 7, TFT_YELLOW);
      tft.drawCircle(x + 20, y + 10, 8, shadowCol);
      // Mrak - menší
      tft.fillCircle(x + 8, y + 14, 6, cloudCol);
      tft.fillCircle(x + 14, y + 11, 7, cloudCol);
      tft.fillCircle(x + 20, y + 14, 5, cloudCol);
      tft.fillRoundRect(x + 8, y + 14, 15, 5, 2, cloudCol);
      // Kapka - jednotlivá
      tft.fillRoundRect(x + 14, y + 21, 2, 5, 1, TFT_BLUE);
      break;
    
    case 95: case 96: case 99: // Bouřka
      // Tmavý mrak - 80% velikosti
      tft.fillCircle(x + 9, y + 13, 6, shadowCol);
      tft.fillCircle(x + 15, y + 10, 8, shadowCol);
      tft.fillCircle(x + 22, y + 13, 6, shadowCol);
      tft.fillRoundRect(x + 9, y + 13, 16, 6, 3, shadowCol);
      // Blesk - 80% velikosti
      tft.drawLine(x+15, y+20, x+12, y+27, TFT_YELLOW);
      tft.drawLine(x+12, y+27, x+17, y+27, TFT_YELLOW);
      tft.drawLine(x+17, y+27, x+14, y+34, TFT_YELLOW);
      break;
    
    default:
      // Mrak - 80% velikosti
      tft.fillCircle(x + 9, y + 13, 6, cloudCol);
      tft.fillCircle(x + 15, y + 10, 8, cloudCol);
      tft.fillCircle(x + 22, y + 13, 6, cloudCol);
      tft.fillRoundRect(x + 9, y + 13, 16, 6, 3, cloudCol);
      break;
  }
}


// ============================================
// ✅ NOVÁ FUNKCE PRO SPRÁVNÉ KRESLENÍ MĚSÍČNÍ FÁZE
// ============================================
// Vykresluje měsíční fázi (0-7) jako správnou grafiku
// Použije správnou geometrii pro každou fázi

void drawMoonPhaseIcon(int mx, int my, int r, int phase, uint16_t textColor, uint16_t bgColor) {
  
  // Barva pozadí kruhu
  uint16_t moonBg = (themeMode == 2) ? blueDark : (themeMode == 3) ? yellowDark : (isWhiteTheme ? 0xDEDB : 0x3186);
  uint16_t moonColor = TFT_YELLOW;
  uint16_t shadowColor = moonBg;
  
  // Obrys kruhu
  tft.drawCircle(mx, my, r, textColor);
  
  // Vyplnění podle fáze
  switch(phase) {
    
    case 0: {
      // NOV (New Moon) - Pouze obrys, vnitřek tmavý
      tft.fillCircle(mx, my, r - 1, shadowColor);
      break;
    }
    
    case 1: {
      // WAXING CRESCENT - Srpek z PRAVÉ strany (křivá hranice)
      tft.fillCircle(mx, my, r - 1, shadowColor);
      // Srpek se vytváří průnikem dvou kruhů - jeden je střed měsíce, druhý je posunutý
      int offset = r / 3;
      for (int dy = -r; dy <= r; dy++) {
        int dx_max = sqrt(r*r - dy*dy);
        // Hranice srpku je druhý kruh posunutý vpravo
        int light_boundary = sqrt(r*r - dy*dy - offset*offset) - offset;
        if (light_boundary < 0) light_boundary = 0;
        for (int dx = light_boundary; dx <= dx_max; dx++) {
          tft.drawPixel(mx + dx, my + dy, moonColor);
        }
      }
      break;
    }
    
    case 2: {
      // FIRST QUARTER - PRAVÁ polovina osvětlena
      tft.fillCircle(mx, my, r - 1, shadowColor);
      for (int dy = -r; dy <= r; dy++) {
        int dx_max = sqrt(r*r - dy*dy);
        for (int dx = 0; dx <= dx_max; dx++) {
          tft.drawPixel(mx + dx, my + dy, moonColor);
        }
      }
      break;
    }
    
    case 3: {
      // WAXING GIBBOUS - Skoro plný, srpek stínu zleva (křivá hranice)
      tft.fillCircle(mx, my, r - 1, moonColor);
      // Stín se vytváří průnikem dvou kruhů - jeden posunutý vlevo
      int offset = r / 3;
      for (int dy = -r; dy <= r; dy++) {
        int dx_max = sqrt(r*r - dy*dy);
        // Hranice stínu je druhý kruh posunutý vlevo
        int shadow_boundary = -(sqrt(r*r - dy*dy - offset*offset) - offset);
        if (shadow_boundary > 0) shadow_boundary = 0;
        for (int dx = -dx_max; dx <= shadow_boundary; dx++) {
          tft.drawPixel(mx + dx, my + dy, shadowColor);
        }
      }
      break;
    }
    
    case 4: {
      // FULL MOON - Zcela osvětlen
      tft.fillCircle(mx, my, r - 1, moonColor);
      break;
    }
    
    case 5: {
      // WANING GIBBOUS - Skoro plný, srpek stínu zprava (křivá hranice)
      tft.fillCircle(mx, my, r - 1, moonColor);
      // Stín se vytváří průnikem dvou kruhů - jeden posunutý vpravo
      int offset = r / 3;
      for (int dy = -r; dy <= r; dy++) {
        int dx_max = sqrt(r*r - dy*dy);
        // Hranice stínu je druhý kruh posunutý vpravo
        int shadow_boundary = sqrt(r*r - dy*dy - offset*offset) - offset;
        if (shadow_boundary < 0) shadow_boundary = 0;
        for (int dx = shadow_boundary; dx <= dx_max; dx++) {
          tft.drawPixel(mx + dx, my + dy, shadowColor);
        }
      }
      break;
    }
    
    case 6: {
      // LAST QUARTER - LEVÁ polovina osvětlena
      tft.fillCircle(mx, my, r - 1, shadowColor);
      for (int dy = -r; dy <= r; dy++) {
        int dx_max = sqrt(r*r - dy*dy);
        for (int dx = -dx_max; dx <= 0; dx++) {
          tft.drawPixel(mx + dx, my + dy, moonColor);
        }
      }
      break;
    }
    
    case 7: {
      // WANING CRESCENT - Srpek z LEVÉ strany (křivá hranice)
      tft.fillCircle(mx, my, r - 1, shadowColor);
      // Srpek se vytváří průnikem dvou kruhů - jeden je střed měsíce, druhý je posunutý
      int offset = r / 3;
      for (int dy = -r; dy <= r; dy++) {
        int dx_max = sqrt(r*r - dy*dy);
        // Hranice srpku je druhý kruh posunutý vlevo
        int light_boundary = -(sqrt(r*r - dy*dy - offset*offset) - offset);
        if (light_boundary > 0) light_boundary = 0;
        for (int dx = -dx_max; dx <= light_boundary; dx++) {
          tft.drawPixel(mx + dx, my + dy, moonColor);
        }
      }
      break;
    }
    
    default: {
      tft.drawCircle(mx, my, r, textColor);
      break;
    }
  }
}




int getMoonPhase(int y, int m, int d) {
  // Přesnější výpočet měsíční fáze
  // Založeno na astronomickém algoritmu s přesností na dny
  
  // Výpočet Julian Date Number
  if (m < 3) {
    y--;
    m += 12;
  }
  
  int a = y / 100;
  int b = 2 - a + (a / 4);
  
  long jd = (long)(365.25 * (y + 4716)) + 
            (long)(30.6001 * (m + 1)) + 
            d + b - 1524;
  
  // Výpočet fáze měsíce
  // Referenční nov: 6. ledna 2000, 18:14 UTC (JD 2451550.26)
  double daysSinceNew = jd - 2451550.1;
  
  // Lunární cyklus je 29.53058867 dní
  double lunationCycle = 29.53058867;
  double currentLunation = daysSinceNew / lunationCycle;
  
  // Získáme pozici v aktuálním cyklu (0.0 - 1.0)
  double phasePosition = currentLunation - floor(currentLunation);
  
  // Konverze na 8 fází (0-7) s přesnými hranicemi
  // Každá fáze zabírá 1/8 cyklu, hranice jsou uprostřed přechodů
  int phase;
  if (phasePosition < 0.0625) phase = 0;       // New Moon (0.000 - 0.062)
  else if (phasePosition < 0.1875) phase = 1;  // Waxing Crescent (0.062 - 0.188)
  else if (phasePosition < 0.3125) phase = 2;  // First Quarter (0.188 - 0.312)
  else if (phasePosition < 0.4375) phase = 3;  // Waxing Gibbous (0.312 - 0.438)
  else if (phasePosition < 0.5625) phase = 4;  // Full Moon (0.438 - 0.562)
  else if (phasePosition < 0.6875) phase = 5;  // Waning Gibbous (0.562 - 0.688)
  else if (phasePosition < 0.8125) phase = 6;  // Last Quarter (0.688 - 0.812)
  else if (phasePosition < 0.9375) phase = 7;  // Waning Crescent (0.812 - 0.938)
  else phase = 0;                               // New Moon (0.938 - 1.000)
  
  return phase;
}

String todayNameday = "--";
int lastNamedayDay = -1;
int lastNamedayHour = -1;
bool namedayValid = false;

#define T_CS 33
#define T_IRQ 36
#define T_CLK 25
#define T_DIN 32
#define T_DOUT 39
#define LCD_BL_PIN 21

String countryToISO(String country) {
  country.toLowerCase();
  if (country.indexOf("czech") >= 0) return "CZ";
  if (country.indexOf("slovak") >= 0) return "SK";
  if (country.indexOf("german") >= 0) return "DE";
  if (country.indexOf("austria") >= 0) return "AT";
  if (country.indexOf("nether") >= 0) return "NL";
  if (country.indexOf("poland") >= 0) return "PL";
  if (country.indexOf("france") >= 0) return "FR";
  if (country.indexOf("italy") >= 0) return "IT";
  if (country.indexOf("spain") >= 0) return "ES";
  if (country.indexOf("united states") >= 0) return "US";
  if (country.indexOf("united kingdom") >= 0) return "GB";
  return "US";
}

String removeDiacritics(String input) {
  String output = input;
  // Malá písmena
  output.replace("á", "a"); output.replace("č", "c"); output.replace("ď", "d");
  output.replace("é", "e"); output.replace("ě", "e"); output.replace("í", "i");
  output.replace("ľ", "l"); output.replace("ĺ", "l"); output.replace("ň", "n");
  output.replace("ó", "o"); output.replace("ô", "o"); output.replace("ř", "r");
  output.replace("š", "s"); output.replace("ť", "t"); output.replace("ú", "u");
  output.replace("ů", "u"); output.replace("ý", "y"); output.replace("ž", "z");
  
  // Velká písmena
  output.replace("Á", "A"); output.replace("Č", "C"); output.replace("Ď", "D");
  output.replace("É", "E"); output.replace("Ě", "E"); output.replace("Í", "I");
  output.replace("Ľ", "L"); output.replace("Ĺ", "L"); output.replace("Ň", "N");
  output.replace("Ó", "O"); output.replace("Ô", "O"); output.replace("Ř", "R");
  output.replace("Š", "S"); output.replace("Ť", "T"); output.replace("Ú", "U");
  output.replace("Ů", "U"); output.replace("Ý", "Y"); output.replace("Ž", "Z");
  
  return output;
}

XPT2046_Touchscreen ts(T_CS, T_IRQ);

const char* ntpServer = "pool.ntp.org";
long gmtOffset_sec = 3600;
int daylightOffset_sec = 0;

const int clockX = 230;
const int clockY = 85;
const int radius = 67;
int lastHour = -1, lastMin = -1, lastSec = -1, lastDay = -1;
int brightness = 255;
String cityName = "Plzen";
unsigned long lastWifiStatusCheck = 0;
int lastWifiStatus = -1;
bool forceClockRedraw = false;

enum ScreenState {
  CLOCK, SETTINGS, WIFICONFIG, KEYBOARD, WEATHERCONFIG, REGIONALCONFIG, GRAPHICSCONFIG, FIRMWARE_SETTINGS, COUNTRYSELECT, CITYSELECT, LOCATIONCONFIRM, CUSTOMCITYINPUT, CUSTOMCOUNTRYINPUT, COUNTRYLOOKUPCONFIRM, CITYLOOKUPCONFIRM
};
ScreenState currentState = CLOCK;

bool regionAutoMode = true;
String selectedCountry = "Czech Republic";
String selectedCity;
String selectedTimezone;
String customCityInput;
String customCountryInput;
String lookupCountry;
String lookupCity;
String lookupTimezone;
String countryName = "Czech Republic"; 
String timezoneName = "Europe/Prague"; 
int lookupGmtOffset = 3600;
int lookupDstOffset = 3600;

#define MAX_RECENT_CITIES 10
struct RecentCity {
  String city;
  String country;
  String timezone;
  int gmtOffset;
  int dstOffset;
};
RecentCity recentCities[MAX_RECENT_CITIES];
int recentCount = 0;

unsigned long lastTouchTime = 0;
int menuOffset = 0;
int countryOffset = 0;
int cityOffset = 0;
const int MENU_BASE_Y = 70;
const int MENU_ITEM_HEIGHT = 35;
const int MENU_ITEM_GAP = 8;
const int MENU_ITEM_SPACING = MENU_ITEM_HEIGHT + MENU_ITEM_GAP;

String ssid, password, selectedSSID, passwordBuffer;
const int MAX_NETWORKS = 20;
String wifiSSIDs[MAX_NETWORKS];
int wifiCount = 0, wifiOffset = 0;
bool keyboardNumbers = false;
bool keyboardShift = false;
bool showPassword = false; // Výchozí stav: heslo je skryté (hvězdičky)

const int TOUCH_X_MIN = 200;
const int TOUCH_X_MAX = 3900;
const int TOUCH_Y_MIN = 200;
const int TOUCH_Y_MAX = 3900;
const int SCREEN_WIDTH = 320;
const int SCREEN_HEIGHT = 240;

struct CityEntry {
  const char* name;
  const char* timezone;
  int gmtOffset;
  int dstOffset;
};

const CityEntry czechCities[] = {
  {"Brno", "Europe/Prague", 3600, 3600},
  {"Ceska Budejovice", "Europe/Prague", 3600, 3600},
  {"Jihlava", "Europe/Prague", 3600, 3600},
  {"Karlovy Vary", "Europe/Prague", 3600, 3600},
  {"Liberec", "Europe/Prague", 3600, 3600},
  {"Olomouc", "Europe/Prague", 3600, 3600},
  {"Ostrava", "Europe/Prague", 3600, 3600},
  {"Pardubice", "Europe/Prague", 3600, 3600},
  {"Plzen", "Europe/Prague", 3600, 3600},
  {"Praha", "Europe/Prague", 3600, 3600},
};

const CityEntry slovakCities[] = {
  {"Banska Bystrica", "Europe/Bratislava", 3600, 3600},
  {"Bardejov", "Europe/Bratislava", 3600, 3600},
  {"Bratislava", "Europe/Bratislava", 3600, 3600},
  {"Kosice", "Europe/Bratislava", 3600, 3600},
  {"Liptovsky Mikulas", "Europe/Bratislava", 3600, 3600},
  {"Lucenec", "Europe/Bratislava", 3600, 3600},
  {"Nitra", "Europe/Bratislava", 3600, 3600},
  {"Poprad", "Europe/Bratislava", 3600, 3600},
  {"Presov", "Europe/Bratislava", 3600, 3600},
  {"Zilina", "Europe/Bratislava", 3600, 3600},
};

const CityEntry germanyCities[] = {
  {"Aachen", "Europe/Berlin", 3600, 3600},
  {"Berlin", "Europe/Berlin", 3600, 3600},
  {"Cologne", "Europe/Berlin", 3600, 3600},
  {"Dortmund", "Europe/Berlin", 3600, 3600},
  {"Dresden", "Europe/Berlin", 3600, 3600},
  {"Dusseldorf", "Europe/Berlin", 3600, 3600},
  {"Essen", "Europe/Berlin", 3600, 3600},
  {"Frankfurt", "Europe/Berlin", 3600, 3600},
  {"Hamburg", "Europe/Berlin", 3600, 3600},
  {"Munich", "Europe/Berlin", 3600, 3600},
};

const CityEntry austriaCities[] = {
  {"Dornbirn", "Europe/Vienna", 3600, 3600},
  {"Graz", "Europe/Vienna", 3600, 3600},
  {"Hallein", "Europe/Vienna", 3600, 3600},
  {"Innsbruck", "Europe/Vienna", 3600, 3600},
  {"Klagenfurt", "Europe/Vienna", 3600, 3600},
  {"Linz", "Europe/Vienna", 3600, 3600},
  {"Salzburg", "Europe/Vienna", 3600, 3600},
  {"Sankt Polten", "Europe/Vienna", 3600, 3600},
  {"Vienna", "Europe/Vienna", 3600, 3600},
  {"Wels", "Europe/Vienna", 3600, 3600},
};

const CityEntry netherlandsCities[] = {
  {"Amsterdam", "Europe/Amsterdam", 3600, 3600},
  {"Rotterdam", "Europe/Amsterdam", 3600, 3600},
  {"The Hague", "Europe/Amsterdam", 3600, 3600},
  {"Utrecht", "Europe/Amsterdam", 3600, 3600},
  {"Eindhoven", "Europe/Amsterdam", 3600, 3600},
  {"Groningen", "Europe/Amsterdam", 3600, 3600},
  {"Tilburg", "Europe/Amsterdam", 3600, 3600},
  {"Almere", "Europe/Amsterdam", 3600, 3600},
  {"Breda", "Europe/Amsterdam", 3600, 3600},
  {"Nijmegen", "Europe/Amsterdam", 3600, 3600},
};

const CityEntry polonyCities[] = {
  {"Bialystok", "Europe/Warsaw", 3600, 3600},
  {"Bydgoszcz", "Europe/Warsaw", 3600, 3600},
  {"Cracow", "Europe/Warsaw", 3600, 3600},
  {"Gdansk", "Europe/Warsaw", 3600, 3600},
  {"Gdynia", "Europe/Warsaw", 3600, 3600},
  {"Katowice", "Europe/Warsaw", 3600, 3600},
  {"Krakow", "Europe/Warsaw", 3600, 3600},
  {"Poznan", "Europe/Warsaw", 3600, 3600},
  {"Szczecin", "Europe/Warsaw", 3600, 3600},
  {"Warsaw", "Europe/Warsaw", 3600, 3600},
};

const CityEntry franceCities[] = {
  {"Amiens", "Europe/Paris", 3600, 3600},
  {"Bordeaux", "Europe/Paris", 3600, 3600},
  {"Brest", "Europe/Paris", 3600, 3600},
  {"Dijon", "Europe/Paris", 3600, 3600},
  {"Grenoble", "Europe/Paris", 3600, 3600},
  {"Lille", "Europe/Paris", 3600, 3600},
  {"Lyon", "Europe/Paris", 3600, 3600},
  {"Marseille", "Europe/Paris", 3600, 3600},
  {"Paris", "Europe/Paris", 3600, 3600},
  {"Toulouse", "Europe/Paris", 3600, 3600},
};

const CityEntry unitedStatesCities[] = {
  {"Atlanta", "America/Chicago", -18000, 3600},
  {"Boston", "America/New_York", -18000, 3600},
  {"Chicago", "America/Chicago", -18000, 3600},
  {"Denver", "America/Denver", -25200, 3600},
  {"Houston", "America/Chicago", -18000, 3600},
  {"Los Angeles", "America/Los_Angeles", -28800, 3600},
  {"Miami", "America/New_York", -18000, 3600},
  {"New York", "America/New_York", -18000, 3600},
  {"Philadelphia", "America/New_York", -18000, 3600},
  {"Seattle", "America/Los_Angeles", -28800, 3600},
};

const CityEntry unitedKingdomCities[] = {
  {"Bath", "Europe/London", 0, 3600},
  {"Belfast", "Europe/London", 0, 3600},
  {"Birmingham", "Europe/London", 0, 3600},
  {"Bristol", "Europe/London", 0, 3600},
  {"Cardiff", "Europe/London", 0, 3600},
  {"Edinburgh", "Europe/London", 0, 3600},
  {"Leeds", "Europe/London", 0, 3600},
  {"Liverpool", "Europe/London", 0, 3600},
  {"London", "Europe/London", 0, 3600},
  {"Manchester", "Europe/London", 0, 3600},
};

const CityEntry japanCities[] = {
  {"Aomori", "Asia/Tokyo", 32400, 0},
  {"Fukuoka", "Asia/Tokyo", 32400, 0},
  {"Hiroshima", "Asia/Tokyo", 32400, 0},
  {"Kobe", "Asia/Tokyo", 32400, 0},
  {"Kyoto", "Asia/Tokyo", 32400, 0},
  {"Nagoya", "Asia/Tokyo", 32400, 0},
  {"Osaka", "Asia/Tokyo", 32400, 0},
  {"Sapporo", "Asia/Tokyo", 32400, 0},
  {"Tokyo", "Asia/Tokyo", 32400, 0},
  {"Yokohama", "Asia/Tokyo", 32400, 0},
};

const CityEntry australiaCities[] = {
  {"Adelaide", "Australia/Adelaide", 34200, 3600},
  {"Brisbane", "Australia/Brisbane", 36000, 0},
  {"Canberra", "Australia/Sydney", 36000, 3600},
  {"Darwin", "Australia/Darwin", 34200, 0},
  {"Hobart", "Australia/Hobart", 36000, 3600},
  {"Melbourne", "Australia/Melbourne", 36000, 3600},
  {"Perth", "Australia/Perth", 28800, 0},
  {"Sydney", "Australia/Sydney", 36000, 3600},
  {"Townsville", "Australia/Brisbane", 36000, 0},
  {"Wollongong", "Australia/Sydney", 36000, 3600},
};

const CityEntry chinaCities[] = {
  {"Beijing", "Asia/Shanghai", 28800, 0},
  {"Chongqing", "Asia/Shanghai", 28800, 0},
  {"Guangzhou", "Asia/Shanghai", 28800, 0},
  {"Hangzhou", "Asia/Shanghai", 28800, 0},
  {"Hong Kong", "Asia/Hong_Kong", 28800, 0},
  {"Shanghai", "Asia/Shanghai", 28800, 0},
  {"Shenzhen", "Asia/Shanghai", 28800, 0},
  {"Tianjin", "Asia/Shanghai", 28800, 0},
  {"Wuhan", "Asia/Shanghai", 28800, 0},
  {"Xian", "Asia/Shanghai", 28800, 0},
};

struct CountryEntry {
  const char* code;
  const char* name;
  const CityEntry* cities;
  int cityCount;
};

const CountryEntry countries[] = {
  {"AT", "Austria", austriaCities, 10},
  {"AU", "Australia", australiaCities, 10},
  {"CN", "China", chinaCities, 10},
  {"CZ", "Czech Republic", czechCities, 10},
  {"DE", "Germany", germanyCities, 10},
  {"FR", "France", franceCities, 10},
  {"GB", "United Kingdom", unitedKingdomCities, 10},
  {"JP", "Japan", japanCities, 10},
  {"NL", "Netherlands", netherlandsCities, 10},
  {"PL", "Poland", polonyCities, 10},
  {"SK", "Slovakia", slovakCities, 10},
};
const int COUNTRIES_COUNT = 11;

uint16_t getBgColor() { 
  if (themeMode == 0) return isWhiteTheme ? TFT_WHITE : TFT_BLACK;
  if (themeMode == 1) return isWhiteTheme ? TFT_WHITE : TFT_BLACK;
  if (themeMode == 2) return blueDark; // MODRÁ - tmavé pozadí
  if (themeMode == 3) return yellowDark; // ŽLUTÁ - tmavé pozadí
  return TFT_BLACK;
}

uint16_t getTextColor() { 
  if (themeMode == 0) return isWhiteTheme ? TFT_BLACK : TFT_WHITE;
  if (themeMode == 1) return isWhiteTheme ? TFT_BLACK : TFT_WHITE;
  if (themeMode == 2) return blueLight; // MODRÁ - světlý text
  if (themeMode == 3) return yellowLight; // ŽLUTÁ - světlý text
  return TFT_WHITE;
}

uint16_t getSecHandColor() { 
  if (themeMode == 2) return yellowLight;   // Sekundová ručička v modrém tématu = žlutá
  if (themeMode == 3) return blueLight;     // Sekundová ručička v žlutém tématu = modrá
  return isWhiteTheme ? TFT_RED : TFT_YELLOW; 
}


void drawWifiIndicator() {
  int wifiStatus = WiFi.status();
  uint16_t color = wifiStatus == WL_CONNECTED ? TFT_GREEN : TFT_RED;
  tft.fillCircle(300, 20, 6, color);
}

// Ikona dostupné aktualizace (zelená šipka vedle WiFi)
void drawUpdateIndicator() {
  if (!updateAvailable) return;
  
  int iconX = 310;  // Vedle WiFi ikony
  int iconY = 12;
  
  // Zelená šipka dolů (download symbol)
  tft.fillTriangle(iconX, iconY + 8, iconX + 4, iconY, iconX + 8, iconY + 8, TFT_GREEN);
  tft.fillRect(iconX + 2, iconY + 8, 4, 6, TFT_GREEN);
  tft.fillRect(iconX, iconY + 14, 8, 2, TFT_GREEN);
}

void loadRecentCities() {
  prefs.begin("sys", false);
  for (int i = 0; i < MAX_RECENT_CITIES; i++) {
    String prefix = "recent" + String(i);
    String city = prefs.getString((prefix + "c").c_str(), "");
    if (city.length() == 0) break;
    recentCities[i].city = city;
    recentCities[i].country = prefs.getString((prefix + "co").c_str(), "");
    recentCities[i].timezone = prefs.getString((prefix + "tz").c_str(), "");
    recentCities[i].gmtOffset = prefs.getInt((prefix + "go").c_str(), 3600);
    recentCities[i].dstOffset = prefs.getInt((prefix + "do").c_str(), 3600);
    recentCount++;
  }
  prefs.end();
}

void addToRecentCities(String city, String country, String timezone, int gmtOffset, int dstOffset) {
  for (int i = 0; i < recentCount; i++) {
    if (recentCities[i].city == city && recentCities[i].country == country) {
      RecentCity temp = recentCities[i];
      for (int j = i; j > 0; j--) recentCities[j] = recentCities[j - 1];
      recentCities[0] = temp;
      return;
    }
  }
  if (recentCount < MAX_RECENT_CITIES) {
    for (int i = recentCount - 1; i >= 0; i--) recentCities[i + 1] = recentCities[i];
  } else {
    recentCount = MAX_RECENT_CITIES - 1;
    for (int i = recentCount - 1; i >= 0; i--) recentCities[i + 1] = recentCities[i];
  }
  recentCities[0].city = city;
  recentCities[0].country = country;
  recentCities[0].timezone = timezone;
  recentCities[0].gmtOffset = gmtOffset;
  recentCities[0].dstOffset = dstOffset;

  prefs.begin("sys", false);
  for (int i = 0; i < recentCount; i++) {
    String prefix = "recent" + String(i);
    prefs.putString((prefix + "c").c_str(), recentCities[i].city);
    prefs.putString((prefix + "co").c_str(), recentCities[i].country);
    prefs.putString((prefix + "tz").c_str(), recentCities[i].timezone);
    prefs.putInt((prefix + "go").c_str(), recentCities[i].gmtOffset);
    prefs.putInt((prefix + "do").c_str(), recentCities[i].dstOffset);
  }
  prefs.end();
}


String toTitleCase(String input) {
  input.toLowerCase();
  if (input.length() > 0) input[0] = toupper(input[0]);
  for (int i = 1; i < input.length(); i++) {
    if (input[i - 1] == ' ') input[i] = toupper(input[i]);
  }
  return input;
}

bool fuzzyMatch(String input, String target) {
  String inp = input;
  String tgt = target;
  inp.toLowerCase();
  tgt.toLowerCase();
  if (inp == tgt) return true;
  if (tgt.indexOf(inp) >= 0) return true;
  if (inp.length() >= 3 && tgt.indexOf(inp.substring(0, 3)) >= 0) return true;
  return false;
}

bool lookupCountryRESTAPI(String countryName) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[LOOKUP-REST] WiFi not connected");
    return false;
  }
  countryName = toTitleCase(countryName);
  Serial.println("[LOOKUP-REST] Searching REST API " + countryName);

  HTTPClient http;
  http.setTimeout(8000);

  String searchName = countryName;
  searchName.replace(" ", "%20");
  String url = "https://restcountries.com/v3.1/name/" + searchName + "?fullText=false";
  Serial.println("[LOOKUP-REST] URL " + url);

  http.begin(url);
  http.setUserAgent("ESP32");

  int httpCode = http.GET();
  Serial.println("[LOOKUP-REST] HTTP Code " + String(httpCode));

  if (httpCode != 200) {
    Serial.print("[LOOKUP-REST] HTTP Error ");
    Serial.println(httpCode);
    http.end();
    return false;
  }

  String response = http.getString();
  
  StaticJsonDocument<2000> doc;
  DeserializationError error = deserializeJson(doc, response);
  if (error) {
    Serial.println("[LOOKUP-REST] JSON error " + String(error.c_str()));
    http.end();
    return false;
  }

  if (doc.is<JsonArray>()) {
    JsonArray arr = doc.as<JsonArray>();
    if (arr.size() > 0) {
      JsonObject first = arr[0];
      if (first["name"].is<JsonObject>()) {
        JsonObject nameObj = first["name"];
        if (nameObj["common"].is<const char*>()) {
          lookupCountry = nameObj["common"].as<String>();
          Serial.println("[LOOKUP-REST] FOUND " + lookupCountry);
          http.end();
          return true;
        }
      }
    }
  }

  Serial.println("[LOOKUP-REST] HTTP Error " + String(httpCode));
  http.end();
  return false;
}

bool lookupCountryEmbedded(String countryName) {
  countryName = toTitleCase(countryName);
  Serial.println("[LOOKUP-EMB] Searching embedded " + countryName);
  for (int i = 0; i < COUNTRIES_COUNT; i++) {
    if (fuzzyMatch(countryName, String(countries[i].name))) {
      lookupCountry = String(countries[i].name);
      Serial.println("[LOOKUP-EMB] FOUND " + lookupCountry);
      return true;
    }
  }
  return false;
}

bool lookupCountryGeonames(String countryName) {
  if (lookupCountryEmbedded(countryName)) return true;
  if (WiFi.status() == WL_CONNECTED) {
    if (lookupCountryRESTAPI(countryName)) return true;
  }
  return false;
}

// ============================================
// OPRAVA 1: Získání Timezone z API (pro celý svět)
// ============================================
void detectTimezoneFromCoords(float lat, float lon, String countryHint) {
  if (WiFi.status() != WL_CONNECTED) {
    // Fallback pokud není wifi, ale to by se při lookupu nemělo stát
    lookupTimezone = "Europe/Prague";
    lookupGmtOffset = 3600;
    lookupDstOffset = 3600;
    return;
  }

  Serial.println("[TZ-AUTO] Detecting timezone from API for: " + String(lat,4) + ", " + String(lon,4));
  
  HTTPClient http;
  // Použijeme Open-Meteo, které vrací "utc_offset_seconds" a "timezone"
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(lat, 4) + "&longitude=" + String(lon, 4) + "&timezone=auto&daily=weather_code&foreground_days=1";
  
  http.setTimeout(8000);
  http.begin(url);
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048); // Zvětšený buffer
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      // 1. Získat název zóny
      if (doc.containsKey("timezone")) {
        lookupTimezone = doc["timezone"].as<String>();
      } else {
        lookupTimezone = "UTC";
      }

      // 2. Získat offset v sekundách
      if (doc.containsKey("utc_offset_seconds")) {
        int totalOffset = doc["utc_offset_seconds"].as<int>();
        
        // Nastavíme GMT offset na aktuální posun a DST na 0 
        // (protože API vrací už sečtený offset včetně letního času)
        lookupGmtOffset = totalOffset;
        lookupDstOffset = 0; 
        
        Serial.println("[TZ-AUTO] API Found: " + lookupTimezone + " Offset: " + String(totalOffset));
        http.end();
        return;
      }
    } else {
      Serial.println("[TZ-AUTO] JSON Error");
    }
  } else {
    Serial.println("[TZ-AUTO] HTTP Error: " + String(httpCode));
  }
  http.end();

  // Fallback pokud API selže - alespoň zkusíme základní regiony podle hintu
  Serial.println("[TZ-AUTO] API Failed, using basic fallback");
  if (countryHint == "United Kingdom" || countryHint == "Ireland" || countryHint == "Portugal") {
     lookupTimezone = "Europe/London"; lookupGmtOffset = 0; lookupDstOffset = 3600;
  } else if (countryHint == "China") {
     lookupTimezone = "Asia/Shanghai"; lookupGmtOffset = 28800; lookupDstOffset = 0;
  } else if (countryHint == "Japan") {
     lookupTimezone = "Asia/Tokyo"; lookupGmtOffset = 32400; lookupDstOffset = 0;
  } else if (countryHint == "Netherlands") {
     lookupTimezone = "Europe/Amsterdam"; lookupGmtOffset = 3600; lookupDstOffset = 3600;
  } else if (countryHint.indexOf("America") >= 0 || countryHint == "Canada" || countryHint == "USA") {
     // Hrubý odhad pro Ameriku pokud API selže
     lookupTimezone = "America/New_York"; lookupGmtOffset = -18000; lookupDstOffset = 3600;
  } else {
     lookupTimezone = "Europe/Prague"; lookupGmtOffset = 3600; lookupDstOffset = 3600;
  }
}

// ============================================
// OPRAVA 2: Ukládání do GLOBÁLNÍCH souřadnic
// ============================================
bool lookupCityNominatim(String cityName, String countryHint) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[LOOKUP-CITY-NOM] WiFi not connected");
    return false;
  }
  cityName = toTitleCase(cityName);
  Serial.println("[LOOKUP-CITY-NOM] Searching " + cityName + " in " + countryHint);

  HTTPClient http;
  http.setTimeout(12000);

  String searchCity = cityName;
  searchCity.replace(" ", "%20");
  String searchCountry = countryHint;
  searchCountry.replace(" ", "%20");
  String url = "https://nominatim.openstreetmap.org/search?format=json&addressdetails=1&limit=1&q=" + searchCity + "%2C" + searchCountry;
  Serial.println("[LOOKUP-CITY-NOM] URL " + url);

  http.begin(url);
  http.addHeader("User-Agent", "ESP32-DataDisplay/1.0"); // Nominatim vyžaduje User-Agent
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String response = http.getString();
    StaticJsonDocument<4000> doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
      Serial.println("[LOOKUP-CITY-NOM] JSON error");
      http.end(); return false;
    }

    if (doc.is<JsonArray>()) {
      JsonArray arr = doc.as<JsonArray>();
      if (arr.size() > 0) {
        JsonObject first = arr[0];
        if (first["name"].is<const char*>() && first["lat"].is<const char*>() && first["lon"].is<const char*>()) {
          lookupCity = first["name"].as<String>();
          
          // ZDE BYLA CHYBA: odstraněno "float" před lat/lon, aby se zapsalo do globálních proměnných
          lat = atof(first["lat"].as<const char*>());
          lon = atof(first["lon"].as<const char*>());
          
          Serial.println("[LOOKUP-CITY-NOM] FOUND " + lookupCity + " Lat " + String(lat, 4) + ", Lon " + String(lon, 4));
          
          // Zavoláme detekci zóny s nalezenými souřadnicemi
          detectTimezoneFromCoords(lat, lon, countryHint);
          
          Serial.println("[LOOKUP-CITY-NOM] Timezone set " + lookupTimezone);
          http.end();
          return true;
        }
      }
    }
  }
  http.end();
  return false;
}

bool lookupCityGeonames(String cityName, String countryHint) {
  cityName = toTitleCase(cityName);
  Serial.println("[LOOKUP-CITY] Searching " + cityName + " in " + countryHint);

  for (int i = 0; i < COUNTRIES_COUNT; i++) {
    if (String(countries[i].name) == countryHint || String(countries[i].code) == countryHint) {
      for (int j = 0; j < countries[i].cityCount; j++) {
        if (fuzzyMatch(cityName, countries[i].cities[j].name)) {
          lookupCity = countries[i].cities[j].name;
          lookupTimezone = countries[i].cities[j].timezone;
          lookupGmtOffset = countries[i].cities[j].gmtOffset;
          lookupDstOffset = countries[i].cities[j].dstOffset;
          Serial.println("[LOOKUP-CITY] FOUND in embedded " + lookupCity);
          return true;
        }
      }
    }
  }

  Serial.println("[LOOKUP-CITY] NOT in embedded DB, trying Nominatim API...");
  if (WiFi.status() == WL_CONNECTED) {
    if (lookupCityNominatim(cityName, countryHint)) {
      Serial.println("[LOOKUP-CITY] FOUND via Nominatim");
      return true;
    } else {
      Serial.println("[LOOKUP-CITY] WiFi not connected, cannot use Nominatim");
    }
  }
  Serial.println("[LOOKUP-CITY] NOT FOUND anywhere");
  return false;
}

void getCountryCities(String countryName, String cities[], int &count) {
  count = 0;
  for (int i = 0; i < COUNTRIES_COUNT; i++) {
    if (String(countries[i].name) == countryName) {
      count = countries[i].cityCount;
      for (int j = 0; j < count; j++) {
        cities[j] = countries[i].cities[j].name;
      }
      return;
    }
  }
}

bool getTimezoneForCity(String countryName, String city, String &timezone, int &gmt, int &dst) {
  for (int i = 0; i < COUNTRIES_COUNT; i++) {
    if (String(countries[i].name) == countryName) {
      for (int j = 0; j < countries[i].cityCount; j++) {
        if (String(countries[i].cities[j].name) == city) {
          timezone = countries[i].cities[j].timezone;
          gmt = countries[i].cities[j].gmtOffset;
          dst = countries[i].cities[j].dstOffset;
          return true;
        }
      }
    }
  }
  return false;
}

void drawClockFace();
void drawClockStatic();
void drawDateAndWeek(const struct tm *ti);
void updateHands(int h, int m, int s);
void drawSettingsIcon(uint16_t color);
void drawSettingsScreen();
void drawWeatherScreen();
void drawRegionalScreen();
void drawGraphicsScreen();
void drawInitialSetup();
void drawKeyboardScreen();
void drawCountrySelection();
void drawCitySelection();
void drawLocationConfirm();
void drawCustomCityInput();
void drawCustomCountryInput();
void drawCountryLookupConfirm();
void drawCityLookupConfirm();
void scanWifiNetworks();
void drawArrowBack(int x, int y, uint16_t color);
void drawArrowDown(int x, int y, uint16_t color);
void drawArrowUp(int x, int y, uint16_t color);
void showWifiConnectingScreen(String ssid);
void showWifiResultScreen(bool success);
void handleNamedayUpdate();


int getMenuItemY(int itemIndex) {
  return MENU_BASE_Y + itemIndex * MENU_ITEM_SPACING;
}

bool isTouchInMenuItem(int y, int itemIndex) {
  int yPos = getMenuItemY(itemIndex);
  return (y >= yPos && y <= yPos + MENU_ITEM_HEIGHT);
}

void drawSettingsIcon(uint16_t color) {
  int ix = 300, iy = 220;
  int rIn = 3, rMid = 6, rOut = 8;
  tft.fillCircle(ix, iy, rMid, color);
  tft.fillCircle(ix, iy, rIn, getBgColor());
  #define DEGTORAD (PI / 180.0)
  for (int i = 0; i < 8; i++) {
    float a = i * 45 * DEGTORAD;
    float aL = a - 0.2;
    float aR = a + 0.2;
    tft.fillTriangle(ix + cos(aL) * rMid, iy + sin(aL) * rMid, ix + cos(aR) * rMid, iy + sin(aR) * rMid, ix + cos(a) * rOut, iy + sin(a) * rOut, color);
  }
}

void drawArrowBack(int x, int y, uint16_t color) {
  tft.drawRoundRect(x, y, 50, 50, 4, color);
  tft.drawLine(x + 35, y + 15, x + 20, y + 25, color);
  tft.drawLine(x + 35, y + 35, x + 20, y + 25, color);
  tft.drawLine(x + 34, y + 15, x + 19, y + 25, color);
  tft.drawLine(x + 34, y + 35, x + 19, y + 25, color);
}

void syncRegion() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println("[AUTO] Syncing region...");
  
  HTTPClient http;
  http.setTimeout(5000);
  http.begin("http://ip-api.com/json?fields=status,city,timezone");

  int httpCode = http.GET();
  if (httpCode == 200) {
    StaticJsonDocument<400> doc;
    DeserializationError error = deserializeJson(doc, http.getString());
    
    if (!error && doc["status"] == "success") {
      // 1. Získat data z API do pomocných proměnných
      String detectedCity = doc["city"].as<String>();
      String detectedTimezone = doc["timezone"].as<String>();
      
      Serial.println("[AUTO] Detected: " + detectedCity + ", TZ: " + detectedTimezone);

      // 2. Nastavit globální 'selected' proměnné pro applyLocation
      selectedCity = detectedCity;
      selectedTimezone = detectedTimezone;
      
      // Detekce země a offsetů podle časové zóny
      if (detectedTimezone.indexOf("Prague") >= 0) {
        selectedCountry = "Czech Republic";
        gmtOffset_sec = 3600; daylightOffset_sec = 3600;
      } else if (detectedTimezone.indexOf("Berlin") >= 0) {
        selectedCountry = "Germany";
        gmtOffset_sec = 3600; daylightOffset_sec = 3600;
      } else if (detectedTimezone.indexOf("Amsterdam") >= 0) {
        selectedCountry = "Netherlands";
        gmtOffset_sec = 3600; daylightOffset_sec = 3600;
      } else if (detectedTimezone.indexOf("Warsaw") >= 0) {
        selectedCountry = "Poland";
        gmtOffset_sec = 3600; daylightOffset_sec = 3600;
      } else if (detectedTimezone.indexOf("Bratislava") >= 0) {
        selectedCountry = "Slovakia";
        gmtOffset_sec = 3600; daylightOffset_sec = 3600;
      } else if (detectedTimezone.indexOf("Paris") >= 0) {
        selectedCountry = "France";
        gmtOffset_sec = 3600; daylightOffset_sec = 3600;
      } else if (detectedTimezone.indexOf("London") >= 0) {
        selectedCountry = "United Kingdom";
        gmtOffset_sec = 0; daylightOffset_sec = 3600;
      } else if (detectedTimezone.indexOf("New_York") >= 0) {
        selectedCountry = "United States";
        gmtOffset_sec = -18000; daylightOffset_sec = 3600;
      } else if (detectedTimezone.indexOf("Tokyo") >= 0) {
        selectedCountry = "Japan";
        gmtOffset_sec = 32400; daylightOffset_sec = 0;
      } else if (detectedTimezone.indexOf("Shanghai") >= 0 || detectedTimezone.indexOf("Hong_Kong") >= 0) {
        selectedCountry = "China";
        gmtOffset_sec = 28800; daylightOffset_sec = 0;
      } else if (detectedTimezone.indexOf("Sydney") >= 0 || detectedTimezone.indexOf("Melbourne") >= 0) {
        selectedCountry = "Australia";
        gmtOffset_sec = 36000; daylightOffset_sec = 3600;
      } else {
        // Fallback pokud neznáme zónu - necháme Czech Republic nebo stávající
        if (selectedCountry == "") {
           selectedCountry = "Czech Republic";
           gmtOffset_sec = 3600; daylightOffset_sec = 3600;
        }
      }
      
      Serial.println("[AUTO] SelectedCountry set to: " + selectedCountry);

      // 3. APLIKOVAT ZMĚNY (Uloží, nastaví čas a hlavně RESETUJE POČASÍ)
      applyLocation();
      
    } else {
      Serial.println("[AUTO] JSON Parsing error or status not success");
    }
  } else {
    Serial.println("[AUTO] HTTP Error: " + String(httpCode));
  }
  http.end();
}

void drawArrowDown(int x, int y, uint16_t color) {
  tft.drawRoundRect(x, y, 50, 50, 4, color);
  tft.drawLine(x + 15, y + 20, x + 25, y + 35, color);
  tft.drawLine(x + 35, y + 20, x + 25, y + 35, color);
  tft.drawLine(x + 15, y + 21, x + 25, y + 36, color);
  tft.drawLine(x + 35, y + 21, x + 25, y + 36, color);
}

void drawArrowUp(int x, int y, uint16_t color) {
  tft.drawRoundRect(x, y, 50, 50, 4, color);
  tft.drawLine(x + 15, y + 35, x + 25, y + 20, color);
  tft.drawLine(x + 35, y + 35, x + 25, y + 20, color);
  tft.drawLine(x + 15, y + 34, x + 25, y + 19, color);
  tft.drawLine(x + 35, y + 34, x + 25, y + 19, color);
}

void showWifiConnectingScreen(String ssid) {
  tft.fillScreen(getBgColor());
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Connecting to", 160, 80, 2);
  tft.setTextColor(TFT_SKYBLUE);
  tft.drawString(ssid, 160, 110, 2);
  tft.setTextColor(getTextColor());
  tft.drawString("Please wait...", 160, 150, 2);
}

void showWifiResultScreen(bool success) {
  tft.fillScreen(getBgColor());
  if (success) {
    tft.setTextColor(TFT_GREEN);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Connection Successful!", 160, 100, 2);
  } else {
    tft.setTextColor(TFT_RED);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Connection FAILED", 160, 100, 2);
  }
  delay(2000);
}

void scanWifiNetworks() {
  tft.fillScreen(getBgColor());
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Scanning WiFi...", 160, 120, 2);

  WiFi.mode(WIFI_STA);
  if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false);

  int n = WiFi.scanNetworks();
  wifiCount = (n > 0) ? min(n, MAX_NETWORKS) : 0;

  for (int i = 0; i < wifiCount; i++) {
    wifiSSIDs[i] = WiFi.SSID(i);
  }
  Serial.println("[WIFI] Scan complete. Found " + String(wifiCount) + " networks");
}

void drawSettingsScreen()
{
  tft.fillScreen(getBgColor());
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("SETTINGS", 160, 30, 4);

  String menuItems[] = {"WiFi Setup", "Weather", "Regional", "Graphics", "Firmware"};  // PŘIDÁNO Firmware
  uint16_t colors[] = {TFT_BLUE, TFT_BLUE, TFT_BLUE, TFT_BLUE, TFT_BLUE};  // Přidána 5. barva

  int totalItems = 5;  // ZMĚNĚNO z 4 na 5
  int visibleItems = 4;  // Kolik se vejde na obrazovku najednou

  for (int i = 0; i < totalItems; i++) {
    if (i >= menuOffset && i < menuOffset + visibleItems) {
      int yPos = getMenuItemY(i - menuOffset);
      tft.drawRoundRect(40, yPos, 180, MENU_ITEM_HEIGHT, 6, colors[i]);
      tft.drawRoundRect(39, yPos-1, 182, MENU_ITEM_HEIGHT+2, 6, colors[i]);  // Silný rámeček!
      tft.fillRoundRect(41, yPos+1, 178, MENU_ITEM_HEIGHT-2, 5, getBgColor());  // Výplň
      tft.drawString(menuItems[i], 130, yPos + 17, 2);
    }
  }

  // Šipka nahoru (pokud nejsme na začátku)
  if (menuOffset > 0) {
    tft.drawRoundRect(230, 70, 50, 50, 4, TFT_BLUE);
    drawArrowUp(230, 70, TFT_BLUE);
  }

  // Tlačítko ZPĚT
  tft.drawRoundRect(230, 125, 50, 50, 4, TFT_RED);
  drawArrowBack(230, 125, TFT_RED);

  // Šipka dolů (pokud je více než 4 položky)
  if (menuOffset < (totalItems - visibleItems)) {
    tft.drawRoundRect(230, 180, 50, 50, 4, TFT_BLUE);
    drawArrowDown(230, 180, TFT_BLUE);
  }
}

// ... (ZDE POKRAČUJE ZBYTEK KÓDU - kvůli délce byla část vynechána, ale structure zůstává stejná)
// Zbývající funkce jako drawWeatherScreen(), drawRegionalScreen(), setup(), loop() atd. jsou stejné jako v originále
