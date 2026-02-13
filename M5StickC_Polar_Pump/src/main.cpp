#include <M5StickCPlus.h>
#include <NimBLEDevice.h>

// ----- 設定 -----
// ポンプ制御ピン (TC1508A)
const int PIN_IN1 = 26; // G26
const int PIN_IN2 = 25; // G25

// タイマー設定
const unsigned long BLOW_UP_TIME_MS = 180000; // 最初の3分間 (180秒) : 膨張 & ベースライン計測
const int RMSSD_WINDOW_SIZE = 30;             // RMSSD計算用の窓幅
const float PUMP_MULTIPLIER = 5.0;            // エラー値にかける倍率 (例: エラー0.1 * 5.0 = 0.5秒稼働)
const unsigned long MIN_PUMP_TIME_MS = 100;   // 最小ポンプ稼働時間
const unsigned long MAX_PUMP_TIME_MS = 5000;  // 最大ポンプ稼働時間

// Polar UUIDs
static BLEUUID serviceUUID("180d");
static BLEUUID charUUID("2a37");

// グローバル変数
bool doConnect = false;
bool connected = false;
bool doScan = false;
NimBLEAdvertisedDevice* myDevice;
NimBLEClient* pClient = nullptr;

// データ処理用
std::vector<float> rrIntervals;      // RR間隔の履歴 (ms)
std::vector<float> baselineSamples;  // ベースライン計算用サンプル
float baselineRmssd = 0.0;
float prevRelaxationValue = 0.0;     // 前回のRelaxation Value
unsigned long startTime = 0;
bool isBaselineEstablished = false;

// ポンプ制御用状態変数
enum PumpState {
    STATE_STOP,
    STATE_INFLATE,
    STATE_DEFLATE
};
PumpState currentPumpState = STATE_STOP;

void pumpStop() {
    if (currentPumpState == STATE_STOP) return; // 既に停止なら何もしない
    currentPumpState = STATE_STOP;

    digitalWrite(PIN_IN1, LOW);
    digitalWrite(PIN_IN2, LOW);
    M5.Lcd.fillRect(0, 140, 135, 20, BLACK);
    M5.Lcd.setCursor(0, 140);
    M5.Lcd.print("Pump: STOP");
}

void pumpInflate() {
    if (currentPumpState == STATE_INFLATE) return; // 既にINFLATEなら何もしない
    currentPumpState = STATE_INFLATE;

    // TC1508A: IN1=H, IN2=L -> Forward
    digitalWrite(PIN_IN1, HIGH);
    digitalWrite(PIN_IN2, LOW);
    M5.Lcd.fillRect(0, 140, 135, 20, RED);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setCursor(0, 140);
    M5.Lcd.print("Pump: INFLATE");
}

void pumpDeflate() {
    if (currentPumpState == STATE_DEFLATE) return; // 既にDEFLATEなら何もしない
    currentPumpState = STATE_DEFLATE;

    // TC1508A: IN1=L, IN2=H -> Reverse
    digitalWrite(PIN_IN1, LOW);
    digitalWrite(PIN_IN2, HIGH);
    M5.Lcd.fillRect(0, 140, 135, 20, BLUE);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setCursor(0, 140);
    M5.Lcd.print("Pump: DEFLATE");
}

// 非同期ポンプ制御のための変数
unsigned long pumpEndTime = 0;
bool isPumping = false;

void updatePumpState() {
    if (isPumping && millis() > pumpEndTime) {
        pumpStop();
        isPumping = false;
    }
}

void triggerPump(bool inflate, float seconds) {
    if (seconds <= 0) return;
    unsigned long duration = (unsigned long)(seconds * 1000);
    
    // 安全リミット
    if (duration < MIN_PUMP_TIME_MS) duration = MIN_PUMP_TIME_MS;
    if (duration > MAX_PUMP_TIME_MS) duration = MAX_PUMP_TIME_MS;

    Serial.printf("Pump Trigger: %s for %lu ms\n", inflate ? "Inflate" : "Deflate", duration);
    
    if (inflate) {
        pumpInflate();
    } else {
        pumpDeflate();
    }
    
    pumpEndTime = millis() + duration;
    isPumping = true;
}

// RMSSD計算
float calculateRmssd() {
    if (rrIntervals.size() < 2) return 0.0;
    
    float sumSquaredDiff = 0.0;
    for (size_t i = 1; i < rrIntervals.size(); i++) {
        float diff = rrIntervals[i] - rrIntervals[i-1];
        sumSquaredDiff += diff * diff;
    }
    
    return sqrt(sumSquaredDiff / (rrIntervals.size() - 1));
}

// データ受信コールバック
void notifyCallback(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    if (length < 2) return;

    uint8_t flags = pData[0];
    int hrValue = 0;
    int offset = 1;

    // 心拍数フォーマット (8bit or 16bit)
    if (flags & 0x01) {
        hrValue = pData[1] | (pData[2] << 8);
        offset = 3;
    } else {
        hrValue = pData[1];
        offset = 2;
    }

    // RR間隔の取得
    if (flags & 0x10) {
        for (int i = offset; i < length; i += 2) {
            if (i + 1 >= length) break;
            
            uint16_t rawRR = pData[i] | (pData[i+1] << 8);
            float rrMs = (rawRR / 1024.0) * 1000.0;
            
            rrIntervals.push_back(rrMs);
            if (rrIntervals.size() > RMSSD_WINDOW_SIZE) {
                rrIntervals.erase(rrIntervals.begin());
            }

            // 新しいRR間隔が得られたので計算を行う
            float currentRmssd = calculateRmssd();
            
            // 経過時間のチェック
            unsigned long elapsedTime = millis() - startTime;
            
            // --- フェーズ1: 最初の3分間 (ベースライン計測 & 初期膨張) ---
            if (elapsedTime < BLOW_UP_TIME_MS) {
                // ベースラインデータの蓄積
                if (currentRmssd > 0) {
                    baselineSamples.push_back(currentRmssd);
                }
                
                // 画面更新
                M5.Lcd.setCursor(0, 40);
                M5.Lcd.printf("Phase: INITIAL\nTime: %lu/%lu s\nHR: %d bpm\nRMSSD: %.1f", 
                    elapsedTime/1000, BLOW_UP_TIME_MS/1000, hrValue, currentRmssd);
                
                // ポンプは膨張し続ける (メインループまたはここで制御)
                // 注: 別途メインループで管理
            } 
            // --- フェーズ2: フィードバック制御 ---
            else {
                if (!isBaselineEstablished && !baselineSamples.empty()) {
                    // ベースライン確定
                    float sum = 0;
                    for(float v : baselineSamples) sum += v;
                    baselineRmssd = sum / baselineSamples.size();
                    isBaselineEstablished = true;
                    prevRelaxationValue = 100.0; // 初期値
                    pumpStop(); // 初期膨張終了
                }

                if (isBaselineEstablished && currentRmssd > 0) {
                    // Relaxation Value (RV) 計算
                    float currentRelaxationValue = (currentRmssd / baselineRmssd) * 100.0;
                    
                    // 相対誤差の計算: (今回 - 前回) / 前回
                    // これが "プラスマイナス" を決定する
                    float error = 0.0;
                    if (prevRelaxationValue != 0) {
                        error = (currentRelaxationValue - prevRelaxationValue) / prevRelaxationValue;
                    }
                    
                    // アクション決定
                    // error > 0 : リラックス値上昇 (リラックス傾向) -> どうする？
                    // error < 0 : リラックス値下降 (緊張傾向)     -> どうする？
                    // ここでは例として:
                    // positive (上昇) -> 膨張 (Inflate) ? 
                    // negative (下降) -> 収縮 (Deflate) ?
                    // ※ユーザー指示: "誤差の前後でのプラスマイナスで膨張収縮を決めて"
                    
                    bool actionInflate = (error > 0); 
                    float durationSeconds = abs(error) * PUMP_MULTIPLIER;
                    
                    // ポンプを作動 (すでに動いていなければ)
                    if (!isPumping && durationSeconds > 0.05) { // ノイズ無視
                         triggerPump(actionInflate, durationSeconds);
                    }

                    // 画面更新
                    M5.Lcd.fillScreen(BLACK);
                    M5.Lcd.setCursor(0, 0);
                    M5.Lcd.println("Polar H10 Monitor");
                    M5.Lcd.printf("Phase: FEEDBACK\nHR: %d\nRV: %.1f%%\nBase: %.1f\n", hrValue, currentRelaxationValue, baselineRmssd);
                    M5.Lcd.printf("Err: %.3f\nAct: %s", error, actionInflate ? "INF" : "DEF");

                    // 値の更新
                    prevRelaxationValue = currentRelaxationValue;
                }
            }
        }
    }
}

// BLEクライアントコールバック
class MyClientCallback : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pclient) {
        connected = true;
        M5.Lcd.println("Connected!");
        startTime = millis(); // 接続した時点をスタートとする
    };
    void onDisconnect(NimBLEClient* pclient) {
        connected = false;
        M5.Lcd.println("Disconnected");
    }
};

// アドバタイズ発見コールバック
class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        if (advertisedDevice->haveServiceUUID() && advertisedDevice->isAdvertisingService(serviceUUID)) {
            M5.Lcd.println("Found Polar H10");
            NimBLEDevice::getScan()->stop();
            myDevice = advertisedDevice;
            doConnect = true;
            doScan = true;
        }
    }
};

void setup() {
    M5.begin();
    M5.Lcd.setRotation(3);
    M5.Lcd.setTextSize(2);
    
    // GPIO設定
    pinMode(PIN_IN1, OUTPUT);
    pinMode(PIN_IN2, OUTPUT);
    
    // --- 起動テスト ---
    M5.Lcd.fillScreen(ORANGE);
    M5.Lcd.setCursor(0, 20);
    M5.Lcd.println("PUMP TEST...");
    M5.Lcd.println("INFLATE (1s)");
    pumpInflate();
    delay(1000);
    
    M5.Lcd.println("DEFLATE (1s)");
    pumpDeflate();
    delay(1000);
    
    pumpStop();
    M5.Lcd.println("TEST DONE");
    delay(1000);
    M5.Lcd.fillScreen(BLACK);
    // ----------------

    Serial.begin(115200);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("Starting BLE...");
    M5.Lcd.println("Scanning for");
    M5.Lcd.println("Polar H10...");

    NimBLEDevice::init("");
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pScan->setInterval(1349);
    pScan->setWindow(449);
    pScan->setActiveScan(true);
    pScan->start(5, false);
}

void loop() {
    // 接続処理
    if (doConnect) {
        if (pClient == nullptr) {
            pClient = NimBLEDevice::createClient();
            pClient->setClientCallbacks(new MyClientCallback());
        }
        
        if (pClient->connect(myDevice)) {
            NimBLERemoteService* pRemoteService = pClient->getService(serviceUUID);
            if (pRemoteService != nullptr) {
                NimBLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
                if (pRemoteCharacteristic != nullptr) {
                    pRemoteCharacteristic->registerForNotify(notifyCallback);
                }
            }
        }
        doConnect = false;
    }

    // フェーズ1の連続膨張制御
    if (connected) {
        unsigned long elapsed = millis() - startTime;
        if (elapsed < BLOW_UP_TIME_MS) {
            // 最初の3分間はずっと空気を入れる
            // 連続駆動しすぎないようにPWM制御などが望ましいが、指示通り「入れ続ける」
            pumpInflate();
        } else {
            // フェーズ2に入った瞬間、一旦止める処理はnotifyCallback内で行われる
            // ここではポンプのタイマー停止処理を呼ぶ
            updatePumpState();
        }
    } else {
        // 未接続時はスキャン再開など
        pumpStop();
        if(!doScan && pClient == nullptr) {
            NimBLEDevice::getScan()->start(5, false); 
        }
    }
    
    M5.update();
    delay(10);
}
