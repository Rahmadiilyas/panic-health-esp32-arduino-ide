#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ================= WIFI =================
const char* ssid = "Redmi 13";
const char* password = "87654321";
const char* serverName = "http://172.24.27.190:8000/api/readings";

// ================= PIN =================
const int pulsePin = 32;
const int mq135Pin = 34;
const int buzzerPin = 5;
const int oneWireBus = 4;   // ubah ke 15 jika DS18B20 berhasil di GPIO 15

// ================= LCD =================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= SENSOR SUHU =================
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

// ================= DATA =================
float suhuGlobal = 0;
float suhuTerakhirValid = 0;

int gasGlobal = 0;
float bpm = 0;
int pulseValue = 0;

// ================= TIMER =================
unsigned long tSend = 0;
unsigned long tLCD = 0;
unsigned long tSensor = 0;
unsigned long tSerial = 0;

int lcdMode = 0;

// ================= BPM STABIL =================
bool pulseDetected = false;
unsigned long lastBeatTime = 0;
unsigned long currentBeatTime = 0;

// dari data kamu:
// bawah sekitar 400 - 1700
// puncak sekitar 3200
int thresholdHigh = 2500;
int thresholdLow  = 1800;

float bpmBuffer[8] = {0, 0, 0, 0, 0, 0, 0, 0};
int bpmIndex = 0;

// ================= BATAS ALARM =================
int batasBpmTinggi = 100;
float batasSuhuTinggi = 38.0;
int batasGasBahayaLCD = 850;
int batasGasBahayaBuzzer = 850;

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(buzzerPin, OUTPUT);
  pinMode(pulsePin, INPUT);
  pinMode(mq135Pin, INPUT);

  Wire.begin();

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("Connecting WiFi");

  sensors.begin();

  Serial.println("\n=== CEK SENSOR SUHU ===");
  Serial.print("Jumlah DS18B20: ");
  Serial.println(sensors.getDeviceCount());

  WiFi.begin(ssid, password);

  Serial.println("\n=== CONNECT WIFI ===");

  int wifiTry = 0;
  while (WiFi.status() != WL_CONNECTED && wifiTry < 30) {
    delay(500);
    Serial.print(".");
    wifiTry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected");
    Serial.print("IP ESP32: ");
    Serial.println(WiFi.localIP());

    lcd.clear();
    lcd.print("WiFi OK");
    delay(1500);
  } else {
    Serial.println("\nWiFi gagal connect");
    lcd.clear();
    lcd.print("WiFi Failed");
    delay(1500);
  }

  lcd.clear();
}

// ================= LOOP =================
void loop() {
  unsigned long now = millis();

  // Pulse dibaca terus agar BPM stabil
  bacaPulse();

  // Suhu dan gas cukup dibaca tiap 1 detik
  if (now - tSensor >= 1000) {
    tSensor = now;

    bacaSuhu();
    bacaGasStabil();
  }

  // Serial monitor tiap 1 detik
  if (now - tSerial >= 1000) {
    tSerial = now;
    tampilSerial();
  }

  // LCD bergantian tiap 2 detik
  if (now - tLCD >= 2000) {
    tLCD = now;
    tampilLCD();
  }

  // Buzzer
  kontrolBuzzer();

  // Kirim data tiap 10 detik
  if (WiFi.status() == WL_CONNECTED && now - tSend >= 10000) {
    tSend = now;
    kirimData();
  }
}

// ================= BACA SUHU =================
void bacaSuhu() {
  sensors.requestTemperatures();
  float suhu = sensors.getTempCByIndex(0);

  if (suhu == DEVICE_DISCONNECTED_C || suhu == -127.00 || suhu < -50) {
    Serial.println("Sensor suhu tidak terbaca!");

    // Jangan langsung jadikan 0 agar data lebih stabil
    if (suhuTerakhirValid > 0) {
      suhuGlobal = suhuTerakhirValid;
    } else {
      suhuGlobal = 0;
    }
  } else {
    suhuGlobal = suhu;
    suhuTerakhirValid = suhu;
  }
}

// ================= BACA GAS STABIL =================
void bacaGasStabil() {
  long total = 0;
  int jumlahSample = 10;

  for (int i = 0; i < jumlahSample; i++) {
    total += analogRead(mq135Pin);
    delay(2);
  }

  gasGlobal = total / jumlahSample;
}

// ================= BACA PULSE / BPM =================
void bacaPulse() {
  pulseValue = analogRead(pulsePin);

  // Deteksi puncak detak
  if (pulseValue > thresholdHigh && pulseDetected == false) {
    pulseDetected = true;
    currentBeatTime = millis();

    if (lastBeatTime > 0) {
      unsigned long beatInterval = currentBeatTime - lastBeatTime;

      // Filter detak manusia normal
      // 300 ms = 200 BPM
      // 2000 ms = 30 BPM
      if (beatInterval > 300 && beatInterval < 2000) {
        float bpmNow = 60000.0 / beatInterval;

        // filter lonjakan tidak masuk akal
        if (bpmNow >= 40 && bpmNow <= 180) {
          bpmBuffer[bpmIndex] = bpmNow;
          bpmIndex++;

          if (bpmIndex >= 8) {
            bpmIndex = 0;
          }

          float total = 0;
          int count = 0;

          for (int i = 0; i < 8; i++) {
            if (bpmBuffer[i] > 0) {
              total += bpmBuffer[i];
              count++;
            }
          }

          if (count > 0) {
            bpm = total / count;
          }
        }
      }
    }

    lastBeatTime = currentBeatTime;
  }

  // Reset jika sinyal turun
  if (pulseValue < thresholdLow) {
    pulseDetected = false;
  }

  // Kalau tidak ada detak lebih dari 5 detik
  if (lastBeatTime > 0 && millis() - lastBeatTime > 5000) {
    bpm = 0;

    for (int i = 0; i < 8; i++) {
      bpmBuffer[i] = 0;
    }
  }
}

// ================= STATUS BPM =================
String statusBPM() {
  if (bpm == 0) {
    return "NO SIGNAL";
  } else if (bpm > batasBpmTinggi) {
    return "TINGGI";
  } else {
    return "NORMAL";
  }
}

// ================= STATUS SUHU =================
String statusSuhu() {
  if (suhuGlobal == 0) {
    return "ERROR";
  } else if (suhuGlobal > batasSuhuTinggi) {
    return "DEMAM!";
  } else {
    return "NORMAL";
  }
}

// ================= STATUS GAS =================
String statusGas() {
  if (gasGlobal > batasGasBahayaLCD) {
    return "BAHAYA!";
  } else {
    return "AMAN";
  }
}

// ================= SERIAL MONITOR =================
void tampilSerial() {
  Serial.println("=== DATA SENSOR ===");

  Serial.print("Pulse ADC: ");
  Serial.print(pulseValue);
  Serial.print(" | BPM: ");
  Serial.println(bpm);

  Serial.print("Suhu: ");
  Serial.println(suhuGlobal);

  Serial.print("Gas: ");
  Serial.println(gasGlobal);

  Serial.println("===================");
}

// ================= LCD =================
void tampilLCD() {
  lcd.clear();

  if (lcdMode == 0) {
    lcd.setCursor(0, 0);
    lcd.print("BPM:");
    lcd.print((int)bpm);

    lcd.setCursor(0, 1);
    lcd.print("Status:");
    lcd.print(statusBPM());
  }

  else if (lcdMode == 1) {
    lcd.setCursor(0, 0);
    lcd.print("Suhu:");

    if (suhuGlobal == 0) {
      lcd.print("ERROR");
    } else {
      lcd.print(suhuGlobal, 1);
      lcd.print("C");
    }

    lcd.setCursor(0, 1);
    lcd.print(statusSuhu());
  }

  else if (lcdMode == 2) {
    lcd.setCursor(0, 0);
    lcd.print("Gas:");
    lcd.print(gasGlobal);

    lcd.setCursor(0, 1);
    lcd.print(statusGas());
  }

  lcdMode++;

  if (lcdMode > 2) {
    lcdMode = 0;
  }
}

// ================= BUZZER =================
// ================= BUZZER ALARM SERAM =================
// ================= BUZZER DETAK JANTUNG =================
void kontrolBuzzer() {
  bool bahaya = false;

  if (bpm > batasBpmTinggi && bpm != 0) {
    bahaya = true;
  }

  if (suhuGlobal > batasSuhuTinggi) {
    bahaya = true;
  }

  if (gasGlobal > batasGasBahayaBuzzer) {
    bahaya = true;
  }

  if (bahaya) {
    unsigned long waktu = millis();

    // Pola detak jantung: LUB-DUB ... jeda
    // 0 - 100 ms    : LUB
    // 120 - 200 ms  : DUB
    // 200 - 800 ms  : diam
    int fase = waktu % 800;

    if (fase < 100) {
      tone(buzzerPin, 180);      // LUB, nada rendah
    } 
    else if (fase >= 120 && fase < 200) {
      tone(buzzerPin, 260);      // DUB, sedikit lebih tinggi
    } 
    else {
      noTone(buzzerPin);         // jeda
    }

  } else {
    noTone(buzzerPin);
  }
}
// ================= KIRIM DATA =================
void kirimData() {
  Serial.println("\n=== KIRIM KE SERVER ===");

  Serial.print("BPM yang akan dikirim: ");
  Serial.println(bpm);

  Serial.print("Suhu yang akan dikirim: ");
  Serial.println(suhuGlobal);

  Serial.print("Gas yang akan dikirim: ");
  Serial.println(gasGlobal);

  HTTPClient http;
  http.begin(serverName);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"kode_device\":\"ESP32_01\",";
  json += "\"heart_rate\":" + String((int)bpm) + ",";
  json += "\"temperature\":" + String(suhuGlobal, 2) + ",";
  json += "\"air_quality\":" + String(gasGlobal);
  json += "}";

  Serial.println("JSON:");
  Serial.println(json);

  int httpCode = http.POST(json);

  Serial.print("HTTP CODE: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    Serial.println(http.getString());
  } else {
    Serial.println("GAGAL KIRIM");
  }

  http.end();
}