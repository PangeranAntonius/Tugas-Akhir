#include <Fuzzy.h>
#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <HardwareSerial.h>

// Pin sensor dan komunikasi
const int MQ135_PIN = 34; // GPIO34 untuk ESP32
#define RXD2 16
#define TXD2 17

// LCD I2C
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Buzzer
const int BUZZER_PIN = 18;   // Buzzer di GPIO18 (Digital)

// SIM800L Serial
HardwareSerial SIM800(1); // UART1

// Konfigurasi GPRS
const String APN  = "indosatgprs";
const String USER = "";
const String PASS = "";

// Konfigurasi Firebase
const String FIREBASE_HOST  = "https://polusiudara-ae662-default-rtdb.firebaseio.com/";
const String FIREBASE_SECRET  = "DzkWPXWilBFl5neykE6NGzRYaOIRy3T3McwMO0bT";

#define USE_SSL true
#define DELAY_MS 500

// Parameter CO
const float a_CO = 605.18;    // Parameter untuk CO
const float b_CO = -3.937;    // Parameter untuk CO
const int Rload = 20000;      // Resistor beban 20kΩ
const float CLEAN_AIR_RATIO = 10.0; // Faktor udara bersih untuk CO

// Kalibrasi Parameters
float R0_CleanAir = 342.061;   

// Fuzzy system initialization
Fuzzy* fuzzySystem = new Fuzzy();

// --- Deklarasi fungsi ---
float convertToRange(int analogValue);
void setupFuzzySystem();
String getAirQualityStatus(float index);
void init_gsm();
void gprs_connect();
boolean gprs_disconnect();
boolean is_gprs_connected();
void update_firebase(String path, String data); 
boolean waitResponse(String expected_answer = "OK", unsigned int timeout = 2000);
void check_voltage();
void triggerBuzzer();


// Konversi nilai analog ke range PPM
float convertToRange(int analogValue) {
  return map(analogValue, 0, 4095, 0, 200);
}

// Function to control buzzer (3 beeps for "BURUK" status)
void triggerBuzzer() {
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 1000);
    delay(500);
    noTone(BUZZER_PIN);
    delay(300);
  }
}

// Fuzzy system setup
void setupFuzzySystem(){
  // INPUT: Range ppm CO (0-200)
  FuzzyInput* pollutionInput = new FuzzyInput(1);

  
  // Himpunan fuzzy yang sudah ada
  FuzzySet* lowPollution    = new FuzzySet(0, 30, 30, 50);    // Trapesium Kiri untuk BAIK
  FuzzySet* mediumPollution = new FuzzySet(50, 75, 75, 100);   // Segitiga untuk SEDANG
  FuzzySet* highPollution   = new FuzzySet(100, 115, 200, 200);  // Trapesium Kanan untuk BURUK

  pollutionInput->addFuzzySet(lowPollution);
  pollutionInput->addFuzzySet(mediumPollution);
  pollutionInput->addFuzzySet(highPollution);
  fuzzySystem->addFuzzyInput(pollutionInput);

  
  // OUTPUT
  FuzzyOutput* airQuality = new FuzzyOutput(1);
  
  // Himpunan fuzzy output yang sudah ada
  FuzzySet* good     = new FuzzySet(0, 30, 30, 50);
  FuzzySet* moderate = new FuzzySet(50, 75, 75, 100);
  FuzzySet* poor     = new FuzzySet(100, 115, 200, 200);

  airQuality->addFuzzySet(good);
  airQuality->addFuzzySet(moderate);
  airQuality->addFuzzySet(poor);
  fuzzySystem->addFuzzyOutput(airQuality);


  // Fuzzy rules (Aturan yang sudah ada)
  FuzzyRuleAntecedent* ifLow = new FuzzyRuleAntecedent();
  ifLow->joinSingle(lowPollution);
  FuzzyRuleConsequent* thenGood = new FuzzyRuleConsequent();
  thenGood->addOutput(good);
  FuzzyRule* rule1 = new FuzzyRule(1, ifLow, thenGood);
  fuzzySystem->addFuzzyRule(rule1);

  FuzzyRuleAntecedent* ifMedium = new FuzzyRuleAntecedent();
  ifMedium->joinSingle(mediumPollution);
  FuzzyRuleConsequent* thenModerate = new FuzzyRuleConsequent();
  thenModerate->addOutput(moderate);
  FuzzyRule* rule2 = new FuzzyRule(2, ifMedium, thenModerate);
  fuzzySystem->addFuzzyRule(rule2);

  FuzzyRuleAntecedent* ifHigh = new FuzzyRuleAntecedent();
  ifHigh->joinSingle(highPollution);
  FuzzyRuleConsequent* thenPoor = new FuzzyRuleConsequent();
  thenPoor->addOutput(poor);
  FuzzyRule* rule3 = new FuzzyRule(3, ifHigh, thenPoor);
  fuzzySystem->addFuzzyRule(rule3);

}

// Fungsi status (TANPA PERUBAHAN)
String getAirQualityStatus(float crispValue) {
  if (crispValue <= 50) return "BAIK";
  else if (crispValue <= 100) return "SEDANG";
  else return "BURUK";
}


void setup() {
  Serial.begin(115200);
  SIM800.begin(9600, SERIAL_8N1, RXD2, TXD2);
  
  lcd.begin();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Inisialisasi...");
  
  pinMode(MQ135_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  setupFuzzySystem();

  lcd.setCursor(0, 1);
  Serial.println("Initializing...");
  init_gsm();
  check_voltage();
  
  lcd.clear();
  lcd.print("Connecting...");
  gprs_connect();

  if (is_gprs_connected()) {
    Serial.println("connected, siap kirim data.");
    lcd.clear();
    lcd.print("Connected!");
    delay(2000);
  } else {
    Serial.println("Belum terkoneksi, periksa sinyal.");
    lcd.clear();
    lcd.print("Failed!");
    delay(2000);
  }
}


// --- FUNGSI LOOP ---
void loop() {
  // Langkah 1: Baca PPM aktual dari sensor menggunakan rumus
  float actual_ppm = readCOppm();
  String status; // Variabel untuk menyimpan status akhir

  // Langkah 2: Terapkan logika untuk validasi dan klasifikasi
  if (actual_ppm < 0) {
    status = "TIDAK VALID";
  } 
  else if (actual_ppm > 200) {
    status = "S. BURUK"; // Langsung set status tanpa fuzzy
  } 
  else {
    // Proses dengan Fuzzy Logic hanya untuk rentang 0-200
    fuzzySystem->setInput(1, actual_ppm);
    fuzzySystem->fuzzify();
    float airQualityIndex = fuzzySystem->defuzzify(1);
    status = getAirQualityStatus(airQualityIndex);
  }
  
  // Menampilkan hasil ke Serial Monitor
  Serial.print("CO (ppm): "); Serial.print(actual_ppm);
  Serial.print(" | Status: "); Serial.println(status);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("PPM: ");
  lcd.print((int)ppmValue);
  
  lcd.setCursor(0, 1);
  lcd.print("Status: ");
  lcd.print(status);

  if (status == "BURUK") {
    triggerBuzzer();
  }
  
  // --- Bagian Pengiriman Data ---
  if (is_gprs_connected()) {
    // 1. Buat string JSON yang hanya berisi data intinya
    String jsonData = "{";
    jsonData += "\"PPM\": " + String((int)ppmValue) + ",";
    jsonData += "\"Status\": \"" + status + "\"";
    jsonData += "}";

    Serial.println("Updating data: " + jsonData);
    // 2. Panggil fungsi update dengan path "MQ135"
    update_firebase("MQ135", jsonData);
    
  } else {
    Serial.println("⚠ GPRS tidak terkoneksi. Coba reconnect...");
    lcd.clear();
    lcd.print("GPRS Error!");
    lcd.setCursor(0, 1);
    lcd.print("Reconnecting...");
    gprs_connect();
  }
  
  delay(5000); // Jeda 5 detik sebelum pengiriman berikutnya
}

void update_firebase(String path, String data) {
  Serial.println("🔄 Updating Firebase path: " + path);
  
  SIM800.println("AT+HTTPTERM");
  waitResponse();
  delay(DELAY_MS);

  SIM800.println("AT+HTTPINIT");
  waitResponse();
  delay(DELAY_MS);

  if (USE_SSL) {
    SIM800.println("AT+HTTPSSL=1");
    waitResponse();
    delay(DELAY_MS);
  }

  SIM800.println("AT+HTTPPARA=\"CID\",1");
  waitResponse();
  delay(DELAY_MS);

  SIM800.println("AT+HTTPPARA=\"URL\",\"" + FIREBASE_HOST + path + ".json?auth=" + FIREBASE_SECRET + "\"");
  waitResponse();
  delay(DELAY_MS);

  SIM800.println("AT+HTTPPARA=\"CONTENT\",\"application/json\"");
  waitResponse();
  delay(DELAY_MS);

  SIM800.println("AT+HTTPDATA=" + String(data.length()) + ",10000");
  if (!waitResponse("DOWNLOAD")) {
    Serial.println("❌ Gagal masuk ke mode DOWNLOAD.");
    return;
  }
  SIM800.println(data);
  waitResponse();
  delay(DELAY_MS);

  // Memperbarui data di lokasi yang ada, bukan membuat yang baru.
  SIM800.println("AT+HTTPACTION=2"); 
  
  unsigned long startTime = millis();
  String response = "";
  while (millis() - startTime < 20000) {
    if (SIM800.available()) {
      char c = SIM800.read();
      response += c;
      if (response.indexOf("+HTTPACTION:") >= 0) {
        Serial.println(response);
        break;
      }
    }
  }

  if (response.indexOf(",200,") >= 0) {
    Serial.println("Data berhasil di-update ke Firebase.");
  } else {
    Serial.println("Gagal update data. Response: " + response);
  }

  SIM800.println("AT+HTTPTERM");
  waitResponse("OK", 1000);
  delay(DELAY_MS);
}

void init_gsm() {
  SIM800.println("AT");
  waitResponse();
  delay(DELAY_MS);

  SIM800.println("AT+CPIN?");
  waitResponse("+CPIN: READY");
  delay(DELAY_MS);

  SIM800.println("AT+CFUN=1");
  waitResponse();
  delay(DELAY_MS);
}

void gprs_connect() {
  Serial.println("Menghubungkan...");
  SIM800.println("AT+SAPBR=0,1");
  waitResponse("OK", 10000);
  delay(DELAY_MS);

  SIM800.println("AT+SAPBR=3,1,\"Contype\",\"GPRS\"");
  waitResponse();
  delay(DELAY_MS);

  SIM800.println("AT+SAPBR=3,1,\"APN\",\"" + APN + "\"");
  waitResponse();
  delay(DELAY_MS);

  SIM800.println("AT+SAPBR=1,1");
  if (waitResponse("OK", 30000)) {
    Serial.println("GPRS connected.");
  } else {
    Serial.println("Gagal konek GPRS.");
  }
  delay(DELAY_MS);

  SIM800.println("AT+SAPBR=2,1");
  waitResponse("OK");
  delay(DELAY_MS);
}

boolean gprs_disconnect() {
  SIM800.println("AT+CGATT=0");
  return waitResponse("OK", 10000);
}

boolean is_gprs_connected() {
  SIM800.println("AT+CGATT?");
  return (waitResponse("+CGATT: 1", 5000) == 1);
}

boolean waitResponse(String expected_answer, unsigned int timeout) {
  uint8_t answer = 0;
  String response = "";
  unsigned long startTime = millis();

  while (SIM800.available() > 0) SIM800.read(); // flush buffer

  do {
    if (SIM800.available() != 0) {
      char c = SIM800.read();
      response += c;
      if (response.indexOf(expected_answer) >= 0) {
        answer = 1;
        break;
      }
    }
  } while ((millis() - startTime) < timeout);

  Serial.println(response);
  return answer;
}

void check_voltage() {
  SIM800.println("AT+CBC");
  unsigned long start = millis();
  String response = "";

  while (millis() - start < 2000) {
    if (SIM800.available()) {
      char c = SIM800.read();
      response += c;
    }
  }

  Serial.println("Voltage response:");
  Serial.println(response);

  int voltageIndex = response.indexOf(":");
  if (voltageIndex >= 0) {
    int firstComma = response.indexOf(",", voltageIndex);
    int secondComma = response.indexOf(",", firstComma + 1);
    if (firstComma > 0 && secondComma > 0) {
      String voltageStr = response.substring(secondComma + 1);
      int voltage = voltageStr.toInt();

      Serial.print("Tegangan SIM800L: ");
      Serial.print(voltage);
      Serial.println(" mV");
    }
  } else {
    Serial.println("Gagal membaca tegangan SIM800L.");
  }
}
