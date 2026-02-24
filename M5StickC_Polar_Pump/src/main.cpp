#include <M5Unified.h>
#include <NimBLEDevice.h>
#include <driver/gpio.h>

// ----- センサー選択 -----
// どちらか1つをコメントアウトしてください
// #define USE_POLAR_H10     // Polar H10を使用
#define USE_COOSPO     // CooSpoを使用

// ----- 設定 (ATOMS3 Grove G1/G2) -----
const int PIN_PUMP_1 = 1;
const int PIN_PUMP_2 = 2;

// �p�����[�^�ݒ�
const unsigned long BLOW_UP_TIME_MS = 180000;
const int RMSSD_WINDOW_SIZE = 30;
const float PUMP_MULTIPLIER = 100.0;           // 
const unsigned long MIN_PUMP_TIME_MS = 100;   
const unsigned long MAX_PUMP_TIME_MS = 8000;  // ＾

// Polar UUIDs
static BLEUUID serviceUUID("180d");
static BLEUUID charUUID("2a37");

// �O���[�o���ϐ�
bool doConnect = false;
bool connected = false;
bool doScan = false;
NimBLEAdvertisedDevice* myDevice;
NimBLEClient* pClient = nullptr;

std::vector<float> rrIntervals;
std::vector<float> baselineSamples;
float baselineRmssd = 0.0;
float prevRelaxationValue = 0.0;
unsigned long startTime = 0;
bool isBaselineEstablished = false;
bool isPumping = false;
unsigned long pumpEndTime = 0;
bool buttonWasLongPress = false;

// --- Display state globals ---
int g_hr = 0;
float g_rmssd = 0.0;
float g_relax = 0.0;
String g_pumpStatus = "IDLE";
bool g_isManual = false;
String g_phase = "SCANNING";

void drawDisplay() {
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(WHITE);
    M5.Display.printf("HR:%d bpm\n", g_hr);
    M5.Display.printf("RR:%.1f ms\n", g_rmssd);
    M5.Display.printf("Bs:%.1f\n", baselineRmssd);
    M5.Display.printf("Rx:%.0f%%\n", g_relax);
    if (g_pumpStatus == "INFLATE") M5.Display.setTextColor(CYAN);
    else if (g_pumpStatus == "DEFLATE") M5.Display.setTextColor(RED);
    else M5.Display.setTextColor(GREEN);
    if (g_isManual) M5.Display.print("[M]");
    M5.Display.println(g_pumpStatus);
    M5.Display.setTextColor(YELLOW);
    M5.Display.println(g_phase);
}

void pumpStop() {
    digitalWrite(PIN_PUMP_1, LOW);
    digitalWrite(PIN_PUMP_2, LOW);
    isPumping = false;
}

void pumpInflate() {
    digitalWrite(PIN_PUMP_1, HIGH);
    digitalWrite(PIN_PUMP_2, LOW);
}

void pumpDeflate() {
    digitalWrite(PIN_PUMP_1, LOW);
    digitalWrite(PIN_PUMP_2, HIGH);
}

void triggerPump(bool inflate, float seconds) {
    if (seconds <= 0) return;
    
    unsigned long duration = (unsigned long)(seconds * 1000);
    if (duration < MIN_PUMP_TIME_MS) duration = MIN_PUMP_TIME_MS;
    if (duration > MAX_PUMP_TIME_MS) duration = MAX_PUMP_TIME_MS;
    
    g_pumpStatus = inflate ? "INFLATE" : "DEFLATE";
    g_isManual = false;
    drawDisplay();

    if (inflate) pumpInflate();
    else pumpDeflate();
    
    pumpEndTime = millis() + duration;
    isPumping = true;
}

float calculateRmssd() {
    if (rrIntervals.size() < 2) return 0.0;
    float sumSquaredDiff = 0.0;
    for (size_t i = 1; i < rrIntervals.size(); i++) {
        float diff = rrIntervals[i] - rrIntervals[i-1];
        sumSquaredDiff += diff * diff;
    }
    return sqrt(sumSquaredDiff / (rrIntervals.size() - 1));
}

void notifyCallback(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    if (length < 2) return;

    uint8_t flags = pData[0];
    int hrValue = 0;
    int offset = 1;

    if (flags & 0x01) {
        hrValue = pData[1] | (pData[2] << 8);
        offset = 3;
    } else {
        hrValue = pData[1];
        offset = 2;
    }

    // Always update HR display (even without RR data)
    g_hr = hrValue;

    // Force-try to parse remaining bytes as RR intervals regardless of flags
    // (Some cheap sensors send RR without setting the flag bit correctly)
    bool gotRR = false;
    for (int i = offset; i + 1 < (int)length; i += 2) {
        uint16_t rawRR = pData[i] | (pData[i+1] << 8);
        float rrMs = (rawRR / 1024.0) * 1000.0;
        // Accept only physiologically plausible RR (200ms=300bpm ~ 2500ms=24bpm)
        if (rrMs < 200 || rrMs > 2500) continue;
        gotRR = true;
        rrIntervals.push_back(rrMs);
        if (rrIntervals.size() > RMSSD_WINDOW_SIZE) {
            rrIntervals.erase(rrIntervals.begin());
        }
    }

    if (!gotRR) {
        char buf[40];
        snprintf(buf, sizeof(buf), "NO RR f=0x%02X l=%d", flags, (int)length);
        g_phase = String(buf);
        g_rmssd = 0;
        drawDisplay();
        return;
    }

    if (rrIntervals.size() >= 2) {
                float currentRmssd = calculateRmssd();
                unsigned long elapsed = millis() - startTime;
                
                if (elapsed < BLOW_UP_TIME_MS) {
                    // Continuous inflation during baseline
                    pumpInflate();
                    baselineSamples.push_back(currentRmssd);
                    g_hr = hrValue;
                    g_rmssd = currentRmssd;
                    g_pumpStatus = "INFLATE";
                    g_isManual = false;
                    g_phase = "BL:" + String((BLOW_UP_TIME_MS - elapsed) / 1000) + "s";
                    drawDisplay();
                } 
                else {
                    if (!isBaselineEstablished) {
                        pumpStop(); // Stop the initial 3-min inflation

                        float sum = 0;
                        for(float v : baselineSamples) sum += v;
                        if (!baselineSamples.empty()) baselineRmssd = sum / baselineSamples.size();
                        if (baselineRmssd == 0) baselineRmssd = 1.0; 
                        isBaselineEstablished = true;
                        
                        prevRelaxationValue = (currentRmssd / baselineRmssd) * 100.0;

                        g_phase = "FEEDBACK";
                        g_pumpStatus = "IDLE";
                        g_isManual = false;
                        drawDisplay();
                        delay(300);
                        return;
                    }

                    float currentRelaxationValue = (currentRmssd / baselineRmssd) * 100.0;
                    float error = (currentRelaxationValue - prevRelaxationValue) / prevRelaxationValue;
                    
                    //  �������d�v 
                    // �����b�N�X (error > 0) -> INFLATE (����)
                    // �X�g���X (error < 0)   -> DEFLATE (�r�C)
                    bool actionInflate = (error < 0); 
                    
                    // �{���������Ď��Ԃ��Z�o
                    float durationSeconds = abs(error) * PUMP_MULTIPLIER;
                    
                    // Always update display globals with fresh sensor data
                    g_hr = hrValue;
                    g_rmssd = currentRmssd;
                    g_relax = currentRelaxationValue;
                    g_phase = "FEEDBACK";

                    if (!isPumping && durationSeconds > 0.05) {
                        triggerPump(actionInflate, durationSeconds);
                    } else {
                        if (!isPumping) {
                            g_pumpStatus = "IDLE";
                            g_isManual = false;
                        }
                        drawDisplay();
                    }
                    prevRelaxationValue = currentRelaxationValue;
                }
            }
    }
}

class MyClientCallback : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pclient) {
        connected = true;
        g_phase = "CONNECTED";
        g_pumpStatus = "INFLATE";
        g_isManual = false;
        drawDisplay();
        delay(300);
        startTime = millis(); 
    };
    void onDisconnect(NimBLEClient* pclient) {
        connected = false;
        g_phase = "DISCONNECTED";
        g_pumpStatus = "IDLE";
        drawDisplay();
    }
};

class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        M5.Display.print("."); 
        if (advertisedDevice->haveServiceUUID() && advertisedDevice->isAdvertisingService(serviceUUID)) {
#ifdef USE_POLAR_H10
            // Polar H10を探す
            if (advertisedDevice->getName().find("Polar") != std::string::npos) {
                M5.Display.println("\nFound Polar H10!");
#elif defined(USE_COOSPO)
            // CooSpo/HW706: デバイス名に関係なくHRサービスがあれば接続
            {
                M5.Display.printf("\nFound: %s\n", advertisedDevice->getName().c_str());
#else
            // すべてのHRセンサーに接続
            {
                M5.Display.println("\nFound HR Sensor!");
#endif
                M5.Display.printf("Device: %s\n", advertisedDevice->getName().c_str());
                NimBLEDevice::getScan()->stop();
                myDevice = advertisedDevice;
                doConnect = true;
                doScan = true;
            }
        }
    }
};

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    
    gpio_reset_pin((gpio_num_t)PIN_PUMP_1);
    gpio_reset_pin((gpio_num_t)PIN_PUMP_2);
    
    M5.Display.setRotation(1);
    M5.Display.setTextSize(2);
    M5.Display.setBrightness(200);

    pinMode(PIN_PUMP_1, OUTPUT);
    pinMode(PIN_PUMP_2, OUTPUT);
    digitalWrite(PIN_PUMP_1, LOW);
    digitalWrite(PIN_PUMP_2, LOW);

    M5.Display.println("FIRMWARE RESTORED");
    M5.Display.println("Logic: Relax->Inflate");
    delay(1000);

    M5.Display.fillScreen(RED);
    M5.Display.setCursor(0,0);
    M5.Display.println("PRE-FILLING...");
    M5.Display.println("Inflating 6s...");
    
    pumpInflate();
    delay(6000); 
    pumpStop();
    
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
#ifdef USE_POLAR_H10
    M5.Display.println("Scanning Polar H10...");
#elif defined(USE_COOSPO)
    M5.Display.println("Scanning HR(CooSpo/HW706)...");
#else
    M5.Display.println("Scanning HR Sensor...");
#endif
    
    NimBLEDevice::init("");
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pScan->setActiveScan(true);
    pScan->start(10, false);
}

void loop() {
    M5.update();
    
    // Manual Button Control (ATOMS3: touch screen button)
    // Long press (>5s): Deflate, Short press: Inflate
    
    // Check long press FIRST before wasReleased
    if (M5.BtnA.pressedFor(5000) && !buttonWasLongPress) {
        buttonWasLongPress = true;
        pumpDeflate();
        pumpEndTime = millis() + 3000; // 3 seconds
        isPumping = true;
        
        g_pumpStatus = "DEFLATE";
        g_isManual = true;
        drawDisplay();
    }
    
    // Only trigger inflate on release if it was NOT a long press
    if (M5.BtnA.wasReleased() && !buttonWasLongPress) {
        // Short press - inflate
        pumpInflate();
        pumpEndTime = millis() + 3000; // 3 seconds
        isPumping = true;
        g_pumpStatus = "INFLATE";
        g_isManual = true;
        drawDisplay();
    }
    
    // Reset flag after release
    if (M5.BtnA.wasReleased()) {
        buttonWasLongPress = false;
    }

    if (isPumping && millis() > pumpEndTime) {
        pumpStop();
        g_pumpStatus = "IDLE";
        g_isManual = false;
        drawDisplay();
    }

    if (doConnect) {
        if (pClient != nullptr) NimBLEDevice::deleteClient(pClient);
        pClient = NimBLEDevice::createClient();
        pClient->setClientCallbacks(new MyClientCallback());
        
        if (pClient->connect(myDevice)) {
            NimBLERemoteService* pRemoteService = pClient->getService(serviceUUID);
            if (pRemoteService) {
                NimBLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
                if (pRemoteCharacteristic && pRemoteCharacteristic->canNotify()) {
                    pRemoteCharacteristic->subscribe(true, notifyCallback);
                }
            }
        } else {
            M5.Display.println("Connect Failed");
            NimBLEDevice::getScan()->start(5, false);
        }
        doConnect = false;
    }
    
    if (!connected && !doConnect && !NimBLEDevice::getScan()->isScanning()) {
         NimBLEDevice::getScan()->start(5, false);
    }
}
