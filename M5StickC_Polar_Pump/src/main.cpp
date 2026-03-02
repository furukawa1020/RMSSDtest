#include <M5Unified.h>
#include <NimBLEDevice.h>
#include <driver/gpio.h>

// ----- M5Stack Core Port C (GPIO16/17) -----
const int PIN_PUMP_1 = 16;
const int PIN_PUMP_2 = 17;

// パラメータ設定
const unsigned long BLOW_UP_TIME_MS  = 60000;   // 60秒ベースライン
const int            RMSSD_WINDOW_SIZE = 30;
const float          PUMP_MULTIPLIER   = 100.0f; // error(%) -> 秒数変換
const unsigned long  MIN_PUMP_TIME_MS  = 100;
const unsigned long  MAX_PUMP_TIME_MS  = 8000;   // 最大8秒

// BLE UUIDs (Heart Rate Service – CooSpo / Polar H10 共通)
static BLEUUID serviceUUID("180d");
static BLEUUID charUUID("2a37");

// グローバル変数
bool                    doConnect = false;
bool                    connected = false;
NimBLEAdvertisedDevice* myDevice  = nullptr;
NimBLEClient*           pClient   = nullptr;

std::vector<float> rrIntervals;
std::vector<float> baselineSamples;
float         baselineRmssd         = 0.0f;
float         prevRelaxationValue   = 0.0f;
unsigned long startTime             = 0xFFFFFFFF; // onConnectで上書き
bool          isBaselineEstablished = false;
bool          isPumping             = false;
unsigned long pumpEndTime           = 0;

// ディスプレイ状態
int    g_hr         = 0;
float  g_rmssd      = 0.0f;
float  g_relax      = 0.0f;
String g_pumpStatus = "IDLE";
String g_phase      = "SCANNING";

void drawDisplay() {
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(2);
    M5.Display.printf("[%s]\n",        g_phase.c_str());
    M5.Display.printf("HR:   %d bpm\n", g_hr);
    M5.Display.printf("RMSSD:%.1f\n",   g_rmssd);
    M5.Display.printf("BASE: %.1f\n",   baselineRmssd);
    M5.Display.printf("RLX:  %.0f%%\n", g_relax);
    M5.Display.setTextSize(3);
    M5.Display.printf("%s\n", g_pumpStatus.c_str());
}

// ---------- ポンプ制御 ----------
void pumpStop() {
    digitalWrite(PIN_PUMP_1, LOW);
    digitalWrite(PIN_PUMP_2, LOW);
    isPumping    = false;
    g_pumpStatus = "IDLE";
    drawDisplay();
}

void pumpInflate() {
    digitalWrite(PIN_PUMP_1, HIGH);
    digitalWrite(PIN_PUMP_2, LOW);
}

void pumpDeflate() {
    digitalWrite(PIN_PUMP_1, LOW);
    digitalWrite(PIN_PUMP_2, HIGH);
}

// inflate=true  -> pumpInflate()  -> 物理的に「収縮」 (リラックス時)
// inflate=false -> pumpDeflate()  -> 物理的に「膨張」 (ストレス時)
void triggerPump(bool inflate, float seconds,
                 int hr, float rmssd, float relax) {
    if (seconds <= 0.0f) return;

    unsigned long duration = (unsigned long)(seconds * 1000);
    if (duration < MIN_PUMP_TIME_MS) duration = MIN_PUMP_TIME_MS;
    if (duration > MAX_PUMP_TIME_MS) duration = MAX_PUMP_TIME_MS;

    g_hr         = hr;
    g_rmssd      = rmssd;
    g_relax      = relax;
    // STRESS -> 膨張(DN),  RELAX -> 収縮(UP)
    g_pumpStatus = inflate ? "STRESS(DN)" : "RELAX(UP)";
    drawDisplay();

    if (inflate) pumpInflate();
    else         pumpDeflate();

    pumpEndTime = millis() + duration;
    isPumping   = true;
}

// ---------- RMSSD 計算 ----------
float calculateRmssd() {
    if (rrIntervals.size() < 2) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 1; i < rrIntervals.size(); i++) {
        float d = rrIntervals[i] - rrIntervals[i - 1];
        sum += d * d;
    }
    return sqrtf(sum / (rrIntervals.size() - 1));
}

// ---------- BLE 通知コールバック ----------
void notifyCallback(NimBLERemoteCharacteristic* pChar,
                    uint8_t* pData, size_t length, bool isNotify) {
    if (length < 2) return;

    uint8_t flags   = pData[0];
    int     hrValue = 0;
    int     offset  = 1;

    if (flags & 0x01) {
        hrValue = pData[1] | (pData[2] << 8);
        offset  = 3;
    } else {
        hrValue = pData[1];
        offset  = 2;
    }
    g_hr = hrValue;

    // ── RR 取得: パケット内 (Polar H10) ──
    bool gotRR = false;
    if (flags & 0x10) {
        for (int i = offset; i + 1 < (int)length; i += 2) {
            uint16_t raw  = pData[i] | (pData[i + 1] << 8);
            float    rrMs = (raw / 1024.0f) * 1000.0f;
            if (rrMs >= 300 && rrMs <= 2000) {
                rrIntervals.push_back(rrMs);
                if (rrIntervals.size() > RMSSD_WINDOW_SIZE)
                    rrIntervals.erase(rrIntervals.begin());
                gotRR = true;
            }
        }
    }

    // ── RR 取得: 通知間隔から推定 (CooSpo/HW706 フォールバック) ──
    if (!gotRR) {
        static unsigned long lastNotifyTime = 0;
        unsigned long now = millis();
        if (lastNotifyTime > 0) {
            unsigned long interval = now - lastNotifyTime;
            if (interval >= 300 && interval <= 2000) {
                rrIntervals.push_back((float)interval);
                if (rrIntervals.size() > RMSSD_WINDOW_SIZE)
                    rrIntervals.erase(rrIntervals.begin());
                gotRR = true;
            }
        }
        lastNotifyTime = now;
    }

    // RR データ不足
    if (rrIntervals.size() < 2) {
        g_phase      = "COLLECTING";
        g_pumpStatus = "WAIT RR";
        drawDisplay();
        return;
    }

    float         currentRmssd = calculateRmssd();
    g_rmssd                    = currentRmssd;
    unsigned long elapsed      = millis() - startTime;

    // ── ベースライン期間: 膨張し続ける ──
    if (elapsed < BLOW_UP_TIME_MS) {
        baselineSamples.push_back(currentRmssd);
        unsigned long remaining = (BLOW_UP_TIME_MS - elapsed) / 1000;
        g_phase      = "BASELINE";
        g_pumpStatus = "BL:" + String(remaining) + "s";
        g_relax      = 0.0f;
        drawDisplay();
        pumpInflate();
        // isPumping は false のまま → loop()のタイマーに邪魔されない
        // pumpInflate()はdigitalWrite(HIGH)なので通知毎に呼べば継続する
        return;
    }

    // ── ベースライン確定 ──
    if (!isBaselineEstablished) {
        pumpStop();
        float sum = 0.0f;
        for (float v : baselineSamples) sum += v;
        if (!baselineSamples.empty()) baselineRmssd = sum / baselineSamples.size();
        if (baselineRmssd == 0.0f) baselineRmssd = 1.0f;
        isBaselineEstablished = true;
        prevRelaxationValue   = (currentRmssd / baselineRmssd) * 100.0f;

        g_phase      = "FEEDBACK";
        g_pumpStatus = "READY";
        g_relax      = prevRelaxationValue;
        drawDisplay();
        delay(2000);
        return;
    }

    // ── フィードバック制御 ──
    float currentRelaxationValue = (currentRmssd / baselineRmssd) * 100.0f;
    float error = (currentRelaxationValue - prevRelaxationValue) / prevRelaxationValue;
    g_relax = currentRelaxationValue;
    g_phase = "FEEDBACK";

    // RELAX (error > 0, RMSSD 上昇) -> actionInflate=false -> pumpDeflate -> 収縮(UP)
    // STRESS (error < 0, RMSSD 低下) -> actionInflate=true  -> pumpInflate -> 膨張(DN)
    bool  actionInflate   = (error < 0);
    float durationSeconds = fabsf(error) * PUMP_MULTIPLIER;

    if (!isPumping && durationSeconds > 0.05f) {
        triggerPump(actionInflate, durationSeconds,
                    hrValue, currentRmssd, currentRelaxationValue);
    }

    if (!isPumping) {
        g_pumpStatus = "IDLE";
        drawDisplay();
    }

    prevRelaxationValue = currentRelaxationValue;
}

// ---------- BLE クライアントコールバック ----------
class MyClientCallback : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pclient) override {
        connected    = true;
        startTime    = millis();
        g_phase      = "BASELINE";
        g_pumpStatus = "CONNECTED";
        drawDisplay();
        delay(500);
    }
    void onDisconnect(NimBLEClient* pclient) override {
        connected    = false;
        g_phase      = "SCANNING";
        g_pumpStatus = "DISCONNECTED";
        drawDisplay();
    }
};

// ---------- BLE スキャンコールバック (CooSpo/Polar 両対応: HR UUID で検索) ----------
class MyAdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) override {
        if (!advertisedDevice->haveServiceUUID() ||
            !advertisedDevice->isAdvertisingService(serviceUUID)) return;

        String name = advertisedDevice->haveName()
                          ? advertisedDevice->getName().c_str()
                          : "Unknown";

        // CooSpo (HW706) は名前が "CooSpo" または "HW706" を含む
        // Polar H10 は "Polar" を含む
        // どちらも HR UUID で見つかったら接続する (名前表示のみ)
        g_phase      = "FOUND";
        g_pumpStatus = name;   // 画面に実デバイス名を表示
        drawDisplay();

        NimBLEDevice::getScan()->stop();
        myDevice  = advertisedDevice;
        doConnect = true;
    }
};

// ---------- setup ----------
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setTextSize(2);
    M5.Display.setBrightness(200);

    pinMode(PIN_PUMP_1, OUTPUT);
    pinMode(PIN_PUMP_2, OUTPUT);
    digitalWrite(PIN_PUMP_1, LOW);
    digitalWrite(PIN_PUMP_2, LOW);

    // 起動メッセージ
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextColor(WHITE);
    M5.Display.println("M5Stack Core");
    M5.Display.println("RMSSD Biofeedback");
    M5.Display.println("STRESS -> UP(inflate)");
    M5.Display.println("RELAX  -> DN(deflate)");
    delay(2000);

    // 初期充填 6 秒
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.println("PRE-FILLING 6s...");
    pumpInflate();
    delay(6000);
    pumpStop();

    g_phase      = "SCANNING";
    g_pumpStatus = "SEARCHING";
    drawDisplay();

    NimBLEDevice::init("");
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pScan->setActiveScan(true);
    pScan->start(10, false);
}

// ---------- loop ----------
void loop() {
    M5.update();

    // ── 手動ボタン操作 (M5Stack Core: 3ボタン) ──
    // BtnA (左ボタン): 押している間 inflate -> 物理的に収縮方向
    if (M5.BtnA.isPressed()) {
        pumpInflate();
        g_pumpStatus = "MANUAL DN";
        drawDisplay();
        pumpEndTime = millis() + 200;
        isPumping   = true;
    }
    // BtnC (右ボタン): 押している間 deflate -> 物理的に膨張方向
    else if (M5.BtnC.isPressed()) {
        pumpDeflate();
        g_pumpStatus = "MANUAL UP";
        drawDisplay();
        pumpEndTime = millis() + 200;
        isPumping   = true;
    }

    // タイマー切れでポンプ停止
    if (isPumping && millis() > pumpEndTime) {
        pumpStop();
    }

    // ── BLE 接続処理 ──
    if (doConnect) {
        if (pClient != nullptr) NimBLEDevice::deleteClient(pClient);
        pClient = NimBLEDevice::createClient();
        pClient->setClientCallbacks(new MyClientCallback());

        if (pClient->connect(myDevice)) {
            NimBLERemoteService* pSvc = pClient->getService(serviceUUID);
            if (pSvc) {
                NimBLERemoteCharacteristic* pChar = pSvc->getCharacteristic(charUUID);
                if (pChar && pChar->canNotify()) {
                    pChar->subscribe(true, notifyCallback);
                }
            }
        } else {
            g_pumpStatus = "CONN FAIL";
            drawDisplay();
            NimBLEDevice::getScan()->start(5, false);
        }
        doConnect = false;
    }

    // スキャン再開
    if (!connected && !doConnect && !NimBLEDevice::getScan()->isScanning()) {
        NimBLEDevice::getScan()->start(5, false);
    }
}