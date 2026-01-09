# NVENC プラグイン出力

YMM4向けのNVENC対応動画出力プラグインです。H.264 / H.265 のMP4を書き出します。

## 使い方
1. YMM4の出力形式から「NVENC プラグイン出力」を選択
2. コーデック、出力品質、ビットレート方式を設定
3. 出力形式は `.mp4`

## 必要環境
- Windows
- NVIDIA GPU と最新ドライバ

## 音声
- AAC固定（Media FoundationのAACエンコーダを使用）

## 配布用パッケージ
プラグインフォルダをzipで圧縮し、拡張子を`.ymme`に変更するとワンクリックインストールが可能です。

## ライセンス
MIT License

## 第三者ライセンス/依存関係
- NVIDIA NVENC SDK を使用します（SDK 自体は同梱しません。入手と利用は NVIDIA の EULA に従ってください）
- YukkuriMovieMaker v4 本体は同梱しません（本体の配布・利用は YMM4 の規約に従ってください）

## 作者
たるたる @tarutaru247
