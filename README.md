# NVENC プラグイン出力

YMM4でNVENC動画出力をするためのプラグインです。H.264 / H.265 のMP4を書き出します。
GPUを使用することでエンコードを高速化します。

## インストール
1. [リリースページ](https://github.com/tarutaru247/YMM4_NVEncPlugin/releases/new)もしくは[Booth](https://tarutaru247.booth.pm/)から、NVEncPlugin.ymmeをダウンロード
2. ダウンロードしたファイルを開くと、自動的にYMM4のプラグインインストーラーが起動します
3. インストーラーの指示に従いインストール

## 使い方
1. YMM4の出力形式から「NVENC プラグイン出力」を選択
2. コーデック、出力品質、ビットレート方式を設定
3. 出力形式は`.mp4`

## 必要環境
- Windows 11
- NVIDIA GPU と最新ドライバ

```AMD GPUやIntel GPUでは動作しません```

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
