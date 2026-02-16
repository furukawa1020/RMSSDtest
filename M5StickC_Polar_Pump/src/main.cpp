#include <M5Unified.h>

// ----- 緊急用：全ピン駆動ポンプテスト -----
// M5Stackの世代によってポートAのピンが違うため、全部動かします。
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
    M5.Display.println("ALL PIN TEST");
    M5.Display.println("21,22,32,33");
    delay(2000);
}

void loop() {
    // パターン1: 黄色ライン (G21 & G32) ON
    M5.Display.fillScreen(RED);
    M5.Display.setCursor(10, 50);
    M5.Display.println("YELLOW ON");
    
    digitalWrite(PIN_A1_OLD, HIGH); // 21
    digitalWrite(PIN_A1_NEW, HIGH); // 32
    
    digitalWrite(PIN_A2_OLD, LOW);
    digitalWrite(PIN_A2_NEW, LOW);
    
    delay(3000); // 3秒

    // 停止
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(10, 50);
    M5.Display.println("STOP");
    
    digitalWrite(PIN_A1_OLD, LOW);
    digitalWrite(PIN_A1_NEW, LOW);
    digitalWrite(PIN_A2_OLD, LOW);
    digitalWrite(PIN_A2_NEW, LOW);
    
    delay(2000);

    // パターン2: 白色ライン (G22 & G33) ON
    M5.Display.fillScreen(BLUE);
    M5.Display.setCursor(10, 50);
    M5.Display.println("WHITE ON");

    digitalWrite(PIN_A2_OLD, HIGH); // 22
    digitalWrite(PIN_A2_NEW, HIGH); // 33

    digitalWrite(PIN_A1_OLD, LOW);
    digitalWrite(PIN_A1_NEW, LOW);
    
    delay(3000); // 3秒

    // 停止
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(10, 50);
    M5.Display.println("STOP");
    
    digitalWrite(PIN_A1_OLD, LOW);
    digitalWrite(PIN_A1_NEW, LOW);
    digitalWrite(PIN_A2_OLD, LOW);
    digitalWrite(PIN_A2_NEW, LOW);
    
    delay(2000);
}
