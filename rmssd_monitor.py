"""
Polar H10心拍モニターからリアルタイムでRMSSDとRelaxation Valueを計算
"""
import asyncio
import numpy as np
from bleak import BleakClient, BleakScanner
from collections import deque
from datetime import datetime

# Polar H10のUUID
HEART_RATE_SERVICE_UUID = "0000180d-0000-1000-8000-00805f9b34fb"
HEART_RATE_MEASUREMENT_UUID = "00002a37-0000-1000-8000-00805f9b34fb"

class RMSSDCalculator:
    def __init__(self, window_size=30):
        """
        RMSSD計算器
        
        Parameters:
        -----------
        window_size : int
            RR間隔を保持する数（デフォルト30）
        """
        self.rr_intervals = deque(maxlen=window_size)
        self.baseline_rmssd = None
        self.baseline_samples = []
        self.baseline_count = 60  # ベースライン計算用のサンプル数
        
    def add_rr_interval(self, rr_interval):
        """RR間隔を追加"""
        self.rr_intervals.append(rr_interval)
        
        # ベースライン計算中
        if len(self.baseline_samples) < self.baseline_count:
            if len(self.rr_intervals) >= 2:
                rmssd = self.calculate_rmssd()
                if rmssd is not None:
                    self.baseline_samples.append(rmssd)
                    if len(self.baseline_samples) == self.baseline_count:
                        self.baseline_rmssd = np.mean(self.baseline_samples)
                        print(f"\n✓ ベースラインRMSSD確立: {self.baseline_rmssd:.2f} ms")
                        print("=" * 60)
        
    def calculate_rmssd(self):
        """
        RMSSDを計算
        RMSSD = sqrt(1/(N-1) * Σ(RR_{i+1} - RR_i)²)
        """
        if len(self.rr_intervals) < 2:
            return None
        
        rr_array = np.array(self.rr_intervals)
        diff = np.diff(rr_array)
        squared_diff = diff ** 2
        mean_squared_diff = np.sum(squared_diff) / (len(rr_array) - 1)
        rmssd = np.sqrt(mean_squared_diff)
        
        return rmssd
    
    def calculate_relaxation_value(self, current_rmssd):
        """
        Relaxation Valueを計算
        Relaxation Value = (RMSSD / RMSSD_baseline) × 100
        """
        if self.baseline_rmssd is None or self.baseline_rmssd == 0:
            return None
        
        relaxation_value = (current_rmssd / self.baseline_rmssd) * 100
        return relaxation_value
    
    def is_baseline_ready(self):
        """ベースラインが確立されたかチェック"""
        return self.baseline_rmssd is not None


class PolarH10Monitor:
    def __init__(self):
        self.calculator = RMSSDCalculator(window_size=30)
        self.device_address = None
        self.heart_rate = 0
        
    async def find_polar_device(self):
        """Polar H10デバイスを検索"""
        print("Polar H10を検索中...")
        devices = await BleakScanner.discover(timeout=10.0)
        
        for device in devices:
            if device.name and "Polar H10" in device.name:
                print(f"✓ Polar H10を発見: {device.name} ({device.address})")
                return device.address
        
        return None
    
    def parse_heart_rate_data(self, sender, data):
        """心拍データをパース"""
        byte_data = bytes(data)
        flags = byte_data[0]
        
        # 心拍数を取得
        if flags & 0x01:  # 16ビット
            self.heart_rate = int.from_bytes(byte_data[1:3], byteorder='little')
            offset = 3
        else:  # 8ビット
            self.heart_rate = byte_data[1]
            offset = 2
        
        # RR間隔を取得（複数含まれる可能性あり）
        if flags & 0x10:  # RR間隔が含まれている
            rr_intervals = []
            i = offset
            while i < len(byte_data) - 1:
                rr_value = int.from_bytes(byte_data[i:i+2], byteorder='little')
                rr_ms = rr_value * 1024 / 1000  # 1/1024秒単位からミリ秒に変換
                rr_intervals.append(rr_ms)
                i += 2
            
            # 各RR間隔を処理
            for rr in rr_intervals:
                self.calculator.add_rr_interval(rr)
                self.display_results(rr)
    
    def display_results(self, latest_rr):
        """結果を表示"""
        rmssd = self.calculator.calculate_rmssd()
        
        if rmssd is None:
            return
        
        timestamp = datetime.now().strftime("%H:%M:%S")
        
        # ベースライン確立前
        if not self.calculator.is_baseline_ready():
            progress = len(self.calculator.baseline_samples)
            total = self.calculator.baseline_count
            print(f"[{timestamp}] HR: {self.heart_rate:3d} bpm | "
                  f"RR: {latest_rr:6.1f} ms | "
                  f"RMSSD: {rmssd:6.2f} ms | "
                  f"ベースライン: {progress}/{total}")
        else:
            # Relaxation Value計算
            relaxation_value = self.calculator.calculate_relaxation_value(rmssd)
            
            # リラックス度の判定
            if relaxation_value >= 120:
                status = "🟢 非常にリラックス"
            elif relaxation_value >= 100:
                status = "🟡 リラックス"
            elif relaxation_value >= 80:
                status = "🟠 普通"
            else:
                status = "🔴 緊張"
            
            print(f"[{timestamp}] HR: {self.heart_rate:3d} bpm | "
                  f"RR: {latest_rr:6.1f} ms | "
                  f"RMSSD: {rmssd:6.2f} ms | "
                  f"Relaxation Value: {relaxation_value:6.2f}% | {status}")
    
    async def start_monitoring(self):
        """モニタリング開始"""
        # デバイス検索
        self.device_address = await self.find_polar_device()
        
        if not self.device_address:
            print("❌ Polar H10が見つかりませんでした")
            print("デバイスの電源を入れ、近くにあることを確認してください")
            return
        
        # BLE接続
        print(f"\n接続中: {self.device_address}")
        
        try:
            async with BleakClient(self.device_address) as client:
                print(f"✓ 接続成功!")
                print("\n" + "=" * 60)
                print("心拍データ取得開始")
                print("=" * 60)
                print(f"ベースラインRMSSD計算中... (約{self.calculator.baseline_count}サンプル)")
                print("-" * 60)
                
                # 心拍データ通知を開始
                await client.start_notify(
                    HEART_RATE_MEASUREMENT_UUID,
                    self.parse_heart_rate_data
                )
                
                # 実行を継続（Ctrl+Cで停止）
                try:
                    while True:
                        await asyncio.sleep(1)
                except KeyboardInterrupt:
                    print("\n\nモニタリング停止")
                    await client.stop_notify(HEART_RATE_MEASUREMENT_UUID)
        except Exception as e:
            print(f"\n❌ エラーが発生しました: {e}")
            print("再試行するにはプログラムを再実行してください。")


async def main():
    """メイン関数"""
    print("=" * 60)
    print("Polar H10 RMSSD & Relaxation Value モニター")
    print("=" * 60)
    print("\n【使い方】")
    print("1. Polar H10を装着し、電源を入れてください")
    print("2. プログラムが自動的にデバイスを検索します")
    print("3. 接続後、リアルタイムでRMSSDとRelaxation Valueが表示されます")
    print("4. 停止するには Ctrl+C を押してください\n")
    
    monitor = PolarH10Monitor()
    await monitor.start_monitoring()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n終了しました")
