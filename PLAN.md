# YMM4 NVENC SDK 直接利用プラグイン 計画（更新）

## 前提
- NVEncC は使用しない（NVENC SDK を直接利用）
- GUI 必須項目: コーデック（h264/h265）、ビットレート
- 旧バージョン対応は行わない
- 速度最優先。GPUパスで完結させる

## 進め方（完全GPUパス / Media Foundation 不使用）
1. IVideoFileWriter2 へ切り替え
   - WriteVideo(ID2D1Bitmap1) を使用
   - CPUへフレームを降ろさない
2. GPU入力パイプの設計
   - ID2D1Bitmap1 を D3D11 テクスチャとして扱う
   - 必要なら GPU 上で NV12 へ変換
3. NVENC SDK 直叩き実装
   - D3D11 テクスチャを NVENC に入力
   - エンコード結果の bitstream を取得
4. 自前 MP4 mux 実装（MF 不使用）
   - AnnexB → length-prefixed に変換
   - avcC / hvcC を組み立てて moov に書き込む
   - mdat を逐次書き込み、終了時に moov を追記
   - 動画のみ（音声なし）で最小構成
5. 互換性確認
   - H.264 / H.265 の mp4 再生確認
   - 主要プレイヤー（Windows標準 / VLC）で確認
6. 速度最適化
   - バッファリング / 非同期処理
   - GPU使用率と速度の確認

## 仕様メモ（移行）
- NVEncC 同梱運用は廃止
- NVEncC/FFmpeg DLL 依存は廃止
- ライセンス表記は NVENC SDK / NVIDIA の条件に従う
