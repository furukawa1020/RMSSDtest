#include <M5Unified.h>
#include <NimBLEDevice.h>
#include <driver/gpio.h>

// ----- �ݒ� (Port C) -----
const int PIN_PUMP_1 = 16;
const int PIN_PUMP_2 = 17;

// �p�����[�^�ݒ�
const unsigned long BLOW_UP_TIME_MS = 180000;
const int RMSSD_WINDOW_SIZE = 30;
const float PUMP_MULTIPLIER = 50.0;           // ���x�{�� ����ɋ��� (10�{ -> 50�{)
const unsigned long MIN_PUMP_TIME_MS = 100;   
const unsigned long MAX_PUMP_TIME_MS = 8000;  // �ő�8�b

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

void triggerPump(bool inflate, float seconds, float currentRelaxVal) {
    if (seconds <= 0) return;
    
    unsigned long duration = (unsigned long)(seconds * 1000);
    if (duration < MIN_PUMP_TIME_MS) duration = MIN_PUMP_TIME_MS;
    if (duration > MAX_PUMP_TIME_MS) duration = MAX_PUMP_TIME_MS;
    
    // RED=Stress(Deflate), BLUE=Relax(Inflate)
    M5.Display.fillScreen(inflate ? BLUE : RED);
    M5.Display.setCursor(10, 50);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(WHITE);
    // ��ʕ\�����킩��₷���ύX
    M5.Display.println(inflate ? "RELAXED!\n(INFLATE)" : "STRESSED!\n(DEFLATE)");
    M5.Display.printf("Relax: %.0f%%\n", currentRelaxVal);
    M5.Display.printf("%.1f sec", duration / 1000.0);

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

    if (flags & 0x10) {
        for (int i = offset; i < length; i += 2) {
            if (i + 1 >= length) break;
            
            uint16_t rawRR = pData[i] | (pData[i+1] << 8);
            float rrMs = (rawRR / 1024.0) * 1000.0;
            
            rrIntervals.push_back(rrMs);
            if (rrIntervals.size() > RMSSD_WINDOW_SIZE) {
                rrIntervals.erase(rrIntervals.begin());
            }

            if (rrIntervals.size() >= 2) {
                float currentRmssd = calculateRmssd();
                unsigned long elapsed = millis() - startTime;
                
                if (elapsed < BLOW_UP_TIME_MS) {
                    // Continuous inflation during baseline (User request)
                    pumpInflate();

                    baselineSamples.push_back(currentRmssd);
                    
                    // Ensure isPumping is false so loop() doesn't auto-stop it
                    // (But we want to update display)
                    if (!isPumping) {
                        M5.Display.fillScreen(BLACK);
                        M5.Display.setCursor(0, 0);
                        M5.Display.setTextColor(WHITE);
                        M5.Display.setTextSize(2);
                        M5.Display.println("INITIAL INFLATION");
                        M5.Display.printf("(%ds remaining)\n", (BLOW_UP_TIME_MS - elapsed)/1000);
                        M5.Display.printf("HR: %d bpm\n", hrValue);
                        M5.Display.printf("RMSSD: %.1f\n", currentRmssd);
                    }
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
                        
                        M5.Display.fillScreen(GREEN);
                        M5.Display.setCursor(0,0);
                        M5.Display.setTextSize(3);
                        M5.Display.println("START CONTROL!");
                        delay(2000);
                        return;
                    }

                    float currentRelaxationValue = (currentRmssd / baselineRmssd) * 100.0;
                    float error = (currentRelaxationValue - prevRelaxationValue) / prevRelaxationValue;
                    
                    //  �������d�v 
                    // �����b�N�X (error > 0) -> INFLATE (����)
                    // �X�g���X (error < 0)   -> DEFLATE (�r�C)
                    bool actionInflate = (error > 0); 
                    
                    // �{���������Ď��Ԃ��Z�o
                    float durationSeconds = abs(error) * PUMP_MULTIPLIER;
                    
                    if (!isPumping && durationSeconds > 0.05) { 
                         triggerPump(actionInflate, durationSeconds, currentRelaxationValue);
                    }

                    if (!isPumping) {
                        M5.Display.fillScreen(BLACK);
                        M5.Display.setCursor(0, 0);
                        M5.Display.setTextColor(WHITE);
                        M5.Display.setTextSize(2);
                        M5.Display.println("FEEDBACK ACTIVE");
                        M5.Display.printf("HR: %d bpm\n", hrValue);
                        M5.Display.printf("RMSSD: %.1f\n", currentRmssd);
                        M5.Display.printf("Base: %.1f\n", baselineRmssd);
                        M5.Display.printf("Relax: %.0f%%\n", currentRelaxationValue);
                    }
                    prevRelaxationValue = currentRelaxationValue;
                }
            }
        }
    }
}

class MyClientCallback : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pclient) {
        connected = true;
        M5.Display.fillScreen(GREEN);
        M5.Display.setCursor(0,0);
        M5.Display.println("Connected!");
        delay(1000);
        startTime = millis(); 
    };
    void onDisconnect(NimBLEClient* pclient) {
        connected = false;
        M5.Display.fillScreen(RED);
        M5.Display.println("Disconnected...");
    }
};

class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        M5.Display.print("."); 
        if (advertisedDevice->haveServiceUUID() && advertisedDevice->isAdvertisingService(serviceUUID)) {
            M5.Display.println("\nFound Polar!");
            NimBLEDevice::getScan()->stop();
            myDevice = advertisedDevice;
            doConnect = true;
            doScan = true;
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
    M5.Display.println("Scanning Polar H10...");
    
    NimBLEDevice::init("");
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pScan->setActiveScan(true);
    pScan->start(10, false);
}

void loop() {
    M5.update();
    
    // Manual Button Control
    if (M5.BtnA.isPressed()) {
        pumpInflate();
        pumpEndTime = millis() + 200; // Keep pumping while held
        isPumping = true;
    }
    else if (M5.BtnC.isPressed()) {
        pumpDeflate();
        pumpEndTime = millis() + 200; // Keep pumping while held
        isPumping = true;
    }

    if (isPumping && millis() > pumpEndTime) {
        pumpStop();
        // Clear screen or update status after manual stop? 
        // Or just let next notify update it.
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
