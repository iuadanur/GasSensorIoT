#include "arduino_secrets.h"
// WiFi ve Cihaz Kimlik Bilgilerinizi içerir
/*
 * ESP32 + MQ-4 + RGB LED + Buzzer + DHT11 Sensör + Arduino IoT Cloud
 * -----------------------------------------------------------------
 * AO (MQ-4) → SENSOR_PIN (GPIO 36) — analog giriş
 * RGB LED: R-LED_R_PIN, G-LED_G_PIN, B-LED_B_PIN
 * Buzzer → BUZZER_PIN (GPIO 4) (NPN + piezo)
 * DHT11 Sensör Data → DHTPIN (GPIO 5)
 */
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "thingProperties.h" // Arduino IoT Cloud tarafından oluşturuldu ve bulut değişkenlerini tanımlar
#include <MQUnifiedsensor.h>
#include <DHT.h>         

/* --------- Kullanıcı ayarları --------- */
#define BUZZER_PIN   4
#define LED_R_PIN    25
#define LED_G_PIN    26
#define LED_B_PIN    27

#define LED_ON  HIGH      // Ortak katot için HIGH, ortak anot için LOW
#define LED_OFF (!LED_ON)

#define SENSOR_PIN   36     // MQ-4 Analog Çıkış Pini (VP / GPIO36)
#define R0_FIXED     18.42 
#define ALARM_PPM    300 
#define WARMUP_MS    10000 

/* --------- DHT Sensör Ayarları --------- */
#define DHTPIN 5          
#define DHTTYPE DHT11     
                         

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define I2C_ADDRESS 0x3C
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
/* --------- MQ-4 Sensör Ayarları --------- */
MQUnifiedsensor MQ4("ESP32", 3.3, 12, SENSOR_PIN, "MQ-4");

/* --------- DHT Sensör Nesnesi --------- */
DHT dht(DHTPIN, DHTTYPE);

// Fiziksel LED'i ayarlamak için fonksiyon prototipi
void setLED_physical(bool r, bool g, bool b);
// Bulut LED'ini güncellemek için fonksiyon prototipi
void updateCloudLED(bool r, bool g, bool b, bool on);

void setup() {
  Serial.begin(115200);
  delay(1500); 

  Serial.println(F("Çevre Gözlem Sistemi (DHT11) - Arduino IoT Cloud Başlatılıyor..."));

  /* GPIO pin yönleri */
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN); 
  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);

  /* DHT Sensörü Başlatma */
  dht.begin();
  Serial.println(F("► DHT11 Sensörü başlatıldı (Pin: GPIO5)."));

  // OLED Başlat
  if (!display.begin(I2C_ADDRESS, true)) {
    Serial.println(F("SH1106 OLED başlatılamadı!"));
    while (1);
  }
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  // thingProperties.h'daki initProperties() bulut değişkenlerini başlatır.
  initProperties();

  ArduinoCloud.begin(ArduinoIoTPreferredConnection);

  setDebugMessageLevel(2); 
  ArduinoCloud.printDebugInfo();

  Serial.println(F("► Bulut bağlantı süreci başlatıldı. Bağlantı bekleniyor..."));

  /* MQ-4 Sensör Isınma ve ADC Ayarları */
  setLED_physical(0, 0, 1); 
  updateCloudLED(0, 0, 1, true); 

  analogSetPinAttenuation(SENSOR_PIN, ADC_11db); 

  MQ4.setRegressionMethod(0); 
  MQ4.setA(-0.318);  
  MQ4.setB(1.133);  
  
  MQ4.init();
  MQ4.setR0(R0_FIXED); 

  Serial.print(F("► MQ-4 R0 değeri ayarlandı: ")); Serial.println(R0_FIXED);
  Serial.println(F("► MQ-4 Sensör ısınıyor (yaklaşık 10 saniye)..."));
  
  unsigned long warmup_start_time = millis();
  while(millis() - warmup_start_time < WARMUP_MS) {
    ArduinoCloud.update(); 
    delay(100); 
  }

  setLED_physical(0, 1, 0); 
  updateCloudLED(0, 1, 0, true); 
  
  buzzerState = false; 

  Serial.println(F("► Tüm sensörler hazır ve Arduino IoT Cloud'a bağlı!"));
}

void loop() {
  ArduinoCloud.update(); 

  /* DHT Sensör Verilerini Oku */
  float h = dht.readHumidity();       
  float t = dht.readTemperature();    

  if (isnan(h) || isnan(t)) {
    Serial.println(F("DHT11 sensöründen veri okunamadı!"));
  } else {
    humidityLevel = h;        
    temperatureLevel = t;     
    Serial.print(F("Nem: ")); Serial.print(h); Serial.print(F(" %\t"));
    Serial.print(F("Sıcaklık: ")); Serial.print(t); Serial.println(F(" *C"));
  }

  /* MQ-4 Metan Sensör Verilerini Oku */
  MQ4.update(); 
  float current_ppm = MQ4.readSensor(); 
  methaneLevel = current_ppm; 

  Serial.print(F("METAN (CH4) = ")); 
  if (isinf(current_ppm) || isnan(current_ppm)) {
    Serial.println(F("** MQ-4 Okuma/Kalibrasyon Hatası **"));
    noTone(BUZZER_PIN); 
    if (buzzerState) { 
      buzzerState = false; 
    }
    setLED_physical(0, 0, 1);   
    updateCloudLED(0, 0, 1, true); 
  } else {
    Serial.printf("%.1f ppm\n", current_ppm);

    /* Alarm & LED & Buzzer durumu (Metan seviyesine göre) */
    if (current_ppm > ALARM_PPM) {
      tone(BUZZER_PIN, 2000, 500); 
      if (!buzzerState) { 
        buzzerState = true; 
      }
      setLED_physical(1, 0, 0);   
      updateCloudLED(1, 0, 0, true); 
    } else {
      noTone(BUZZER_PIN); 
      if (buzzerState) { 
        buzzerState = false; 
      }
      setLED_physical(0, 1, 0);   
      updateCloudLED(0, 1, 0, true); 
    }
  }
  // OLED GÜNCELLEME
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Temp: ");
  display.print(t);
  display.print(" C");

  display.setCursor(0, 15);
  display.print("Hum:  ");
  display.print(h);
  display.print(" %");

  display.setCursor(0, 30);
  display.print("CH4:  ");
  display.print(current_ppm);
  display.print(" ppm");

  if (current_ppm > ALARM_PPM) {
    display.setCursor(0, 50);
    display.setTextColor(SH110X_WHITE);
    display.print("!!! METAN ALARMI !!!");
  }

  display.display();
  delay(1000); 
}

/* Fiziksel RGB LED'i kontrol etmek için yardımcı fonksiyon */
void setLED_physical(bool r, bool g, bool b) {
  digitalWrite(LED_R_PIN, r ? LED_ON : LED_OFF);
  digitalWrite(LED_G_PIN, g ? LED_ON : LED_OFF);
  digitalWrite(LED_B_PIN, b ? LED_ON : LED_OFF);
}

/* CloudColoredLight (`rgbLed`) değişkenini HSB kullanarak güncellemek için yardımcı fonksiyon */
void updateCloudLED(bool r_on, bool g_on, bool b_on, bool switch_status) {
  if (switch_status) {
    if (!rgbLed.getSwitch()) { 
        rgbLed.setSwitch(true);
    }
    rgbLed.setBrightness(100); 

    if (r_on && !g_on && !b_on) { 
      rgbLed.setHue(0.0f);        
      rgbLed.setSaturation(100.0f); 
    } else if (!r_on && g_on && !b_on) { 
      rgbLed.setHue(120.0f);      
      rgbLed.setSaturation(100.0f); 
    } else if (!r_on && !g_on && b_on) { 
      rgbLed.setHue(240.0f);      
      rgbLed.setSaturation(100.0f); 
    }
  } else {
    if (rgbLed.getSwitch()) { 
        rgbLed.setSwitch(false);
    }
  }
}