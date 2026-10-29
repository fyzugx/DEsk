[English](ARCHITECTURE.md) | **日本語**

# DEsk の設計（アーキテクチャ）

DEsk v0.1 (BETA) の技術的な全体像です。どんなプログラムがあるか、コンポジタがどう組まれているか、部屋をどう描くか、どのスレッドが何を受け持つか、何がどこに保存されるかをまとめています。コードを読む人・変える人向けです。見た目と使い方は [README](../README.ja.md)、よくある変更の手順は [MODDING.ja.md](MODDING.ja.md)、設定とファイルの一覧は [CONFIGURATION.md](CONFIGURATION.md)（英語）、開発の記録は [design.md](design.md) にあります。

## 1. 原則

コードの置き場所や振る舞いに迷ったときは、たいていこれで決まります。

- **机は場所であって、スキンではない。** 窓は紙、ドックは引き出し、アプリ一覧は本棚、通知はポストの手紙です。新しい機能は「ボタンを足す」ではなく「机の上のその物ならどうするか」で考えます。
- **作るのは部屋そのものと、コンポジタにしかできないことだけ。** 設定画面・アプレット・ファイルマネージャーなどは既存の自由なソフト（Xfce・MATE・LXQt・GNOME・NetworkManager など）を借り、Shell Settings から開きます。線引きは `src/settings.c` の冒頭のコメントと README の「デスクトップ環境として」の表にあります。
- **ランプは一つ、部屋は一つ。** 部屋（机・引き出し・本棚・Control Panel・ポスト）は一つのレイトレースの場面です。画面の形と画質の段階ごとに一度だけ焼いてディスクに保存し、ほかのものの下に合成します。その上に描くもの（紙、手紙、時計の光）も同じランプで照らします。
- **大きさはポイントで。** 机の 1 単位は 45 ポイント、1440×900 が 1 倍です。大きい画面には物が大きく映るのではなく、机が広く映ります（`desk_px_per_pt`、`DESK_SCALE`）。
- **コンポジタのスレッドは待たない。** 部屋のトレース、ディスクの走査、アイコンの変換、ミキサーへの問い合わせ、PAM、天気、音は、それぞれ自分のスレッドで動き、結果はイベントループを通して返します。
- **古い機械でも軽く。** 目標は 2010 年頃のノートで Low のまま 60 fps。どの段階（Ultra・High・Medium・Low・2D Fallback）も同じコードの仕事を減らしたもので、別の実装は持ちません。
- **まず英語、そして世界へ。** UI の言葉はコードに英語で書き、表を通して訳します（v0.1 は 7 か国語）。季節の演出は、どの文化にも共通するもの（誕生日、新年）だけです。

## 2. プログラム

| プログラム | ソース | 何か |
| --- | --- | --- |
| `desk` | `src/wm.c` と描画ライブラリ | Wayland コンポジタ。デスクトップのすべて。GPU の試運転の子プロセス（`desk-gpu-trial`。同じ実行ファイルを `DESK_GPU_PROBE` 付きで起動）も兼ねます。 |
| `desk-session` | `session/desk-session`（sh） | ログイン画面が起動するもの。セッションの環境（Wayland 優先のツールキットの変数、入力メソッド、DEsk 用の dconf の層）を整え、`~/.local/state/desk/session.log` に記録を残し、`DESK_SESSION=1` で `desk` を起動します。 |
| `desk-session-helper` | `scripts/desk-session-helper`（sh） | DEsk が描かない部分（polkit エージェント、キーリング、IBus か fcitx、ネットワークのアプレット、udiskie、電池の知らせ）を、DEsk 自身の autostart エントリから選んで起動します。 |
| `desk-settings` | `src/settings.c` | Shell Settings。同じライブラリで描く普通の Wayland クライアントで、`shell.conf` を書きます。 |
| `desk-index` | `src/index.c` | DEsk Experience Index（1.0〜9.9 で採点するベンチマーク）。`--text` は端末向け、`--suggest` はインストーラが Graphics の段階を選ぶのに使います。 |
| `desk-preview` | `src/preview.c` | コンポジタなしで机・眺め・部品を PNG に描きます。描画の変更を確かめる主な道具です。 |
| `desk-gputest` | `src/gputest.c` | GPU の経路を確かめます。`desk-gputest --room` はすべてのシートを GPU と CPU の両方で焼いて比べます。 |
| `desk-ctl` | `scripts/desk-ctl`（sh） | 固まった机のための `reset` と `logout`。端末やほかの VT から使います。 |
| `desk-window` | `scripts/desk-window`（sh） | 今のセッションの中に DEsk を窓として起動します。 |
| `desk-screenshot` | `scripts/desk-screenshot`（sh） | Print / Alt+Print：grim で撮り、ピクチャに保存し、手紙で知らせます。 |

### セッションの始まり

1. ログイン画面が `/usr/share/wayland-sessions/desk.desktop` から `desk-session` を起動します。
2. `desk-session` が `XDG_CURRENT_DESKTOP=DEsk`、ツールキットのバックエンド、入力メソッドの選択、dconf のプロファイルを設定し、`desk` を起動します。
3. `desk` は `$XDG_RUNTIME_DIR/desk.lock` に排他の `flock` を取り（ユーザーごとに一つ。`DESK_ALLOW_MULTIPLE=1` で解除）、wlroots を起動し、部屋をキャッシュから読むかトレースしている間に最初のフレームを出します。
4. ディスプレイができると、`src/session.c` が D-Bus と systemd にその場所を伝え（`dbus-update-activation-environment --systemd`）、XDG の autostart エントリを起動します。DEsk とぶつかるもの（別の通知デーモン、X のスクリーンロッカー、ほかのデスクトップの polkit エージェント）は除きます。
5. DEsk 自身の autostart エントリ（`session/autostart/*.desktop`、`OnlyShowIn=DEsk`）が、借りる部品ごとに `desk-session-helper` を起動します。

ログアウト・再起動・電源オフは、まずすべての窓に閉じるよう頼み（`wm.c` の `end_begin`）、そのあと logind を通して行います（`src/power.c`）。

## 3. ソースの地図

| ファイル | 行数 | 役割 |
| --- | ---: | --- |
| `src/wm.c` | 約 22,500 | コンポジタ：wlroots の準備、出力、入力、窓（xdg-shell と Xwayland）、シーンの層、机のすべての操作（引き出し、眺め、カレンダー、メモ、ポスト、トレイ、時計、ロック、Ctrl+Alt+Del）、`shell.conf` と `displays.conf`、複数の画面。 |
| `src/draw.c` | 約 34,900 | 描画ライブラリ：レイトレースの部屋（基準の C のトレーサ、GPU の駆動、キャッシュ）と、cairo で描くすべて（紙、枠、折りたたみ、丸める動き、本とバインダー、Control Panel の動く部分、手紙、カレンダーとメモ、Shell Settings のフォーム、玄関の扉）。 |
| `src/room.glsl` | 約 2,400 | 部屋のトレーサを GLES 3.00 のフラグメントシェーダにしたもの。`draw.c` の C を一行ずつ移したもので、基準は C です。 |
| `src/gpu.c` | 約 800 | シェーダ用の画面外の EGL/GBM/GLES コンテキスト。画面と同じレンダーノードを選びます。 |
| `src/apps.c` | 約 2,000 | `.desktop` ファイルをワーカースレッドで走査し、8 つの棚に分け、アイコンを探し、標準の端末・ブラウザ・ファイル・エディタを選び、プログラムを起動します。 |
| `src/i18n.c` | 約 1,700 | 言葉の表（7 か国語）と月・曜日の名前。仕組みは `include/i18n.h` に。 |
| `src/fonts.c` | 約 280 | 役割ごとの書体（CJK は地域ごと）。 |
| `src/settings.c` | 約 3,200 | `desk-settings`（Shell Settings）。 |
| `src/index.c` | 約 4,500 | `desk-index`。 |
| `src/post.c` | 約 440 | 通知デーモン（`org.freedesktop.Notifications`）。 |
| `src/tray.c` | 約 1,350 | StatusNotifierItem のウォッチャーとホスト（トレイの紙片）。 |
| `src/media.c` | 約 800 | MPRIS のクライアント（時計の MEDIA、メディアキー）。 |
| `src/ime.c` | 約 760 | どの入力メソッドがオンか（IBus、fcitx5）。 |
| `src/session.c` | 約 470 | autostart とセッションの環境。 |
| `src/lock.c`、`src/user.c` | | PAM でのパスワード確認（スレッドで）、扉に出す名前と写真。 |
| `src/power.c` | | logind を通したログアウト・再起動・電源オフ。 |
| `src/panel.c` | 約 950 | Control Panel の計器が読む `/proc`・`/sys`・ミキサーの値。 |
| `src/occasion.c` | 約 530 | 日付（誕生日、新年など）と玄関の天気（open-meteo.com。切れます）。 |
| `src/cards.c` | 約 190 | wlroots に渡す DRM のカード：画面の口を持つ本物のカードがあれば、ファームウェアの画面（simpledrm）を外す。 |
| `src/agenda.c`、`src/memo.c` | | カレンダーの付箋とメモ帳の紙。`$XDG_DATA_HOME/desk` に保存。 |
| `src/camera.c` | 約 260 | カメラやマイクが使われているか（時計の二つのランプ）。 |
| `src/sfx.c`、`src/sound.c` | 約 2,750 | すべての音を起動時に合成し、ミキサーのスレッドで鳴らします（libpulse、または実行時に読み込む ALSA）。 |
| `src/preview.c`、`src/gputest.c` | | 開発用の道具（§2）。 |
| `include/desk.h` | 約 1,200 | 描画ライブラリの API。`wm.c`・`settings.c`・`index.c`・道具が使います。 |
| `include/compat.h` | | 旧名（DeskWM）の設定とデータを引き継ぎます。 |
| `protocols/` | | wlr-output-management と wlr-output-power-management の XML。 |
| `scripts/` | | インストーラ、セッションのスクリプト、ビルドの補助（`glsl2c.py`、`room-key.py`）、`check-words.py`（訳のテスト）。 |
| `session/` | | セッションのエントリ、PAM のファイル、autostart、既定のアプリの一覧、ポータルの設定。 |
| `themes/` | | DEsk のアイコンテーマ（`themes/make-icons.py` で生成）と GTK テーマ。 |
| `packaging/` | | おすすめのプログラム一式のための Debian の equivs ファイル。 |
| `docs/` | | この文書群と開発の記録。 |

大きな二つのファイルは `/* ---- 名前 ---- */` という形のコメントで節に分かれています（`draw.c` にはさらに `/* ==== */` の見出し）。`---- ` で検索すると目次になります。どの節も、何をしていてなぜそうなのかのコメントから始まります。

## 4. コンポジタ（`src/wm.c`）

DEsk は **wlroots 0.18** の上にできています。バックエンドとレンダラーは wlroots が選びます（ログインセッションでは DRM と libinput、入れ子では Wayland か X11 のバックエンド、テストではヘッドレス）。GPU があればレンダラーは GLES2、なければ pixman です。状態は一つの `struct desk_server` にまとまっています。

### 4.1 シーングラフ

画面に出るものはすべて一つの `wlr_scene` のノードです。層は次の順（下から上）に作られる木です。

| 層 | 中身 |
| --- | --- |
| `layer_side` | 机のある画面以外の画面の、机の続きの板（§4.6）。 |
| `layer_bg` | トレースした部屋（一枚のバッファ）。カメラと引き出しで動きます。 |
| `layer_desk` | 部屋の上に描く動く部分：時計、Control Panel のランプと針、ポストの手紙、紙片、ポインタの下の光。 |
| `layer_folded` | 机の上に置いた、折りたたんだ窓（どの窓よりも下）。 |
| `layer_win` | 窓。それぞれが紙の枠・影・角・クライアントのサーフェスを持つ木です。 |
| `layer_full` | 全画面の窓。 |
| `layer_xover` | X11 の override-redirect の窓（メニュー、ツールチップ）と入力メソッドの候補窓。 |
| `layer_drawer` | 机の上に引き出した引き出しと、その中身。 |
| `layer_parked` | 折っている途中・広げている途中の紙（動く間は窓の上）。 |
| `layer_shelf`、`layer_console`、`layer_post` | 向き直る三つの眺め：本棚、Control Panel、ポスト。 |
| `layer_top` | ログインのときの暗がりと数える時計、初回起動の手紙。 |
| `layer_drag` | 運んでいるもの。 |
| `layer_lock` | 玄関の扉（画面ロック）と、すべての画面を覆う黒。 |
| `layer_glow` | 新年の花火の光。 |
| `layer_dark` | 入れ子のときの、暗くなった画面の黒。 |

机の層（`layer_bg`・`layer_desk`・`layer_drawer`）は、引き出しを開けるときとカメラが動き出すときに一緒に動き、紙の層（`layer_folded`・`layer_win`・`layer_xover`・`layer_parked`）もそれについて動きます（`desk_follow`）。

### 4.2 バッファとスプライト

DEsk は **CPU 上の cairo** で `struct desk_buf`（cairo の画像サーフェスを包んだ `wlr_buffer`）に描き、`scene_put` でシーンに渡します。よく変わるものは *スプライト* です。変わったところだけを覆うバッファで、見せる内容のハッシュを鍵にしているので（`struct desk_sprite`、`sprite_paint`）、変わらないものは二度と描きません。部屋の上の描画は、段階で決まる重ね描きの解像度（`desk_overlay_size`）で行い、シーンに拡大させるので、大きい画面でも軽く済みます。ソースボックスはすべて `scene_source` を通り、絵の内側へ最後のビットまで収められます（絵の外へ出る箱に wlroots は assert で止まり、浮動小数点で出した箱は丸めの誤差で出ることがあります）。ノードの絵の取り替えは `scene_swap` で、前の箱も同じく収めます。絵の大きさはノードの横に記録します（`scene_new`・`pic_size_keep`、読むのは `scene_pic_size`）。GLES2 の画面では、絵が GPU に載ると scene が元の絵を手放す（`buffer` が NULL になる）ので、大きさを `buffer` から読んではいけません。

### 4.3 フレーム

`output_frame` は出力のフレームごとに一度呼ばれます。机の画面で（あるいは机の画面に 12 ms 以上フレームが来ていなければほかの画面で）`output_frame_work` を呼び、すべての動きを実時間で進めます：折りたたみ、紙の影、角、大きさの変更、投げ捨て、引き出し、眺めへのカメラの動き、バインダー、カレンダーとメモ、ログインのランプ、ロック、時計とトレイの紙片。そのあとシーンを確定します。何も変わらなければ何も描かず、何もしていない机は何も確定しません。

### 4.4 入力

ポインタの出来事は決まった順に振り分けます（`process_cursor_motion`、`server_cursor_button`、`server_cursor_axis`）。

1. ロック（扉の鍵だけ）
2. 運んでいるもの（引き出しから出したもの、折りたたんだ紙）
3. 動かしている・大きさを変えている窓、時計の音量のバー、トレイのカード
4. 机のある画面以外の画面では、窓だけ（§4.6）
5. 開いている眺め：ポスト、Control Panel、本棚（バインダー、本、カレンダー、メモ）
6. 窓がかぶっていなければ机そのもの：時計、紙片、ALL APPS の札、Control Panel とポストの札、机に置いた手紙、引き出しの取っ手
7. 窓：折れた角、枠の縁と見出し、それからクライアント

キーボードのショートカットは `handle_keybinding` です。机自身の文字欄（本の検索の行、カレンダーの付箋、メモ帳）とプログラムへの入力は入力メソッドを通ります。DEsk は `text-input-v3` を実装し、それを自分で IBus につなぐので（「typing into programs' windows, through the input method」の節）、候補は書いている文字のすぐ下に出ます。

### 4.5 窓

`struct desk_toplevel` は xdg-shell か Xwayland の窓を表します。サーバー側の装飾を頼む窓（xdg-decoration か KDE の server-decoration）には DEsk の紙の枠が付き、自分で枠を描く窓も `paper_for_all_windows = no` でなければ紙に載ります。操作は右上の角だけです：Fold（たたんで最小化）、Unfold / Refold（最大化と元に戻す）、Discard（閉じる。丸めて投げる）。閉じるときは必ずまずクライアントに頼みます。

### 4.6 二つ以上の画面

机が描かれるのは一つの画面だけで、レイアウトの位置 0,0 にある画面です（`first_output`）。ほかの画面には机の続きの板が出ます（`side_desks_paint`、`desk_draw_side_desk`）。部屋から天板の帯を切り出し、鏡に映すように繰り返して敷くので、板目は画面の境を越えて続きます。窓はそこにも置けます。最大化と全画面は窓のある画面で、新しい窓はポインタのある画面に出ます。机の画面でのこと（引き出し、眺め）で、ほかの画面の窓は動きません。画面を外すと、そこにあった窓は残った一番近い画面へ移ります（`windows_onto_screens`）。表示の道具が決めた配置は、画面の組ごとに `displays.conf` に覚えます。

### 4.7 プロトコル

作る wlroots のグローバル：compositor と subcompositor、xdg-shell、xdg-decoration と KDE の server-decoration、xdg-output、xdg-activation、data-device と primary selection を持つ seat、data-control、text-input-v3、viewporter、presentation-time、single-pixel-buffer、linux-dmabuf（GPU のレンダラーのとき）、screencopy（grim 用）、idle-notify と idle-inhibit、output-management と output-power-management、pointer-constraints と relative-pointer（ゲーム）。まだ無いもの：layer-shell、session-lock、gamma-control、input-method-v2。

### 4.8 D-Bus

libsystemd がある（`-Dpost=enabled`）と、DEsk はイベントループのスレッドで sd-bus を使います。

- `org.freedesktop.Notifications` のサーバー（`post.c`）：ポストの手紙。
- StatusNotifierWatcher（バスにほかにいなければ）とホスト（`tray.c`）。
- MPRIS のクライアント（`media.c`）：再生中のもの、メディアキー。
- IBus（専用のバス）と fcitx5（`ime.c`）、セッションの終わりには logind。

## 5. 描画ライブラリ（`src/draw.c`）

`draw.c` は `apps.c`・`panel.c`・`agenda.c`・`fonts.c`・`i18n.c`・`memo.c`・`user.c`・（EGL があれば）`gpu.c` と一緒に静的ライブラリ `desk-draw` になります。API は `include/desk.h` です。コンポジタ、Shell Settings、Experience Index、プレビューがみなこれをリンクするので、どこで描いても同じ見た目になります。

### 5.1 部屋

部屋は **符号付き距離場（SDF）**（`draw.c` の `scene_map`）で、サンプルごとにレイマーチします。一つのサンプルは、マーチ、法線、素材、凹凸と使い込み、環境遮蔽、一つの面光源からの柔らかい影、球面調和の環境光、一回の反射、クリアコートを経て、被覆率・持ち主の多数決（そのピクセルがどの物か。当たり判定用）・グローでまとめられます。

部屋は **層**（シート）として焼きます：引き出しを抜いた部屋、引き出しだけ（引き出した状態）、机から見た本棚、Control Panel、ポスト。部屋のシートの上に引き出しのシートを滑らせることで、引き出しがフレームレートどおりに開きます。それぞれの眺めへのカメラの動きは短いフィルムとして焼きます。

トレーサは二つあります。

- **C**（`draw.c`）：基準。CPU のすべてのコアでタイルに分けて動きます。
- **GLSL**（`room.glsl` を `gpu.c` で）：同じコードを移したもので、画面と同じレンダーノードの画面外の GLES 3.0 コンテキストで動きます。名前の付いた数値はすべて `draw.c` からシェーダに渡すので（`room_glsl_preamble`）、両者が形について食い違うことはありません。ドライバーはまず子プロセス（`desk-gpu-trial`）で試すので、落ちるドライバーが机を道連れにすることはありません。失敗すれば CPU で焼きます。

`desk-gputest --room` が二つのトレーサをシートごとに比べます。

### 5.2 段階とキャッシュ

| 段階 | すること |
| --- | --- |
| Ultra | より細かく焼き、GPU では幅も広く。動いている間も粗く描きません。 |
| High | 設計どおり（既定）。 |
| Medium、Low | 小さく焼き、重ね描きも小さく。動いている紙は粗く描きます。 |
| 2D Fallback | 一度だけ小さく、1 画素 1 サンプル（ぼかして）。影・フェード・ぼかし・アニメーションなし。画面を CPU で描いているときは自動でこれになります。 |

焼いたシートは `$XDG_CACHE_HOME/desk` に、シートの種類・大きさ・机の幅・段階・`ROOM_KEY` の付いた名前の PNG で置きます。`ROOM_KEY` はビルド時に `room.glsl` と `gpu.c` から作るハッシュです（`scripts/room-key.py`）。部屋のコードを変えれば古いシートは使われず、普通の更新では残ります。

### 5.3 平らなもの

ほかはすべて cairo です：窓の枠と紙（繊維、古び、折り目）、折りたたみ（`desk_fold_*`）、丸める動き、本とバインダー、手紙、カレンダーとメモ、Shell Settings のフォーム（`desk_shell_form_*`）、Control Panel の動く部分、時計の表示、玄関の扉（扉と歩いて入る場面のための、二つ目の小さなレイトレーサ）。文字は `fonts.c`（役割ごとにラテン文字の書体と CJK の書体）と `wrap_lines`（単語での折り返し、CJK の折り返し、行頭禁則）を通ります。

## 6. スレッド

| スレッド | 持ち主 | すること |
| --- | --- | --- |
| コンポジタ | `wm.c` | wlroots のイベントループ：入力、プロトコル、スプライトの描画、D-Bus。 |
| 部屋のワーカーとトレーサ | `draw.c` | 今の画面と段階の部屋を読むか焼く。CPU のトレースはすべてのコアでタイルごとに。GPU のコンテキストは部屋のスレッドにあります。 |
| GPU の試運転 | `draw.c` | スレッドではなく子プロセス。ドライバーを信用する前に試します。 |
| 扉のトレーサ | `draw.c` | ロック画面の場面。描くときはすべてのコアで。 |
| アプリの走査 | `apps.c` | アプリのディレクトリを歩き、変わらないスナップショットを公開します（`desk_apps_acquire`/`release`）。 |
| アイコンの仕事 | `apps.c`、`tray.c` | アイコンを探して変換（必要なら rsvg-convert、無ければ ImageMagick で SVG から）。 |
| テクスチャの準備 | `draw.c` | 起動時に紙と木のタイルを作ります。 |
| ミキサー | `sound.c` | 机の音を混ぜ、libpulse か ALSA に流します。 |
| 音量 | `panel.c` | `wpctl` でミキサーに聞き、設定します。 |
| 天気 | `occasion.c` | オンのとき、30 分ごとに open-meteo.com へ `curl`（か `wget`）を一回。 |
| パスワードの確認 | `lock.c` | PAM（間違えると待たされます）。 |

共有する状態はミューテックス（`g_room_lock`、`g_render_lock`、アプリ一覧の参照カウント）で守るか、アトミックにします（部屋の進み具合の数、部屋の形の数）。ワーカーは wlroots にもシーンにも触りません。結果はロックの中で連番か世代番号と一緒に公開し（新しい部屋、アプリ一覧の新しいスナップショット）、コンポジタが次の tick に自分のスレッドで拾います。あるいはイベントループが見ているパイプか eventfd で起こします（天気、トレイのアイコン、パスワードの確認）。v0.1 は ThreadSanitizer・AddressSanitizer・UndefinedBehaviorSanitizer で動かしています（§9）。

シグナルは受け止め、塞ぎません。ハンドラはイベントループが見ているパイプに 1 バイト書くだけで（`cad_signals`）、ループがそれを読んで動きます。`desk-ctl` の USR1 と USR2 は机の立て直しとセッションの終わり、TERM・HUP・INT（logind がセッションを終えるとき、窓の中の机での ^C）はログアウトと同じ後片付けで机を終え、手にしているもの（メモ、カレンダーの付箋）を残します。塞いだシグナルは exec を越えて机が起動するすべてのプログラムに受け継がれますが、ハンドラは受け継がれません。

## 7. ファイル

| 何 | どこ |
| --- | --- |
| 設定 | `$XDG_CONFIG_HOME/desk/shell.conf`（機械の既定は `/etc/xdg/desk/shell.conf`） |
| 画面の配置 | `$XDG_CONFIG_HOME/desk/displays.conf` |
| 引き出しの中身 | `$XDG_CONFIG_HOME/desk/drawer`（ほかの画面の引き出しは `drawer@画面名`、二重底の下は同じ名前に `.hidden`） |
| 棚の指定 | `$XDG_CONFIG_HOME/desk/shelves.conf` |
| 既定のプログラム | `~/.config/desk-mimeapps.list`、それから freedesktop.org の一覧 |
| カレンダーの付箋、メモ帳 | `$XDG_DATA_HOME/desk/calendar`、`$XDG_DATA_HOME/desk/memo` |
| 焼いた部屋、アイコン、扉 | `$XDG_CACHE_HOME/desk/` |
| 記録 | `$XDG_STATE_HOME/desk/session.log`（と `.old`）、`install.log` |
| Shell Settings 用 | `$XDG_RUNTIME_DIR/<WAYLAND_DISPLAY>.render`（画面を CPU で描いているか） |
| ユーザーごとに一つ | `$XDG_RUNTIME_DIR/desk.lock`（PID 入り。`desk-ctl` が読みます） |

DEsk が書くファイルはすべて、別の名前に書いてから名前を付け替えます。形式は [CONFIGURATION.md](CONFIGURATION.md) にあります。

## 8. 言語・書体・音

- **言葉**：`TR("English")` は英語で `WORDS`（`src/i18n.c`）を引きます。言語ごとに列がある表です（`enum desk_lang`：en、ja、zh、ko、es、fr、de）。訳の無い語は英語のまま出ます。キーには文脈を付けられます（`"clock|DAY"`）。`desk-index` は `index.c` に自分の表を持ちます。`meson test` が両方の表を確かめます（`scripts/check-words.py`）。
- **書体**：役割（UI、印刷、見出し、刻印、タイプ、画面、手書き）ごとにラテン文字の一覧と CJK の一覧があり、CJK の一覧は中国語・日本語・韓国語ごとに選び直します（Noto CJK の SC・JP・KR）。同じ字でも国によって字形が違うためです。
- **音**：録音は一つもありません。`sfx.c` が起動時に、固有振動、雑音、ぱりぱりという音、きしみからすべての音を合成し、`sound.c` が少しずつ違う版を、画面上のその物の位置の左右に振って鳴らします。

## 9. ビルドと確認

```sh
meson setup build && meson compile -C build
meson test -C build                       # 訳の表
./build/desk-preview -o out.png --shelf   # コンポジタなしで描く
./scripts/desk-window                     # 今のセッションの中で動かす
```

v0.1 では次を行いました。

- 4 つの構成で警告 0 のビルド：最小（`-Dpost=disabled -Dsound=disabled -Dxwayland=disabled`）、`-Dwerror=true`、`-D_FORTIFY_SOURCE=3`、AddressSanitizer＋UndefinedBehaviorSanitizer
- 厳しい警告（`-Wshadow -Wformat=2 -Wnull-dereference -Wduplicated-cond -Wlogical-op` など）と GCC の `-fanalyzer` を全ファイルに
- コンポジタ、プレビュー、Experience Index を ThreadSanitizer で
- 入れ子の机、Shell Settings、Experience Index を ASan/UBSan で。わざと壊した設定・データ・`.desktop` ファイルを与えたものも含みます
- すべての眺めを 7 か国語で描画

`scripts/verify.sh` は `Dockerfile` の Debian 13 のイメージを作り、その中で DEsk をヘッドレスで動かします。

## 10. 何が安定か

v0.1 には **プラグインの API はありません**。`include/` のものはすべて内部用で、リリースの間に変わることがあります。安定しているのは DEsk が実装している標準のインターフェースです：Wayland のプロトコル（§4.7）、freedesktop.org の通知、StatusNotifierItem、MPRIS、XDG の autostart と `.desktop` ファイル、そして §7 のファイル（v0.1 はそのキーを読み続けることを約束します）。DEsk そのものを変えるにはソースを変えます。その方法は [MODDING.ja.md](MODDING.ja.md) にあります。
