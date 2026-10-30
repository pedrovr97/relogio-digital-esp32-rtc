
#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>

// ======================================
// CONFIGURACOES DO HARDWARE
// ======================================

#define LCD_ADDR 0x27 // Se nao funcionar, teste 0x3F
#define LCD_COLS 16
#define LCD_ROWS 2

#define SDA_PIN 21
#define SCL_PIN 22

#define BTN_MENU 25
#define BTN_UP 26 // Confirme o fio fisico do botao MAIS
#define BTN_DOWN 27
#define BTN_OK 15 // Confirme o fio fisico do botao OK

#define BUZZER_PIN 5

LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

RTC_DS3231 rtc;
Preferences memoria;

// ======================================
// BOTOES
// ======================================

enum Tecla {
    KEY_MENU = 0,
    KEY_UP = 1,
    KEY_DOWN = 2,
    KEY_CONFIRM = 3
};

const uint8_t buttonPins[4] = {
    BTN_MENU, BTN_UP, BTN_DOWN, BTN_OK
};

bool lastReading[4] = {
    HIGH, HIGH, HIGH, HIGH
};

bool stableState[4] = {
    HIGH, HIGH, HIGH, HIGH
};

uint32_t lastChange[4] = {0, 0, 0, 0};

bool pressed(uint8_t key) {
    bool reading = digitalRead(buttonPins[key]);

    if (reading != lastReading[key]) {
        lastChange[key] = millis();
    }

    lastReading[key] = reading;

    if (millis() - lastChange[key] >= 35) {
        if (reading != stableState[key]) {
            stableState[key] = reading;
            return reading == LOW;
        }
    }

    return false;
}

// ======================================
// TELAS
// ======================================

enum Screen {
    CLOCK_SCREEN,
    STOPWATCH_SCREEN,
    ALARM_SCREEN,
    SETTINGS_SCREEN
};

Screen currentScreen = CLOCK_SCREEN;

const char* weekdays[7] = {
    "DOM", "SEG", "TER", "QUA",
    "QUI", "SEX", "SAB"
};

// ======================================
// STATUS DO RTC
// ======================================

bool rtcLostPower = false;

void verificarEstadoRTC() {
    rtcLostPower = rtc.lostPower();

    if (rtcLostPower) {
        Serial.println(
            "ATENCAO: RTC sinalizou perda de alimentacao!"
        );
        Serial.println(
            "A hora NAO sera alterada automaticamente."
        );
        Serial.println(
            "Ajuste data e hora pelos botoes."
        );
    } else {
        Serial.println(
            "RTC nao sinalizou perda de alimentacao."
        );
    }

    DateTime now = rtc.now();

    Serial.printf(
        "RTC: %04d-%02d-%02d %02d:%02d:%02d\n",
        now.year(), now.month(), now.day(),
        now.hour(), now.minute(), now.second()
    );
}

// ======================================
// CRONOMETRO
// ======================================

bool stopwatchRunning = false;
uint32_t stopwatchStart = 0;
uint64_t stopwatchAccumulated = 0;

uint64_t stopwatchTime() {
    uint64_t elapsed = stopwatchAccumulated;

    if (stopwatchRunning) {
        elapsed += (uint32_t)(
            millis() - stopwatchStart
        );
    }

    return elapsed;
}

void toggleStopwatch() {
    if (stopwatchRunning) {
        stopwatchAccumulated = stopwatchTime();
        stopwatchRunning = false;
    } else {
        stopwatchStart = millis();
        stopwatchRunning = true;
    }
}

void resetStopwatch() {
    if (!stopwatchRunning) {
        stopwatchAccumulated = 0;
    }
}

// ======================================
// DESPERTADOR
// ======================================

uint8_t alarmHour = 7;
uint8_t alarmMinute = 0;
bool alarmEnabled = false;
bool alarmRinging = false;

uint8_t alarmField = 0;

uint32_t alarmStart = 0;
uint32_t lastAlarmDate = 0;

const uint32_t ALARM_DURATION = 30000;

void saveAlarm() {
    memoria.putUChar("hour", alarmHour);
    memoria.putUChar("minute", alarmMinute);
    memoria.putBool("enabled", alarmEnabled);
}

void loadAlarm() {
    alarmHour = memoria.getUChar("hour", 7);
    alarmMinute = memoria.getUChar("minute", 0);
    alarmEnabled = memoria.getBool("enabled", false);

    if (alarmHour > 23) alarmHour = 7;
    if (alarmMinute > 59) alarmMinute = 0;
}

void stopAlarm() {
    alarmRinging = false;
    digitalWrite(BUZZER_PIN, LOW);
}

void checkAlarm(const DateTime& now) {
    uint32_t today =
        (uint32_t)now.year() * 10000UL +
        (uint32_t)now.month() * 100UL +
        now.day();

    if (
        !rtcLostPower &&
        alarmEnabled &&
        !alarmRinging &&
        now.hour() == alarmHour &&
        now.minute() == alarmMinute &&
        lastAlarmDate != today
    ) {
        alarmRinging = true;
        alarmStart = millis();
        lastAlarmDate = today;
    }

    if (alarmRinging) {
        if (millis() - alarmStart >= ALARM_DURATION) {
            stopAlarm();
        } else {
            bool beep = ((millis() / 250) % 2) == 0;
            digitalWrite(
                BUZZER_PIN,
                beep ? HIGH : LOW
            );
        }
    }
}

// ======================================
// AJUSTE MANUAL DE DATA E HORA
// ======================================

int editYear = 2026;
int editMonth = 1;
int editDay = 1;
int editHour = 12;
int editMinute = 0;

uint8_t editField = 0;

int daysInMonth(int year, int month) {
    const int days[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (month < 1 || month > 12) return 31;

    bool leap =
        year % 4 == 0 &&
        (year % 100 != 0 || year % 400 == 0);

    if (month == 2 && leap) return 29;

    return days[month - 1];
}

void beginTimeEdit() {
    DateTime now = rtc.now();

    if (!rtcLostPower &&
        now.year() >= 2020 &&
        now.year() <= 2099) {
        editYear = now.year();
        editMonth = now.month();
        editDay = now.day();
        editHour = now.hour();
        editMinute = now.minute();
    } else {
        editYear = 2026;
        editMonth = 10;
        editDay = 14;
        editHour = 12;
        editMinute = 0;
    }

    editField = 0;
    currentScreen = SETTINGS_SCREEN;
}

void changeTimeField(int delta) {
    switch (editField) {
        case 0:
            editHour = (editHour + delta + 24) % 24;
            break;

        case 1:
            editMinute = (editMinute + delta + 60) % 60;
            break;

        case 2: {
            int maxDay = daysInMonth(
                editYear, editMonth
            );

            editDay += delta;

            if (editDay < 1) editDay = maxDay;
            if (editDay > maxDay) editDay = 1;
            break;
        }

        case 3:
            editMonth =
                (editMonth - 1 + delta + 12) % 12 + 1;
            break;

        case 4:
            editYear += delta;

            if (editYear < 2020) editYear = 2099;
            if (editYear > 2099) editYear = 2020;
            break;
    }

    int maxDay = daysInMonth(
        editYear, editMonth
    );

    if (editDay > maxDay) {
        editDay = maxDay;
    }
}

void confirmTimeEdit() {
    if (editField < 4) {
        editField++;
        return;
    }

    DateTime newTime(
        editYear,
        editMonth,
        editDay,
        editHour,
        editMinute,
        0
    );

    rtc.adjust(newTime);

    // rtc.adjust() limpa o indicador OSF.
    rtcLostPower = rtc.lostPower();

    // Evita disparo imediato se o novo horario coincidir com o alarme.
    if (alarmEnabled && editHour == alarmHour && editMinute == alarmMinute) {
        lastAlarmDate = (uint32_t)editYear * 10000UL +
                        (uint32_t)editMonth * 100UL + editDay;
    } else {
        lastAlarmDate = 0;
    }
    currentScreen = CLOCK_SCREEN;

    Serial.println("Horario ajustado manualmente.");

    verificarEstadoRTC();
}

// ======================================
// PROCESSAMENTO DOS BOTOES
// ======================================

void handleButtons() {
    bool menu = pressed(KEY_MENU);
    bool up = pressed(KEY_UP);
    bool down = pressed(KEY_DOWN);
    bool confirm = pressed(KEY_CONFIRM);

    if (alarmRinging) {
        if (menu || up || down || confirm) {
            stopAlarm();
        }
        return;
    }

    switch (currentScreen) {

        case CLOCK_SCREEN:
            if (menu) {
                currentScreen = STOPWATCH_SCREEN;
            } else if (confirm) {
                beginTimeEdit();
            }
            break;

        case STOPWATCH_SCREEN:
            if (menu) {
                alarmField = 0;
                currentScreen = ALARM_SCREEN;
            } else if (confirm) {
                toggleStopwatch();
            } else if (down) {
                resetStopwatch();
            }
            break;

        case ALARM_SCREEN:
            if (menu) {
                saveAlarm();
                beginTimeEdit();
            } else if (confirm) {
                alarmField = (alarmField + 1) % 3;
            } else if (up || down) {
                int delta = up ? 1 : -1;

                if (alarmField == 0) {
                    alarmHour =
                        (alarmHour + delta + 24) % 24;
                } else if (alarmField == 1) {
                    alarmMinute =
                        (alarmMinute + delta + 60) % 60;
                } else {
                    alarmEnabled = !alarmEnabled;
                    if (alarmEnabled) {
                        DateTime atual = rtc.now();
                        if (atual.hour() == alarmHour && atual.minute() == alarmMinute) {
                            lastAlarmDate = (uint32_t)atual.year() * 10000UL +
                                            (uint32_t)atual.month() * 100UL + atual.day();
                        }
                    }
                }

                saveAlarm();
            }
            break;

        case SETTINGS_SCREEN:
            if (menu) {
                currentScreen = CLOCK_SCREEN;
            } else if (up) {
                changeTimeField(1);
            } else if (down) {
                changeTimeField(-1);
            } else if (confirm) {
                confirmTimeEdit();
            }
            break;
    }
}

// ======================================
// LCD 16x2: sempre preencher 16 colunas
// ======================================
void lcdLine(uint8_t row, const char* text) {
    char line[17];
    snprintf(line, sizeof(line), "%-16.16s", text);
    lcd.setCursor(0, row);
    lcd.print(line);
}

void drawClock(const DateTime& now) {
    char line[32];
    // Primeira linha: dia, data e indicacao de alarme
    snprintf(line, sizeof(line), "%s %02d/%02d/%04d %c",
             weekdays[now.dayOfTheWeek()], now.day(), now.month(),
             now.year(), alarmEnabled ? 'A' : '-');
    lcdLine(0, line);

    if (rtcLostPower) {
        lcdLine(1, "AJUSTE O HORARIO");
    } else {
        snprintf(line, sizeof(line), "    %02d:%02d:%02d    ",
                 now.hour(), now.minute(), now.second());
        lcdLine(1, line);
    }
}

void drawStopwatch() {
    lcdLine(0, stopwatchRunning ? "CRONO: RODANDO" : "CRONO: PAUSADO");
    uint64_t elapsed = stopwatchTime();
    char line[24];
    snprintf(line, sizeof(line), "  %02lu:%02lu.%02lu",
             (unsigned long)((elapsed / 60000ULL) % 100),
             (unsigned long)((elapsed / 1000ULL) % 60),
             (unsigned long)((elapsed % 1000ULL) / 10));
    lcdLine(1, line);
}

void drawAlarm() {
    const char* fields[] = {"HORA", "MINUTO", "ON/OFF"};
    char line[32];
    snprintf(line, sizeof(line), "ALARME %s", fields[alarmField]);
    lcdLine(0, line);
    snprintf(line, sizeof(line), "%02d:%02d  %s",
             alarmHour, alarmMinute, alarmEnabled ? "LIGADO" : "DESLIG");
    lcdLine(1, line);
}

void drawSettings() {
    const char* fields[] = {"HORA", "MIN", "DIA", "MES", "ANO"};
    char line[32];
    snprintf(line, sizeof(line), "AJUSTAR: %s", fields[editField]);
    lcdLine(0, line);
    snprintf(line, sizeof(line), "%02d:%02d %02d/%02d/%04d",
             editHour, editMinute, editDay, editMonth, editYear);
    lcdLine(1, line);
}

void updateDisplay(const DateTime& now) {
    if (alarmRinging) {
        lcdLine(0, "ALARME TOCANDO!");
        lcdLine(1, "APERTE UM BOTAO");
        return;
    }
    switch (currentScreen) {
        case CLOCK_SCREEN: drawClock(now); break;
        case STOPWATCH_SCREEN: drawStopwatch(); break;
        case ALARM_SCREEN: drawAlarm(); break;
        case SETTINGS_SCREEN: drawSettings(); break;
    }
}

// ======================================
// SETUP
// ======================================

void setup() {
    Serial.begin(115200);
    delay(300);

    pinMode(BTN_MENU, INPUT_PULLUP);
    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_OK, INPUT_PULLUP);

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setTimeOut(100);

    lcd.init();
    lcd.backlight();
    lcdLine(0, "RELOGIO ESP32");
    lcdLine(1, "INICIANDO...");

    if (!rtc.begin()) {
        Serial.println("ERRO: DS3231 nao encontrado!");

        lcdLine(0, "ERRO NO RTC");
        lcdLine(1, "VERIFIQUE FIOS");

        while (true) {
            delay(1000);
        }
    }

    // IMPORTANTE:
    // Nao ajustar o RTC automaticamente.
    // Isso evita sobrescrever a hora no diagnostico.
    verificarEstadoRTC();

    memoria.begin("relogio", false);
    loadAlarm();

    Serial.println("Sistema inicializado.");
    Serial.println("MENU: navegar");
    Serial.println("OK: configurar horario");
    Serial.println("RTC sem ajuste automatico.");

    delay(800);
}

// ======================================
// LOOP PRINCIPAL
// ======================================

void loop() {
    handleButtons();

    DateTime now = rtc.now();

    checkAlarm(now);

    static uint32_t lastDisplay = 0;

    if (millis() - lastDisplay >= 200) {
        lastDisplay = millis();
        updateDisplay(now);
    }

    static uint32_t lastSerial = 0;

    if (millis() - lastSerial >= 10000) {
        lastSerial = millis();

        Serial.printf(
            "Hora RTC: %02d:%02d:%02d\n",
            now.hour(),
            now.minute(),
            now.second()
        );
    }

    delay(2);
}
