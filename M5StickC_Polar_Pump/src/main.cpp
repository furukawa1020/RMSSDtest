#include <M5Unified.h>

// ----- 緊急用：全ピン駆動ポンプテスト (HIGH/HIGH) -----
// Basic/Gray/M5GO/Fire: 21, 22
// Core2/CoreS3: 32, 33

const int PIN_A1_OLD = 21; // Yellow (Old)
const int PIN_A2_OLD = 22; // White (Old)
const int PIN_A1_NEW = 32; // Yellow (New)
const int PIN_A2_NEW = 33; // White (New)

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);
    M5.Display.setTextSize(3);
    
    // 全ての可能性のあるピンを出力設定
    pinMode(PIN_A1_OLD, OUTPUT);
    pinMode(PIN_A2_OLD, OUTPUT);
    pinMode(PIN_A1_NEW, OUTPUT);
    pinMode(PIN_A2_NEW, OUTPUT);
    
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.println("HIGH/HIGH TEST");
    delay(2000);
}

void loop() {
    // ユーザーリクエスト: 両方HIGH
    M5.Display.fillScreen(MAGENTA);
    M5.Display.setCursor(0, 0);
    M5.Display.println("BOTH HIGH");
    M5.Display.println("21=H, 22=H");
    M5.Display.println("32=H, 33=H");
    
    // 全ピンHIGH
    digitalWrite(PIN_A1_OLD, HIGH); 
    digitalWrite(PIN_A1_NEW, HIGH); 
    digitalWrite(PIN_A2_OLD, HIGH);
    digitalWrite(PIN_A2_NEW, HIGH);
    
    delay(3000); // 3秒

    // 停止 (LOW/LOW)
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.println("ALL LOW");
    
    digitalWrite(PIN_A1_OLD, LOW);
    digitalWrite(PIN_A1_NEW, LOW);
    digitalWrite(PIN_A2_OLD, LOW);
    digitalWrite(PIN_A2_NEW, LOW);
    
    delay(2000);
}
