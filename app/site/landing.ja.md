# RhinoTiger

![ロゴ。半分がサイ、半分がトラの顔、虹色の「犀」の下に「虎」、中ほどを RHINOTIGER が横切る](../manual/img/screen-logo.png "RhinoTiger（犀虎）")

RhinoTiger（犀虎）は、M-VAVE FM-1 のための、ダークサイからハイテックまでの自動生成サイケトランス・ファームウェアです。
音楽はひとりで鳴り、あなたは舵を取ります。4つのマクロノブ、ミュート付きのミックス、そして音楽に一発ものの音を投げ込む鍵盤で。
転がる FM ベース、パーカッション、くり返すライン、ザップやうがい音、その下に持続するアトモス。ひとりでに、あるいは呼べば来る
ブレイク、ビルド、ドロップ。鍵盤で弾く16のエフェクト。そして、遅く湿ったものから速く細かいものまで、8つの世界があります。

![再生中の RhinoTiger。COLD SIGNAL と 152、小節の5段のレーン、4つのマクロ](../manual/img/screen-playing.png "FM-1 の画面の RhinoTiger")

これはバージョン {{VERSION}} です。インストールは自己責任です。インストーラーで M-VAVE 純正のファームウェアに戻せます。

- [ブラウザで試す](../emu/)。音つきで、ここで鳴らせます。FM-1 と同じコードが動くので、FM-1 がなくてもかまいません。
- FM-1 を USB でつなぎ、Chrome か Edge から[インストール](../install/)します。
- [マニュアル](../manual/ja/)では、画面、ページ、鍵盤とエフェクト、セクション、世界を説明しています。
- コマンドラインのインストーラーを使うなら、[{{PKG}} をダウンロード](../firmware/{{PKG}})して `python3 tools/fm1_install.py {{PKG}}` を実行します。
- [ソース](https://github.com/hacha/rhinotiger)は GPL-3.0-only です。fm1-base（MPL-2.0）と felucca-bsp の上に作られていて、どちらも
  Hügelton Instruments の Leo Kuroshita による Felucca に由来します。また、Charles Vestal による fm1-base のテンプレートから始まりました。
  ロゴの文字は Noto Sans JP Black（SIL Open Font License 1.1）から描き起こしたものです。

M-VAVE と FM-1 はそれぞれの所有者の商標です。RhinoTiger は M-VAVE とは関係ありません。
