/*  ВЕРСИЯ 2.0! Установи новую версию библиотеки из реопзитория!
   Система управляет количеством помп PUPM_AMOUNT, подключенных подряд в пины платы, начиная с пина START_PIN.
   На каждую помпу заводится таймер, который включает помпу на заданное время через заданные промежутки времени.

   Включение производится сигналом уровня SWITCH_LEVEL. 0 - для реле низкого уровня (0 Вольт, все семейные модули реле),
   1 - высокого уровня (5 Вольт, редкие модули реле, все мосфеты).
   Примечание: катушка реле кушает около 60 мА, несколько включенных вместе катушек создадут лишнюю нагрузку
   на линию питания. Также несколько включенных одновременно помп сделают то же самое. Для устранения этого эффекта
   есть настройка PARALLEL. При её отключении помпы будут "вставать в очередь", совместное включение будет исключено.

   Управление:
   Поворот - смена позиции стрелки
   Удержанный поворот - изменение значения
   Крути энкодер дальше настройки секунд ПЕРИОДА, чтобы перейти к настройке времени РАБОТЫ!
   Кнопка энкодера удерживается при включении системы - сброс настроек

   Интерфейс отображается на дисплее 1602 с драйвером на I2C. Версий драйвера существует две.
   Смотри настройку DRIVER_VERSION ниже.

   Версия 2.0: новая система таймеров и меню
*/

#define LCD_BACKL 1         // автоотключение подсветки дисплея (1 - разрешить) 
#define BACKL_TOUT 30       // таймаут отключения дисплея, секунды
#define ENCODER_TYPE 1      // тип энкодера (0 или 1). Если энкодер работает некорректно (пропуск шагов), смените тип
#define ENC_REVERSE 1       // 1 - инвертировать энкодер, 0 - нет
#define DRIVER_VERSION 1    // 0 - маркировка драйвера дисплея кончается на 4АТ, 1 - на 4Т
#define PUPM_AMOUNT 16      // количество помп, подключенных через реле/мосфет
#define START_PIN 0         // подключены начиная с пина
#define SWITCH_LEVEL 0      // реле: 1 - высокого уровня (или мосфет), 0 - низкого
#define PARALLEL 0          // 1 - параллельный полив, 0 - полив в порядке очереди
#define TIMER_START 1       // 1 - отсчёт периода с момента ВЫКЛЮЧЕНИЯ помпы, 0 - с момента ВКЛЮЧЕНИЯ помпы

#define PUMP_PIN 4        // это реле, ведущее на грязную воду помпу
#define PUMP_PIN1 5       // это реле, ведущее на чистую воду помпу


// названия каналов управления. БУКВУ L НЕ ТРОГАТЬ БЛЕТ!!!!!!
static const wchar_t *relayNames[]  = {
  L"Зона  1",
  L"Зона  2",
  L"Зона  3",
  L"Зона  4",
  L"Зона  5",
  L"Зона  6",
  L"Зона  7",
  L"Зона  8",
  L"Зона  9",
  L"Зона 10",
  L"Зона 11",
  L"Зона 12",
  L"Зона 13",
  L"Зона 14",
  L"Зона 15",
  L"Зона 16",
};

#define CLK 3
#define DT 2
#define SW 0

#include "PCF8574.h"               // Подключение библиотеки PCF8574
PCF8574 pcf8574_a(0x20);            // Создаем объект и указываем адрес устройства 0x20
PCF8574 pcf8574_b(0x21);            // Создаем объект и указываем адрес устройства 0x21

#include "GyverEncoder.h"
Encoder enc1(CLK, DT, SW);

#include <EEPROMex.h>
#include <EEPROMVar.h>

#include "LCD_1602_RUS.h"

// -------- АВТОВЫБОР ОПРЕДЕЛЕНИЯ ДИСПЛЕЯ-------------
// Если кончается на 4Т - это 0х27. Если на 4АТ - 0х3f
#if (DRIVER_VERSION)
LCD_1602_RUS lcd(0x27, 16, 2);
#else
LCD_1602_RUS lcd(0x3f, 16, 2);
#endif
// -------- АВТОВЫБОР ОПРЕДЕЛЕНИЯ ДИСПЛЕЯ-------------

uint32_t pump_timers[PUPM_AMOUNT];
uint32_t pumping_time[PUPM_AMOUNT];
uint32_t period_time[PUPM_AMOUNT];
boolean pump_state[PUPM_AMOUNT];
byte pump_pins[PUPM_AMOUNT];
boolean pump_finished[PUPM_AMOUNT];  // зона уже полита

int8_t current_set;
int8_t current_pump;
boolean now_pumping;
boolean backlState = true;
uint32_t backlTimer;


int8_t thisH, thisM, thisS;
long thisPeriod;
boolean dryState = true;   // какой клапан открыт. true - dry(грязная) false - чистая



void setup() {
  // pcf8574_a.begin();         //  инициализация библиотеки pcf8574
  // pcf8574_b.begin();
  // delay(50);                 // Необходимо время для инициализации
  // --------------------- КОНФИГУРИРУЕМ ПИНЫ ---------------------
  for (byte i = 0; i < PUPM_AMOUNT; i++) {            // пробегаем по всем помпам
    pump_pins[i] = START_PIN + i;                     // настраиваем массив пинов
    if (pump_pins[i] < 8) {pcf8574_a.pinMode(START_PIN + i, OUTPUT);                  // настраиваем пины
                          pcf8574_a.digitalWrite(START_PIN + i, !SWITCH_LEVEL);}
    else                  {pcf8574_b.pinMode(START_PIN + i - 8, OUTPUT);                   
                          pcf8574_b.digitalWrite(START_PIN + i - 8, !SWITCH_LEVEL);}       // выключаем от греха
    pump_finished[i] = true;
  }
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(PUMP_PIN1, OUTPUT);
  digitalWrite(PUMP_PIN, !SWITCH_LEVEL);         
  digitalWrite(PUMP_PIN1, !SWITCH_LEVEL);        // выключаем от греха реле переключения воды

  // --------------------- ИНИЦИАЛИЗИРУЕМ ЖЕЛЕЗО ---------------------
  // Serial.begin(9600);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  //enc1.setStepNorm(1);
  //attachInterrupt(0, encISR, CHANGE);
  enc1.setType(ENCODER_TYPE);
  if (ENC_REVERSE) enc1.setDirection(REVERSE);

  // --------------------- СБРОС НАСТРОЕК ---------------------
  if (!digitalRead(SW)) {          // если нажат энкодер, сбросить настройки до 1
    lcd.setCursor(0, 0);
    lcd.print("Reset settings");
    for (byte i = 0; i < 500; i++) {
      EEPROM.writeLong(i, 0);
    }
  }
  while (!digitalRead(SW));        // ждём отпускания кнопки
  lcd.clear();                     // очищаем дисплей, продолжаем работу

  // --------------------------- НАСТРОЙКИ ---------------------------
  // в ячейке 1023 должен быть записан флажок, если его нет - делаем (ПЕРВЫЙ ЗАПУСК)
  if (EEPROM.read(1023) != 5) {
    EEPROM.writeByte(1023, 5);

    // для порядку сделаем 1 ячейки с 0 по 500
    for (byte i = 0; i < 500; i += 4) {
      EEPROM.writeLong(i, 0);
    }
  }

  for (byte i = 0; i < PUPM_AMOUNT; i++) {            // пробегаем по всем помпам
    period_time[i] = EEPROM.readLong(8 * i);          // читаем данные из памяти. На чётных - период (ч)
    pumping_time[i] = EEPROM.readLong(8 * i + 4);     // на нечётных - полив (с)

    if (SWITCH_LEVEL)			// вырубить все помпы
      pump_state[i] = 0;
    else
      pump_state[i] = 1;
  }

  // ---------------------- ВЫВОД НА ДИСПЛЕЙ ------------------------
  drawLabels();
  changeSet();
}

void loop() {
  encoderTick();
  periodTick();
  flowTick();
  backlTick();
}

void backlTick() {
  if (LCD_BACKL && backlState && millis() - backlTimer >= BACKL_TOUT * 1000) {
    backlState = false;
    lcd.noBacklight();
  }
}
void backlOn() {
  backlState = true;
  backlTimer = millis();
  lcd.backlight();
  
}
void periodTick() {
  for (byte i = 0; i < PUPM_AMOUNT; i++) {            // пробегаем по всем помпам
    if (period_time[i] > 0
         && pumping_time[i] > 0                       // если общее время полива зоны ноль - пропускаем зону
         && millis() - pump_timers[i] >= period_time[i] * 1000
         && (pump_state[i] != SWITCH_LEVEL)
         &&  !pump_finished[i]                        // если зона уже поливалась - пропускаем 
         && !(now_pumping * !PARALLEL)) {
      pump_state[i] = SWITCH_LEVEL;
      digitalWrite(PUMP_PIN, SWITCH_LEVEL);           // включить грязную воду
      dryState = true;                                // флаг грязной воды поднять
      if (pump_pins[i] < 8) pcf8574_a.digitalWrite(pump_pins[i], SWITCH_LEVEL);
      else                  pcf8574_b.digitalWrite(pump_pins[i] - 8, SWITCH_LEVEL);
      pump_timers[i] = millis();
      now_pumping = true;
      lcd.setCursor(10, 0);                           // вывод текущей операции на экран
      lcd.print("г.вода #" + String(i + 1));
      // Serial.println("dry clapan ON");
      //Serial.println("clear clapan" + " OFF");
      // Serial.println("Pump #" + String(i) + " ON");
    }
    // переключение воды с грязной на чистую
     if (period_time[i] > 0                          // если помпа качает и счетчик больше чем period_time
         &&millis() - pump_timers[i] >= period_time[i] * 1000
         && (pump_state[i] == SWITCH_LEVEL)
         && (dryState)) {
      digitalWrite(PUMP_PIN, !SWITCH_LEVEL);         // выключить грязную воду
      dryState = false;                              // флаг грязной воды снять
      digitalWrite(PUMP_PIN1, SWITCH_LEVEL);         // включить чистую воду
      lcd.setCursor(10, 0);                          // вывод текущей операции на экран
      lcd.print("ч.вода #" + String(i + 1));
      // Serial.println("dry clapan OFF");
      // Serial.println("clear clapan ON");
    }
    // добавлено к гайверу
  }
}

void flowTick() {
  for (byte i = 0; i < PUPM_AMOUNT; i++) {            // пробегаем по всем помпам
    if (pumping_time[i] > 0
        && millis() - pump_timers[i] >= pumping_time[i] * 1000
        && (pump_state[i] == SWITCH_LEVEL) ) {
      pump_state[i] = !SWITCH_LEVEL;
      digitalWrite(PUMP_PIN1, !SWITCH_LEVEL);                                    // выключить чистую воду
      digitalWrite(PUMP_PIN, !SWITCH_LEVEL);                                     // выключить грязную воду
      if (pump_pins[i] < 8) pcf8574_a.digitalWrite(pump_pins[i], !SWITCH_LEVEL); // выключить зону
      else                  pcf8574_b.digitalWrite(pump_pins[i] - 8, !SWITCH_LEVEL);
      if (TIMER_START) pump_timers[i] = millis();
      now_pumping = false;
      pump_finished[i] = true;
      lcd.setCursor(10, 0);                                                       // очистка текущей операции на экране
      lcd.print("          ");
      // Serial.println("clear clapan OFF");
      // Serial.println("Pump #" + String(i) + " OFF");
    }
  }
}

/*
  void encISR() {
  enc1.tick();  // отработка энкодера
  }
*/

void encoderTick() {
  enc1.tick();    // отработка энкодера
  if (enc1.isDouble()) { // двойной клик
    for (byte i = 0; i < PUPM_AMOUNT; i++) {            // пробегаем по всем помпам
      pump_finished[i] = false;                         // сброс переменных политых зон(старт полива) 
  }
  }
  if (enc1.isTurn()) {                               // если был совершён поворот
    if (backlState) {
      backlTimer = millis();      // сбросить таймаут дисплея
      if (enc1.isRight()) {
        if (++current_set >= 7) current_set = 6;
      } else if (enc1.isLeft()) {
        if (--current_set < 0) current_set = 0;
      }

      if (enc1.isRightH())
        changeSettings(1);
      else if (enc1.isLeftH())
        changeSettings(-1);

      changeSet();
    } else {
      backlOn();      // включить дисплей
    }
  }
}

// тут меняем номер помпы и настройки
void changeSettings(int increment) {
  if (current_set == 0) {
    current_pump += increment;
    if (current_pump > PUPM_AMOUNT - 1) current_pump = PUPM_AMOUNT - 1;
    if (current_pump < 0) current_pump = 0;
    s_to_hms(period_time[current_pump]);
    drawLabels();
  } else {
    if (current_set == 1 || current_set == 4) {
      thisH += increment;
    } else if (current_set == 2 || current_set == 5) {
      thisM += increment;
    } else if (current_set == 3 || current_set == 6) {
      thisS += increment;
    }
    if (thisS > 59) {
      thisS = 0;
      thisM++;
    }
    if (thisM > 59) {
      thisM = 0;
      thisH++;
    }
    if (thisS < 0) {
      if (thisM > 0) {
        thisS = 59;
        thisM--;
      } else thisS = 0;
    }
    if (thisM < 0) {
      if (thisH > 0) {
        thisM = 59;
        thisH--;
      } else thisM = 0;
    }
    if (thisH < 0) thisH = 0;
    if (current_set < 4) period_time[current_pump] = hms_to_s();
    else pumping_time[current_pump] = hms_to_s();
  }
}

// вывести название реле
void drawLabels() {
  lcd.setCursor(1, 0);
  lcd.print("                ");
  lcd.setCursor(1, 0);
  lcd.print(relayNames[current_pump]);
}

// изменение позиции стрелки и вывод данных
void changeSet() {
  switch (current_set) {
    case 0: drawArrow(0, 0); update_EEPROM();
      break;
    case 1: drawArrow(7, 1);
      break;
    case 2: drawArrow(10, 1);
      break;
    case 3: drawArrow(13, 1);
      break;
    case 4: drawArrow(7, 1);
      break;
    case 5: drawArrow(10, 1);
      break;
    case 6: drawArrow(13, 1);
      break;
  }
  lcd.setCursor(0, 1);
  if (current_set < 4) {
    lcd.print(L"г.ВОДА");
    s_to_hms(period_time[current_pump]);
  }
  else {
    lcd.print(L"ПОЛИВ ");
    s_to_hms(pumping_time[current_pump]);
  }
  lcd.setCursor(8, 1);
  if (thisH < 10) lcd.print(0);
  lcd.print(thisH);
  lcd.setCursor(11, 1);
  if (thisM < 10) lcd.print(0);
  lcd.print(thisM);
  lcd.setCursor(14, 1);
  if (thisS < 10) lcd.print(0);
  lcd.print(thisS);
}

// перевод секунд в ЧЧ:ММ:СС
void s_to_hms(uint32_t period) {
  thisH = floor((long)period / 3600);    // секунды в часы
  thisM = floor((period - (long)thisH * 3600) / 60);
  thisS = period - (long)thisH * 3600 - thisM * 60;
}

// перевод ЧЧ:ММ:СС в секунды
uint32_t hms_to_s() {
  return ((long)thisH * 3600 + thisM * 60 + thisS);
}

// отрисовка стрелки и двоеточий
void drawArrow(byte col, byte row) {
  lcd.setCursor(0, 0); lcd.print(" ");
  lcd.setCursor(7, 1); lcd.print(" ");
  lcd.setCursor(10, 1); lcd.print(":");
  lcd.setCursor(13, 1); lcd.print(":");
  lcd.setCursor(col, row); lcd.write(126);
}

// обновляем данные в памяти
void update_EEPROM() {
  EEPROM.updateLong(8 * current_pump, period_time[current_pump]);
  EEPROM.updateLong(8 * current_pump + 4, pumping_time[current_pump]);
  //Serial.println("pumping_time " + String(pumping_time[current_pump]));
}
