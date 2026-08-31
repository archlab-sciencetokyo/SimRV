# SimRV キャッシュ勉強会 - 実習用ワークロードバイナリ

このディレクトリには、**60分 SimRV キャッシュ技術勉強会**用のアセンブリソースおよびビルドスクリプトが含まれています。

## モジュールのロード

実習を開始する前に、SimRV シミュレータモジュールをロードしてください：

```bash
module load archlab/simrv
```

## ディレクトリ内容

- `01_spatial_locality.S` & `01_spatial_locality.bin`: 32バイトラインにおける1回の冷ミスと連続7回の空間ヒットを検証。
- `02_temporal_locality.S` & `02_temporal_locality.bin`: 配列ループ反復による100%の時間的ヒット（再利用）を検証。
- `03_conflict_thrashing.S` & `03_conflict_thrashing.bin`: 4-WayのSet 0にマッピングされる5つのアドレスによるLRUスラッシングを検証。
- `build.sh`: `riscv64-unknown-elf-gcc` を使用して `.S` ファイルを RV32 ベアメタル `.elf` および `.bin` に再コンパイルするスクリプト。

## バイナリのビルド方法

```bash
./build.sh
```

## インタラクティブ TUI モードでの実行（視覚的観察）

SimRV をサイクル精度・ベアメタルモードで起動します：

```bash
# 演習 1: 空間的局所性
simrv -m /mnt/archlab/study/cache/01_spatial_locality.bin -b -C

# 演習 2: 時間的局所性
simrv -m /mnt/archlab/study/cache/02_temporal_locality.bin -b -C

# 演習 3: コンフリクトミスとスラッシング
simrv -m /mnt/archlab/study/cache/03_conflict_thrashing.bin -b -C
```

### TUI キャッシュインスペクタ操作:
- `r`: 左ペインを **Cache** インスペクタ画面に切り替え。
- `s` または `Space`: 1命令ずつステップ実行。
- 画面上で **HIT** (緑), **MISS** (赤), **REPLACED** (橙), **LRU** カウンタ, 各セット/ウェイの有効ビットを観察。

## ヘッドレス / CLI モードでの実行

```bash
simrv -m /mnt/archlab/study/cache/01_spatial_locality.bin -b -c -C -s 50
simrv -m /mnt/archlab/study/cache/02_temporal_locality.bin -b -c -C -s 100
simrv -m /mnt/archlab/study/cache/03_conflict_thrashing.bin -b -c -C -s 100
```
