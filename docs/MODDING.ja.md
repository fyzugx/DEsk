[English](MODDING.md) | **日本語**

# DEsk を変える

DEsk v0.1 にプラグインの API はありません。MOD はソースへの変更で、DEsk 本体と同じようにビルドしてインストールします。ここでは、その準備、守ってほしい決まり、よくある変更の手順を説明します。先に [ARCHITECTURE.ja.md](ARCHITECTURE.ja.md) でコードの地図を見ておいてください。

DEsk は MIT ライセンスです。配るときは `LICENSE` を同梱し、README に何を変えたかを書いてください。

## 1. 準備

```sh
meson setup build                  # ./build にデバッグ最適化のビルド
meson compile -C build
meson test -C build                # 訳の表を確かめる
```

ビルドに要るものは README の「必要なもの」にあります。Debian・Ubuntu・Arch なら `scripts/install_session.sh --bare` がそれだけを入れて一度ビルドします。

変更を見る方法は三つあり、下へ行くほど本物に近くなります。

1. **プレビュー**：コンポジタなしで PNG に描きます。

   ```sh
   ./build/desk-preview -o out.png -s 1440x900            # 机
   ./build/desk-preview -o out.png --shelf --lang de       # 本棚をドイツ語で
   ./build/desk-preview --help                             # 描けるもの一覧
   ```

2. **入れ子**：`scripts/desk-window` で、今のセッションの中に DEsk を窓として起動します（大きさは `DESK_SIZE=1600x900`）。本物の設定を共有するので、分けたいときは `XDG_CONFIG_HOME`・`XDG_DATA_HOME`・`XDG_STATE_HOME`・`XDG_CACHE_HOME` を専用のディレクトリにします。`DESK_ALLOW_MULTIPLE=1` で、別の DEsk が動いていても起動できます。X11 の中なら `WLR_X11_OUTPUTS=2` で画面が二つになります。
3. **インストール**：`scripts/install_session.sh` をもう一度実行すると入れ替わります。ログアウトしてログインし直します。

DEsk が動きながら言うことは `~/.local/state/desk/session.log`（入れ子では標準エラー）にあります。`DESK_PROFILE=1` でフレームの時間も出ます。

## 2. 決まり

- **C11**、タブ、今のままの括弧の書き方、`-Wall -Wextra` で警告なし。コメントは「なぜか」を言う文章で書きます。`wm.c` と `draw.c` の節をいくつか読んで、同じように書いてください。
- **ライブラリを増やさない**（よほどの理由がない限り）。必須は wlroots、wayland、xkbcommon、cairo、pixman、fontconfig、libdrm、PAM です。libsystemd、libpulse、libinput、EGL/GLES/gbm、Xwayland は任意で、任意のままにします（`#ifdef DESK_HAVE_POST`、`DESK_HAVE_GPU` など）。
- **コンポジタのスレッドで待たない**：ディスクの走査、プロセスの終わりを待つこと、ネットワークはそこでしません。スレッドを立てて結果を返します（ARCHITECTURE の §6）。
- **大きさはポイントで。** 画面に比例させず、`desk_px_per_pt(screen_w)` を掛けます。
- **言葉は英語で `TR()` を通し**、表に全言語の行を足します（§3）。
- **作る前に借りる。** すでにある自由なプログラムで足りるなら、パネルを書かずにインストーラに足して Shell Settings から開きます（§5）。
- **すべての段階、すべての言語で。** Ultra と 2D Fallback で、いちばん長い言語（たいていドイツ語）と CJK の言語で確かめます。

## 3. 言葉と言語

### 言葉を足す

1. コードには英語で書きます：`TR("Put it away")`。ほかの言語で二つの語に分かれる言葉には文脈を付けます：`TR("verb|Open")`。
2. `src/i18n.c` の `WORDS` に行を足します。列は `enum desk_lang` の順（en、ja、zh、ko、es、fr、de）。訳がまだ無いところは `NULL` にすると英語が出ます。

   ```c
   { "Put it away", "片付ける", "收起来", "치우기", "Guardar", "Ranger", "Wegräumen" },
   ```

3. printf の書式なら、どの訳も英語と同じ変換を同じ順で取らなければなりません（英語に `%d` があるところは `%d`）。
4. `meson test -C build` がすべての行を確かめます（列の数、キーの重複、printf の変換）。

`desk-index` だけが使う言葉は、`src/index.c` の冒頭の表に書き、`W()` で引きます。

### 言語を足す

1. `include/i18n.h` の `enum desk_lang` の `DESK_LANG_COUNT` の前に足します。
2. `src/i18n.c`：`CODES` にその言語のコードと英語名を足し、`WORDS` のすべての行に列を足し（表は規則的です。読み方は `scripts/check-words.py` を見てください）、`MONTH_WHOLE`・`MONTH_SHORT`・`DAY_WHOLE`・`DAY_SHORT`・`DAY_LETTER` に行を足します。単語の間に空白を入れない言語なら、`desk_lang_wide` が真を返すようにします。
3. `src/index.c`：その `WORDS` に列を足し、`when_words` の午前・午後（`HALF`）を足します。
4. 日付の並びが違うなら：`src/draw.c` の `cal_year_word`・`cal_month_year`・`cal_week_range`・`clk_page_day`、`src/index.c` の `when_words` と `stamp_date`。
5. `src/settings.c`：`LANG_WORDS` にその言語自身での名前を、`LANG_VALS` にコードを。`src/wm.c`：`shell.conf` の `language` が受け付けるコードの一覧に。
6. `src/apps.c`：`REGION`（`locale_build`）にその言語のふつうのロケールを。プログラムの名前に使います。
7. CJK の文字を使うなら、`src/fonts.c` にその地域の書体を（`CUTS`、`ONLY_FOR`、`desk_font_set_region`）。
8. 確認：`meson test`、それから `desk-preview --lang xx` に `--shelf`・`--console`・`--post`・`--calendar`・`--memo`・`--agenda`・`--lock`。長い語はいくつかの場所では縮めて収めますが、それでもはみ出す所は短い語を選びます。

訳の無い行は英語で出て、名前の表に行の無い言語は英語の名前で出るので、言語は少しずつ足せます。

## 4. 設定を足す

設定は DEsk が読む `shell.conf` の一行で、ふつうは Shell Settings にも一行あります。

1. **`src/wm.c`**：`struct shell_conf` に項目を足し、既定値を `shell_conf_defaults` に、読み方を書きます。yes/no のキーは `shell_conf_read_file` の連なりに、それ以外は `conf_other` に（`clock` や `motion` を参考に）。悪い値は `wlr_log(WLR_ERROR, ...)` で知らせて既定値のままにします。反映は `shell_apply_all` で行います。起動時と、ファイルが変わるたびに呼ばれます。
2. **`src/settings.c`**：`SETTINGS` の該当する節に行を足します（yes/no は `S_CHECK`、選択は `S_CHOICE` と `*_WORDS`・`*_VALS` の配列）。既定値も。`conf_write` のひな形のコメントにキーを足します。
3. **言葉**：ラベル、その下の説明、選択肢の語を `WORDS` に（§3）。
4. **文書**：[CONFIGURATION.md](CONFIGURATION.md) と、日本語 README の Shell Settings の表に。

DEsk が知らないキーは記録に書かれ、ファイルにはそのまま残るので、古い版と新しい版で一つの `shell.conf` を共有できます。

## 5. 借りるプログラム

### Shell Settings の System タブに行を足す

各行は、その仕事をするプログラムを開きます。`src/settings.c` で：

1. その仕事ができるプログラムの `struct prog` の一覧（好ましい順。`P_DISPLAYS` など）と、最初のプログラムを入れるパッケージを、今の Debian/Ubuntu、Debian 13 / Ubuntu 26.04 より前、Arch、Fedora について言う `struct opens`（`O_DISPLAYS`。そこで何のパッケージもしないなら `""`）。そのプログラムが Wayland のグローバルなしで動かないなら `needs` にその名前を。
2. `SETTINGS` にラベルと説明の付いた `S_ACTION` の行。
3. インストーラが入れるように、`scripts/install_debian.sh`・`install_ubuntu.sh`・`install_arch.sh`・`install_fedora.sh`・`install-desktop.sh` にパッケージを。Fedora では、そのプログラムが本体とは別のパッケージに分かれていて、本体が「おすすめ」しかしていないことがあります（`ibus-setup` など）。Fedora のインストーラは弱い依存を入れないので、確かめてください。

行にはプログラムの名前が添えられ（「ディスプレイ（wdisplays）」）、入っている最初のものを開き、どれも無ければ入れるべきパッケージを示します。

### 標準のプログラム

引き出しの端末・ブラウザ・ファイルマネージャー・テキストエディタは `src/apps.c`（「the standard programs」）で決まります：`shell.conf` の `app.terminal` など、次に既定のアプリの一覧、それから `STD_TERMINAL`・`STD_BROWSER`・`STD_FILES`・`STD_EDITOR` の順。DEsk 自身の好みを変えるならその一覧の順を変えます。ユーザーはビルドし直さなくても `shell.conf` の `app.*` で変えられます。

### プログラムを置く棚

ユーザーはコードなしで `~/.config/desk/shelves.conf` に書けます。

```
org.kde.kate.desktop = Development
```

規則そのものは `src/apps.c` にあります：Categories の表と、自分の Categories が紛らわしいプログラムのための `SHELF_FIX`。

## 6. 音

音は合成です（`src/sfx.c`）。または Faust で作って焼きます（`art/sound/bake.py`：`<名前>-<番号>.wav`。あれば sfx.c の代わりに読みます）。足すには：

1. `include/sound.h` の `enum desk_sound` の `DESK_SND_COUNT` の前に名前を。
2. `sfx.c` に、ファイル冒頭の部品（固有振動、破裂、摩擦、ぱりぱり、すれ、きしみ）で組んだ関数 `s_<名前>(struct buf *b, struct rng *r, int v)` と、`SFX` の行（名前、秒数、版の数）。
3. 鳴らす：`desk_sound_play(DESK_SND_YOURS, pan, gain, 0.0)`。`pan` は -1（左）〜 1（右）で、たいていはその物の画面上の位置。

`DESK_SOUND=file:/tmp/desk-%p.wav` でスピーカーではなくファイルに書くので、誰にも聞かせずに確かめられます。

## 7. 部屋

部屋は二度トレースされます。`src/draw.c` の C（基準）と、GPU の `src/room.glsl` です。**両方を同じように変えてください。**

- 場面は `draw.c` の `scene_map(vec3 p, int kind)` と `room.glsl` の `scene_map` で、素材と陰影がそれに続きます。名前の付いた数値（`RM_*`）は `room_glsl_preamble` でシェーダに届くので、新しい数値は二度書かずに `draw.c` の名前付きの定数にします。
- ビルドは `room.glsl` と `gpu.c` から `ROOM_KEY` を作るので、古いコードで焼いた部屋は使われません。`draw.c` だけの変更ではキーは変わらないので、見るときは `~/.cache/desk/room-*` を消すか、`XDG_CACHE_HOME` を空のディレクトリにします。
- 両者が合っているかは `./build/desk-gputest --room` で。すべてのシートを両方で焼き、被覆率・持ち主・色の違いを出します。
- CPU での重さは `DESK_NO_GPU=1` で。記録の `desk: room:` の行が各シートにかかった時間を言います。2D Fallback（`graphics = 2d`）は軽いままにしてください。
- 当たり判定（本棚・Control Panel・ポストでポインタの下に何があるか）はトレースが記録する持ち主の番号を使うので、新しくクリックできる物には番号が要ります（`room_owner` と `*_owner` の関数）。

## 8. 机の上の物

部屋の上に描く動く部分（時計の表示、Control Panel のランプ、手紙、紙片）はスプライトで（`wm.c` の `struct desk_sprite`、`sprite_paint`）、cairo のコンテキストと画面の大きさを受け取る `draw.c` の関数が描きます。位置は部屋のカメラから出すので（`room_project`）、どの形の画面でも焼いた物に付いたままです。足すには：

1. `draw.c` に描く関数、`include/desk.h` に宣言。
2. 画面上のどこにあるか（箱）を部屋から。今の重ね描きについて `desk_overlay_box` がしているように。
3. `struct desk_server` にスプライトを持ち、見せる内容が変わったときに描き（その内容のハッシュを鍵に）、正しい層に置きます（ARCHITECTURE の §4.1）。
4. クリックできるなら、`draw.c` に当たり判定、`server_cursor_button` の正しい順番の所に分岐を（ARCHITECTURE の §4.4）。
5. コンポジタなしで確かめられるよう、`desk-preview` でも描きます。

手に取って使う物や、紙ではない窓は別です。それは**物**で、自分のファイルを一つ持ちます。[THINGS.ja.md](THINGS.ja.md) を見てください。

## 9. アイコンとテーマ

- DEsk のアイコンテーマは生成物です：`python3 themes/make-icons.py` が `themes/icons/DEsk` を書きます。形と光はグラデーション、質感は SVG のフィルタで、フィルタなしでも同じ物に見えなければなりません（Qt はフィルタを描きません）。`themes/contact-sheet.py` で一覧の画像を作って見られます。
- GTK テーマは Adwaita の上に重ねた `themes/gtk/DEsk/gtk-3.0/gtk.css` です。
- どちらも DEsk 専用の dconf の層に入るので、ほかのデスクトップの見た目は変えません。

## 10. 変更を配る前に

```sh
meson test -C build
# DEsk を確かめている構成（それぞれ別のディレクトリで）
meson setup b-min -Dpost=disabled -Dsound=disabled -Dxwayland=disabled
meson setup b-werror -Dbuildtype=debug -Dwerror=true
meson setup b-asan -Dbuildtype=debug -Db_sanitize=address,undefined
meson setup b-tsan -Dbuildtype=debug -Db_sanitize=thread
```

それぞれビルドし（`meson compile -C b-...`）、サニタイザの版を入れ子で動かして変えた所を通し、Ultra と 2D、長い言語と CJK の言語でプレビューを見て、記録に `[ERROR]` の行が無いか確かめます。これまでの変更をどう確かめてきたかは開発の記録（[design.md](design.md)）にあります。
