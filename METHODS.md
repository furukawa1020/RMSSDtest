# メソッド一覧

このファイルでは、プロジェクト内の各メソッド・関数の仕様を説明します。

---

## Python: `rmssd_monitor.py`

### クラス: `RMSSDCalculator`

RR間隔からRMSSDおよびRelaxation Valueを計算するクラスです。

#### `__init__(self, window_size=30)`

```python
RMSSDCalculator(window_size=30)
```

| パラメータ | 型 | デフォルト | 説明 |
|---|---|---|---|
| `window_size` | int | 30 | RR間隔を保持するウィンドウサイズ |

---

#### `add_rr_interval(self, rr_interval)`

RR間隔をキューに追加し、ベースラインRMSSDの計算を進めます。

| パラメータ | 型 | 説明 |
|---|---|---|
| `rr_interval` | float | 追加するRR間隔（ミリ秒） |

- ベースライン計算中（`baseline_count` サンプルに達するまで）は、RMSSDをサンプルとして蓄積します。
- サンプル数が `baseline_count` に達すると、`baseline_rmssd` を確定します。

---

#### `calculate_rmssd(self) -> float | None`

現在のウィンドウ内のRR間隔からRMSSDを計算します。

**戻り値**: RMSSDの値（ms）。RR間隔が2件未満の場合は `None`。

**計算式**:
```
RMSSD = √(1/(N-1) × Σ(RR_{i+1} - RR_i)²)
```

---

#### `calculate_relaxation_value(self, current_rmssd) -> float | None`

現在のRMSSDとベースラインRMSSDを比較してRelaxation Valueを計算します。

| パラメータ | 型 | 説明 |
|---|---|---|
| `current_rmssd` | float | 現在のRMSSD値（ms） |

**戻り値**: Relaxation Valueの値（%）。ベースラインが未確立または0の場合は `None`。

**計算式**:
```
Relaxation Value = (RMSSD / RMSSD_baseline) × 100
```

---

#### `is_baseline_ready(self) -> bool`

ベースラインRMSSDが確立されているかを確認します。

**戻り値**: `True`（確立済み）/ `False`（未確立）

---

### クラス: `PolarH10Monitor`

Polar H10からBLEでデータを取得し、RMSSD計算と結果表示を行うクラスです。

#### `__init__(self)`

`RMSSDCalculator(window_size=30)` を内部で生成し、デバイスアドレスと心拍数を初期化します。

---

#### `find_polar_device(self) -> str | None` *(async)*

BLEスキャンを実行し、"Polar H10" を名前に含むデバイスを検索します。

**戻り値**: 発見されたデバイスのBLEアドレス文字列。見つからない場合は `None`。

---

#### `parse_heart_rate_data(self, sender, data)`

BLE通知コールバック。受信バイト列から心拍数とRR間隔を解析し、`RMSSDCalculator` へ渡して結果を表示します。

| パラメータ | 型 | 説明 |
|---|---|---|
| `sender` | any | BLE送信元（未使用） |
| `data` | bytes-like | 受信した心拍データ |

- `flags & 0x01`：心拍数が16ビット形式かどうかを判定
- `flags & 0x10`：RR間隔データが含まれるかどうかを判定
- RR間隔の単位変換: `rr_value × 1024 / 1000`（1/1024秒 → ms）

---

#### `display_results(self, latest_rr)`

ターミナルに計測結果を表示します。

| パラメータ | 型 | 説明 |
|---|---|---|
| `latest_rr` | float | 直近のRR間隔（ms） |

- ベースライン確立前: HR・RR・RMSSD・進捗を表示
- ベースライン確立後: HR・RR・RMSSD・Relaxation Value・ステータスを表示

**Relaxation Valueのステータス判定**:

| 範囲 | ステータス |
|---|---|
| 120%以上 | 🟢 非常にリラックス |
| 100〜120% | 🟡 リラックス |
| 80〜100% | 🟠 普通 |
| 80%未満 | 🔴 緊張 |

---

#### `start_monitoring(self)` *(async)*

Polar H10を検索・接続し、BLE通知の受信を開始します。`Ctrl+C` で停止します。

---

## C++: `M5StickC_Polar_Pump/src/main.cpp`

M5Stack Core + NimBLE を使用したリアルタイムバイオフィードバック制御プログラムです。

---

### `drawDisplay()`

```cpp
void drawDisplay()
```

M5Stackのディスプレイを更新し、現在のフェーズ・心拍数・RMSSD・ベースライン・Relaxation Value・ポンプ状態を表示します。

---

### `pumpStop()`

```cpp
void pumpStop()
```

両ポンプピン（PIN_PUMP_1 / PIN_PUMP_2）をLOWにしてポンプを停止します。`isPumping` を `false` にリセットし、ディスプレイを更新します。

---

### `pumpInflate()`

```cpp
void pumpInflate()
```

PIN_PUMP_1 を HIGH、PIN_PUMP_2 を LOW に設定し、物理的な収縮方向（ストレス時の応答）を実行します。

---

### `pumpDeflate()`

```cpp
void pumpDeflate()
```

PIN_PUMP_1 を LOW、PIN_PUMP_2 を HIGH に設定し、物理的な膨張方向（リラックス時の応答）を実行します。

---

### `triggerPump(inflate, seconds, hr, rmssd, relax)`

```cpp
void triggerPump(bool inflate, float seconds, int hr, float rmssd, float relax)
```

| パラメータ | 型 | 説明 |
|---|---|---|
| `inflate` | bool | `true`: pumpInflate（ストレス）, `false`: pumpDeflate（リラックス） |
| `seconds` | float | ポンプ動作時間（秒） |
| `hr` | int | 現在の心拍数（表示用） |
| `rmssd` | float | 現在のRMSSD（表示用） |
| `relax` | float | 現在のRelaxation Value（表示用） |

- `seconds` が0以下の場合は何もしません。
- 動作時間は `MIN_PUMP_TIME_MS`〜`MAX_PUMP_TIME_MS` の範囲にクランプされます。

---

### `calculateRmssd()`

```cpp
float calculateRmssd()
```

グローバル変数 `rrIntervals` の内容を使ってRMSSDを計算します。

**戻り値**: RMSSDの値（ms）。データが2件未満の場合は `0.0f`。

**計算式**:
```
RMSSD = √(Σ(RR[i] - RR[i-1])² / (N-1))
```

---

### `notifyCallback(pChar, pData, length, isNotify)`

```cpp
void notifyCallback(NimBLERemoteCharacteristic* pChar,
                    uint8_t* pData, size_t length, bool isNotify)
```

BLE Heart Rate通知のコールバック関数です。

| パラメータ | 型 | 説明 |
|---|---|---|
| `pChar` | NimBLERemoteCharacteristic* | 通知元のBLEキャラクタリスティック |
| `pData` | uint8_t* | 受信データのポインタ |
| `length` | size_t | データ長 |
| `isNotify` | bool | 通知フラグ |

処理の流れ:
1. フラグから心拍数（8bit or 16bit）を解析
2. Polar H10の場合はパケット内RR間隔を取得
3. RR間隔がない場合は通知間隔から推定（CooSpo/HW706 フォールバック）
4. ベースライン期間中は `pumpInflate()` を継続
5. ベースライン確定後はフィードバック制御を実行（RMSSD変化率に応じてポンプ制御）

---

### コールバッククラス: `MyClientCallback`

#### `onConnect(pclient)`

BLE接続確立時に呼ばれます。`startTime` を記録し、フェーズを `BASELINE` に設定します。

#### `onDisconnect(pclient)`

BLE切断時に呼ばれます。フェーズを `SCANNING` に設定します。

---

### コールバッククラス: `MyAdvertisedDeviceCallbacks`

#### `onResult(advertisedDevice)`

BLEスキャン結果コールバック。Heart Rate Service UUID（`180d`）を持つデバイスが見つかった場合に接続フラグを立てます。Polar H10 / CooSpo / HW706 などに対応しています。

---

### `setup()`

```cpp
void setup()
```

M5Stack初期化、ピン設定、起動メッセージ表示、6秒間の初期充填、BLEスキャン開始を行います。

---

### `loop()`

```cpp
void loop()
```

メインループ処理です。

- **BtnA（左ボタン）**: 押している間 `pumpInflate()`（手動DOWN）
- **BtnC（右ボタン）**: 押している間 `pumpDeflate()`（手動UP）
- ポンプタイマー管理: `pumpEndTime` を超えたら `pumpStop()`
- BLE接続処理: `doConnect` フラグが立っている場合、Heart Rate ServiceのCharacteristicに通知を登録
- スキャン再開: 未接続かつスキャン停止中の場合に自動再スキャン
