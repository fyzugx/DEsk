[English](THINGS.md) | **日本語**

# 物：紙ではない窓

DEsk では窓は一枚の紙です。折れて、広がって、角がめくれて、閉じるとくしゃくしゃになります。机のことを何も知らないプログラムには、これが決まりです。

**物**はその例外です。机の上にあって、そのものに合った決まりを持ちます。動画の小窓は電話の画面で、電話は折れません。テキストエディタはノートで、打つのもペンで書くのもでき、ノートは閉じ、机の端へ持っていくと片付きます。電卓はそもそも窓ではありません。

物は **C のファイル一つ**と一覧に載せる名前で、たいていは絵のフォルダが一つ付きます。知っておくことは `include/thing.h` だけです。埋める欄と、机に頼めることが書いてあります。

`include/` のほかのものと同じで、`thing.h` は内部のもので、版によって変わることがあります（[ARCHITECTURE.ja.md](ARCHITECTURE.ja.md) の 10 節）。物は DEsk のソースへの変更で、一緒にビルドします（[MODDING.ja.md](MODDING.ja.md)）。このページは、今ある三つと一緒に読んでください。

## 三つの形

| 形 | 窓は | 例 | 読むもの |
| --- | --- | --- | --- |
| 何も知らないプログラムに着せる | 誰かの窓を、物の画面にする | 電話 | `src/thing-phone.c` |
| 自前のプログラムに着せる | 物の上に描くプログラム | ノートと、その筆箱 | `src/thing-notebook.c`、`src/notepad.c` |
| 窓がない | なし。窓の間に自分の木を持つ | 電卓 | `src/thing-calc.c` |

一つ目が、机に物を足すいちばん安い方法です。書くのは物だけです。端末に画面を、音楽プレーヤーにテープデッキを着せても、プログラムは知らなくて構いません。

## どの物も守ること

決まりは物が決めますが、使う人が迷わないように、次は変えません。

1. 閉じ方が見て分かる。
2. 持って動かせる。
3. 窓があるなら Alt+Tab に出る。
4. 全画面の時は物を外し、窓だけにする（その間 `view_thing` は NULL を返し、あとは机がやります）。
5. 絵が読めない時は普通の紙に戻り、何も壊れない（`ready`）。
6. 読むもの、字と数字は、どの画質でも画面の画素で描く。絵はぼやけても、字はぼやけない。

ノートは、目立たないけれど物が持てるものをいくつか使っています。机の上の、置いた所にある自分の物を、窓より先に手で扱うこと（`hand_down`・`drag`・`release`・`wheel` と、`desk_windows` に置いた自分の絵を `wlr_scene_node_at` で確かめる：筆箱）。机の端での振る舞い（`at_edge`）と去り方（`goes`、絵は `view_picture`）を、画面の半分やくしゃくしゃの玉の代わりに持つこと。広げた時に下に残す分（`open_out_below`）。press を自分で持っていても、窓の上の指はプログラムに渡すこと（`program_fingers`）。ペンで書いている時はキーボードを上げないこと（`pen_writes`）。プログラムとは、プログラムが見ている小さなファイル一つで話します。同じ机の上の二つのプロセスには、それで足ります。

## いちばん小さい物

xterm に木の枠を着せます。絵はありません。

```c
/* src/thing-frame.c */
#include "thing.h"

#include <string.h>

enum { FRAME = 26 };

static bool frame_claims(const struct desk_toplevel *view)
{
	const char *id = view_program_id(view);
	return id && !strcmp(id, "XTerm");
}

static void frame_paint(cairo_t *cr, struct desk_toplevel *view, double x, double y, double w,
	double h, const char *title, bool focused, bool resting)
{
	(void)view;
	(void)title;
	(void)focused;
	(void)resting;
	cairo_set_source_rgb(cr, 0.30, 0.18, 0.09);
	cairo_rectangle(cr, x, y, w, h);
	cairo_fill(cr);
}

const struct desk_thing desk_thing_frame = {
	.name = "frame",
	.claims = frame_claims,
	.no = THING_NO_FOLD | THING_NO_CORNER | THING_NO_CRUMPLE,
	.inset_l = FRAME, .inset_t = FRAME, .inset_r = FRAME, .inset_b = FRAME,
	.pad = 8,
	.paint = frame_paint,
};
```

あとは `src/things.c` の一覧（`extern` と名前）と、`meson.build` の `wm_sources` に足すだけです。これで枠を持って動かせ、縁で大きさを変えられ、Alt+Tab に出て、全画面の時は枠が外れます。ほかに何も書かなくて構いません。

書かなかった欄は、紙と同じか、何もしません。欄の意味は `thing.h` にあります。初めに窓に着せる物の欄、次に `drag`・`release`・`tick` など手やコマをいつでも受け取りたい物の欄、最後に引き出しにしまう物の欄が並びます。

窓に着せる物の状態は `view_thing_data` に置きます。初回はゼロで、窓と一緒に机が片付けます。窓のない物は `static` の構造体を持ちます。

**指はそのまま使えます。** 物の中の窓に置いた指はプログラムのものです。物そのものに置いた指は手になります（`hand_down`・`drag`・`release`、`press`、または紙と同じように持つ・引く）。窓の無い物にだけ `hand_here` が要ります。机の上を動く指が、その物を運ぶのか、どこかへ行くのかを知るためです。

指ごとに別々のことをする物は、`finger_down`・`finger_move`・`finger_up` で指を自分で受けられます（画面のキーボード、`src/thing-keyboard.c`）。`over_all` を付けると玄関を含めて何より先に聞かれ、`text_wanted` で文字を打つ所が現れたことを知らされます。

## 絵

机に置く物は、先に Blender で焼きます。台本は物ごとに `art/<物>/` に一つで、絵は必要な時に読みます（`desk_art_find`）。`art/kit.py` に台本で共通のものがあります。材質、角丸の形やキー、光、一つの目、フィルム、縁を薄くした絵の一部、キーの絵（上がった列と押した列）、ガラスの下の光、磨きです。

```python
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import kit
```

手本は `art/phone/phone.py` です。手間のかかった点：

- **鏡の方向に光を置くと、黒いガラスが灰色になります。** 遠い一つの目から見た平らな面は、一つの方向しか映しません：`(0, sin 傾き, cos 傾き)`。ランプはそこから外します。
- **磨いた面は、映しているもので決まります。** `seen_only_in_polish` は光沢にしか見えない光の板で、光のリンクでキーには映しません。白く映すと色が負けて金属に戻るので、面の色に合わせて弱くします。
- **伸ばす絵は、伸ばす向きに変わってはいけません。** 横に伸ばす絵は上下にだけ変わるようにします（`light_by_height`）。そうすれば、キーを別の位置に置いても合います。
- **平らな面は本当に平らにします。** 滑らかにした多角形は、三角形に沿って映り込みを曲げます：`o.data.polygons[-1].use_smooth = False`。
- **寸法は一度だけ書きます。** `kit.write_map` が、C に要る画素を絵の隣の見出しに書きます（`art/phone/phone-map.h`）。

同じ台本は、同じファイルを 1 バイトも違わず作ります。何も変わらないはずの変更は、前後を `--quick` で焼いて比べて確かめます。

絵は `meson.build` の `install_data` で `share/desk/<物>` に入れます。ほかの物と同じです。

## 入れる前に

入れ子の DEsk（`./scripts/desk-window`）で、サニタイザー入りのビルド（MODDING の 10 節）で、次を全部試します。

- いちばん小さい窓と、とても大きい窓。
- 全画面と、戻す。
- 持つ、運ぶ、それぞれの縁を引く。
- Alt+Tab。
- 二つの画面（X の中の入れ子で `WLR_X11_OUTPUTS=2`）。
- 絵を取り上げる（`DESK_ART_DIR` を空のフォルダに）。紙になること。
- インストールした形（`meson install --destdir`）。絵が見つかること。
- シーンの中で描いた物を動かす以外のことをするなら、`WLR_RENDERER=gles2` と `pixman`。
- 画質 Low と、古い機械の速さ。

言葉は `TR()` を通し、`src/i18n.c` の表に全部の言語で入れます（`meson test words` が見ます）。
