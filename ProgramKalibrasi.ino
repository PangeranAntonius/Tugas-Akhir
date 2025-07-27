#include <Arduino.h>

// Pin Configuration
const int MQ135_PIN = 34;     // GPIO34 untuk ESP32

// Parameter Rangkaian Sesuai Laporan Anda
const float Vc = 5.0;            // Tegangan catu daya sensor adalah 5V
const float R_LOAD = 20000.0;      // Resistor beban 20k Ohm
const float RO_CLEAN_AIR_FACTOR = 3.6; // Faktor dari datasheet

void setup() {
  Serial.begin(115200);
  pinMode(MQ135_PIN, INPUT);
  
  Serial.println("======================================");
  Serial.println("Program Kalibrasi Sensor MQ-135");
  Serial.println("======================================");
  Serial.println("Pastikan sensor berada di udara bersih dan stabil.");
  Serial.println("Proses akan dimulai dalam 30 detik untuk pemanasan sensor...");

  // Memberi waktu sensor untuk stabil (pemanasan)
  delay(30000); 
  
  Serial.print("Mengambil sampel");
  
  float sumRs = 0;
  const int numReadings = 50; // Mengambil 50 sampel untuk hasil yang stabil

  for(int i = 0; i < numReadings; i++) {
    int analogValue = analogRead(MQ135_PIN);
    // Konversi nilai analog (0-4095) ke tegangan (0-3.3V) yang dibaca pin ESP32
    float vrl = analogValue * (3.3 / 4095.0); 
    
    // Hitung Rs menggunakan RUMUS YANG SESUAI DENGAN LAPORAN
    float Rs = ((Vc / vrl) - 1) * R_LOAD;
    
    sumRs += Rs;
    delay(1000); // Jeda 1 detik antar sampel
    Serial.print(".");
  }
  
  // Hitung nilai R0
  float averageRs = sumRs / numReadings;
  float calibratedR0 = averageRs / RO_CLEAN_AIR_FACTOR;
  
  Serial.println("\n\nKalibrasi selesai!");
  Serial.print("Rata-rata Rs terukur: ");
  Serial.println(averageRs);
  Serial.println("--------------------------------------");
  Serial.print("NILAI R0 FINAL: ");
  Serial.println(calibratedR0);
  Serial.println("--------------------------------------");
  Serial.println("Masukkan nilai R0 ini ke dalam program utama Anda.");
}

void loop() {
  // Program berhenti setelah kalibrasi selesai.
  // Tidak ada yang perlu dijalankan di loop.
  delay(10000); 
}