#include <M5Unified.h>

// ----- 設定 -----
// ポンプ制御ピン (M5Stack Core Port A - Red)
// Groveケーブル: 黄色=G21, 白色=G22 (I2CピンをGPIOとして使用)
// 注意: M5Stack Basic/Gray/M5GO/Fire では Port A は I2C (SCL=22, SDA=21) です
// GPIO設定をして出力モードに切り替えます
const int PIN_YELLOW = 21; 
const int PIN_WHITE  = 22; 

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);
    M5.Display.setTextSize(3);
    
    // GPIO設定
    // Port AはデフォルトでI2Cプルアップされている場合がありますが
    // OUTPUT設定で強制的にH/L駆動します
    pinMode(PIN_YELLOW, OUTPUT);
    
    pinMode(PIN_WHITE, OUTPUT);
    
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.println("M5GO PORT A");\n    M5.Display.println("PUMP TEST");
    delay(2000);
}

void loop() {
    // パターン1: 黄色(21) ON
    M5.Display.fillScreen(RED);
    M5.Display.setCursor(10, 50);
    M5.Display.println("PIN 21 (YEL)");
    M5.Display.println("ON");
    
    digitalWrite(PIN_YELLOW, HIGH);
    digitalWrite(PIN_WHITE, LOW);
    
    delay(3000); // 3秒

    // 停止
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(10, 50);
    M5.Display.println("STOP");
    
    digitalWrite(PIN_YELLOW, LOW);
    digitalWrite(PIN_WHITE, LOW);
    
    delay(2000);

    // パターン2: 白色(22) ON
    M5.Display.fillScreen(BLUE);
    M5.Display.setCursor(10, 50);
    M5.Display.println("PIN 22 (WHT)");
    M5.Display.println("ON");

    digitalWrite(PIN_YELLOW, LOW);
    digitalWrite(PIN_WHITE, HIGH);
    
    delay(3000); // 3秒

    // 停止
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(10, 50);
    M5.Display.println("STOP");
    
    digitalWrite(PIN_YELLOW, LOW);
    digitalWrite(PIN_WHITE, LOW);
    
    delay(2000);
}
