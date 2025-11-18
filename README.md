# Polar H10 RMSSD & Relaxation Value モニター

Polar H10心拍センサーからリアルタイムで心拍データを取得し、RMSSDとRelaxation Valueを計算・表示するプログラムです。

## 機能

- **リアルタイム心拍データ取得**: Polar H10からBluetooth経由でRR間隔を取得
- **RMSSD計算**: RR間隔の変動からRMSSD（Root Mean Square of Successive Differences）を計算
- **Relaxation Value**: ベースラインRMSSDとの比較でリラックス度を数値化

## 計算式

### RMSSD
```
RMSSD = √(1/(N-1) × Σ(RR_{i+1} - RR_i)²)
```

### Relaxation Value
```
Relaxation Value = (RMSSD / RMSSD_baseline) × 100
```

## セットアップ

1. **必要なパッケージをインストール**
```powershell
pip install -r requirements.txt
```

2. **Polar H10の準備**
   - Polar H10を胸部に装着
   - デバイスの電源を入れる（LEDが点滅することを確認）

3. **プログラムの実行**
```powershell
python rmssd_monitor.py
```

## 使い方

1. プログラムを起動すると、自動的にPolar H10を検索します
2. デバイスが見つかると接続し、データ取得を開始します
3. 最初の約60サンプルでベースラインRMSSDを計算します
4. ベースライン確立後、Relaxation Valueが表示されます
5. 停止するには `Ctrl+C` を押してください

## 出力例

```
[14:23:45] HR: 72 bpm | RR: 833.0 ms | RMSSD: 45.23 ms | Relaxation Value: 105.32% | 🟡 リラックス
```

## Relaxation Valueの解釈

- **120%以上**: 🟢 非常にリラックス
- **100-120%**: 🟡 リラックス
- **80-100%**: 🟠 普通
- **80%未満**: 🔴 緊張

## トラブルシューティング

### デバイスが見つからない場合
- Polar H10の電源が入っているか確認
- Bluetoothが有効になっているか確認
- デバイスが近くにあるか確認
- 他のアプリでPolar H10に接続していないか確認

### 接続が切れる場合
- デバイスのバッテリー残量を確認
- 距離を近づける
- 電波干渉を避ける

## 必要環境

- Python 3.7以上
- Windows 10/11（Bluetooth Low Energy対応）
- Polar H10心拍センサー

## ライセンス

MIT License
