/* desk-index: the DEsk Experience Index.
 *
 * How this machine does at what the desk asks of it, rated the way Windows
 * once rated a PC: five parts, each from 1.0 to 9.9, and a base score that
 * is the lowest of them, because the desk is only as quick as its slowest
 * part. The parts are what DEsk is made of:
 *
 *   Processor    the room tracer's arithmetic, and the table lookups that
 *                decoding a picture or laying out text come down to, on one
 *                thread and on all of them
 *   Memory       how fast a block the size of a few screens is copied, and
 *                how long a load from a random place takes
 *   Graphics     the desk's own cairo drawing over a 1440 x 900 frame -- a
 *                window's paper, a sheet of settings, a letter being folded
 *                and another screwed up, a binder sheet -- which is what
 *                makes the desk feel quick or slow
 *   3D graphics  the room tracer's march, on the graphics card, through
 *                the same offscreen GLES path the room is traced on (gpu.h);
 *                with no card the desk can use, on the processor, which is
 *                then what traces the room
 *   Disk         128 MB written, made sure of, and read back, where the
 *                desk keeps its cache (or in /var/tmp, when that cache is
 *                kept in memory)
 *
 * It is a sheet on the desk like any other: it asks DEsk for its frame
 * and prints onto that paper -- an instrument panel of five brass meters
 * whose needles swing while each part is measured, the report typed out
 * under it, and the base score stamped. The measuring is done on a thread
 * of its own, so the sheet stays live, and all of it takes under twenty
 * seconds. The last rating is kept in $XDG_STATE_HOME/desk/index.conf and
 * shown when the Index is opened again; with none kept, it rates at once.
 *
 *   desk-index           the sheet
 *   desk-index --text    the same measuring with no window: the figures on
 *                        standard output, and kept as the sheet keeps them */

#define _GNU_SOURCE
#include "compat.h"
#include "desk.h"
#include "fonts.h"
#include "i18n.h"
#include "sound.h"
#ifdef DESK_HAVE_GPU
#include "gpu.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#ifdef DESK_HAVE_WL_CURSOR
#include <wayland-cursor.h>
#endif

#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"

/* ---- the words ----
 *
 * The Index's own words, and the same in each language the desk speaks
 * (i18n.h's order: en, ja, zh, ko, es, fr, de). They are
 * kept here and not in the desk's table (i18n.c): nothing else prints them.
 * A word that is not here is looked for there, and failing that is left in
 * English. */

static const char *const WORDS[][DESK_LANG_COUNT] = {
	{ "DEsk Experience Index",
		"DEsk エクスペリエンス インデックス",
		"DEsk 体验指数",
		"DEsk 체험 지수",
		"Índice de experiencia de DEsk",
		"Indice d’expérience DEsk",
		"DEsk-Leistungsindex" },
	/* the plates under the meters */
	{ "PROCESSOR",
		"プロセッサ",
		"处理器",
		"프로세서",
		"PROCESADOR",
		"PROCESSEUR",
		"PROZESSOR" },
	{ "MEMORY", "メモリ", "内存", "메모리", "MEMORIA", "MÉMOIRE", "SPEICHER" },
	{ "GRAPHICS", "グラフィックス", "图形", "그래픽", "GRÁFICOS", "GRAPHISMES", "GRAFIK" },
	{ "3D GRAPHICS",
		"3D グラフィックス",
		"3D 图形",
		"3D 그래픽",
		"GRÁFICOS 3D",
		"GRAPHISMES 3D",
		"3D-GRAFIK" },
	{ "DISK", "ディスク", "磁盘", "디스크", "DISCO", "DISQUE", "DATENTRÄGER" },
	/* and the report's rows */
	{ "Processor",
		"プロセッサ",
		"处理器",
		"프로세서",
		"Procesador",
		"Processeur",
		"Prozessor" },
	{ "Memory", "メモリ", "内存", "메모리", "Memoria", "Mémoire", "Speicher" },
	{ "Graphics", "グラフィックス", "图形", "그래픽", "Gráficos", "Graphismes", "Grafik" },
	{ "3D graphics",
		"3D グラフィックス",
		"3D 图形",
		"3D 그래픽",
		"Gráficos 3D",
		"Graphismes 3D",
		"3D-Grafik" },
	{ "Disk", "ディスク", "磁盘", "디스크", "Disco", "Disque", "Datenträger" },
	/* the figures behind each score */
	{ "one thread %.1f, all %d threads %.1f",
		"1 スレッド %.1f、全 %d スレッド %.1f",
		"单线程 %.1f，全部 %d 线程 %.1f",
		"1스레드 %.1f, 전체 %d스레드 %.1f",
		"un hilo %.1f, los %d hilos %.1f",
		"un thread %.1f, les %d threads %.1f",
		"ein Thread %.1f, alle %d Threads %.1f" },
	{ "one thread %.1f, all %d threads %.1f (held to %.1f CPUs)",
		"1 スレッド %.1f、全 %d スレッド %.1f（%.1f CPU 分に制限）",
		"单线程 %.1f，全部 %d 线程 %.1f（限制为 %.1f 个 CPU）",
		"1스레드 %.1f, 전체 %d스레드 %.1f(CPU %.1f개로 제한)",
		"un hilo %.1f, los %d hilos %.1f (limitado a %.1f CPU)",
		"un thread %.1f, les %d threads %.1f (limité à %.1f CPU)",
		"ein Thread %.1f, alle %d Threads %.1f (auf %.1f CPUs begrenzt)" },
	{ "copies %.1f GB/s, %.0f ns to a random place, %.1f GB in all",
		"コピー %.1f GB/s、ランダムな読み出し %.0f ns、合計 %.1f GB",
		"复制 %.1f GB/s，随机读取 %.0f ns，共 %.1f GB",
		"복사 %.1f GB/s, 임의 읽기 %.0f ns, 모두 %.1f GB",
		"copia %.1f GB/s, %.0f ns a un lugar al azar, %.1f GB en total",
		"copie %.1f Go/s, %.0f ns vers un endroit au hasard, %.1f Go en tout",
		"kopiert %.1f GB/s, %.0f ns an eine zufällige Stelle, %.1f GB insgesamt" },
	{ "the test desk at 1440 \xc3\x97 900 drawn in %.1f ms",
		"1440×900 の試験用の机を %.1f ms で描画",
		"1440 × 900 的测试书桌用 %.1f ms 画完",
		"1440 × 900 시험용 책상을 %.1f ms에 그림",
		"el escritorio de prueba a 1440 × 900 dibujado en %.1f ms",
		"le bureau d’essai en 1440 × 900 dessiné en %.1f ms",
		"der Test-Schreibtisch in 1440 × 900 in %.1f ms gezeichnet" },
	{ "%s: %.0f million pixels of the room's march a second",
		"%s：部屋の追跡を毎秒 %.0f 百万ピクセル",
		"%s：每秒追踪房间 %.0f 百万像素",
		"%s: 방 추적 초당 %.0f백만 픽셀",
		"%s: %.0f millones de píxeles del trazado de la habitación por segundo",
		"%s\xc2\xa0: %.0f millions de pixels du tracé de la pièce par seconde",
		"%s: %.0f Millionen Pixel der Raumverfolgung pro Sekunde" },
	{ "%s answered wrongly: the processor marches %.1f million pixels a second",
		"%s の結果が正しくありません。プロセッサで毎秒 %.1f 百万ピクセル",
		"%s 的结果不正确：处理器每秒 %.1f 百万像素",
		"%s의 결과가 올바르지 않습니다: 프로세서로 초당 %.1f백만 픽셀",
		"%s respondió mal: el procesador traza %.1f millones de píxeles por segundo",
		"%s a mal répondu\xc2\xa0: le processeur trace %.1f millions de pixels par seconde",
		"%s hat falsch geantwortet: der Prozessor verfolgt %.1f Millionen Pixel pro "
		"Sekunde" },
	{ "no graphics card the desk can use: the processor marches %.1f million pixels a second",
		"机が使えるグラフィックカードがありません。プロセッサで毎秒 %.1f 百万ピクセル",
		"没有书桌可用的显卡：处理器每秒 %.1f 百万像素",
		"책상이 쓸 수 있는 그래픽 카드가 없습니다: 프로세서로 초당 %.1f백만 픽셀",
		"ninguna tarjeta gráfica que el escritorio pueda usar: el procesador traza %.1f "
		"millones de píxeles por segundo",
		"aucune carte graphique utilisable par le bureau\xc2\xa0: le processeur trace %.1f "
		"millions de pixels par seconde",
		"keine Grafikkarte, die der Schreibtisch nutzen kann: der Prozessor verfolgt "
		"%.1f Millionen Pixel pro Sekunde" },
	{ "copies %.1f GB/s, %.0f ns to a random place; %.1f GB holds it to %.1f",
		"コピー %.1f GB/s、ランダムな読み出し %.0f ns、容量 %.1f GB のため %.1f まで",
		"复制 %.1f GB/s，随机读取 %.0f ns；容量 %.1f GB，因此最多 %.1f",
		"복사 %.1f GB/s, 임의 읽기 %.0f ns; 용량 %.1f GB라서 %.1f까지",
		"copia %.1f GB/s, %.0f ns a un lugar al azar; %.1f GB lo limitan a %.1f",
		"copie %.1f Go/s, %.0f ns vers un endroit au hasard\xc2\xa0; %.1f Go le limitent à %.1f",
		"kopiert %.1f GB/s, %.0f ns an eine zufällige Stelle; %.1f GB begrenzen es auf "
		"%.1f" },
	{ "writes %.0f MB/s, reads %.0f MB/s",
		"書き込み %.0f MB/s、読み込み %.0f MB/s",
		"写入 %.0f MB/s，读取 %.0f MB/s",
		"쓰기 %.0f MB/s, 읽기 %.0f MB/s",
		"escribe %.0f MB/s, lee %.0f MB/s",
		"écrit %.0f Mo/s, lit %.0f Mo/s",
		"schreibt %.0f MB/s, liest %.0f MB/s" },
	{ "writes %.0f MB/s, reads %.0f MB/s (kept in memory, not on a disk)",
		"書き込み %.0f MB/s、読み込み %.0f MB/s（ディスクではなくメモリ上）",
		"写入 %.0f MB/s，读取 %.0f MB/s（在内存中，不在磁盘上）",
		"쓰기 %.0f MB/s, 읽기 %.0f MB/s(디스크가 아니라 메모리 위)",
		"escribe %.0f MB/s, lee %.0f MB/s (en memoria, no en un disco)",
		"écrit %.0f Mo/s, lit %.0f Mo/s (en mémoire, pas sur un disque)",
		"schreibt %.0f MB/s, liest %.0f MB/s (im Speicher, nicht auf einem Datenträger)" },
	/* what each score means at the desk */
	{ "Programs open briskly; the room's first trace goes by unnoticed.",
		"プログラムはきびきび開き、部屋の最初の描画も気になりません。",
		"程序打开很快；房间的第一次绘制也不会让人察觉。",
		"프로그램이 빠르게 열리고, 방을 처음 그리는 것도 눈치채지 못합니다.",
		"Los programas se abren con soltura; el primer trazado de la habitación pasa "
		"desapercibido.",
		"Les programmes s’ouvrent vivement\xc2\xa0; le premier tracé de la pièce passe inaperçu.",
		"Programme öffnen sich zügig; das erste Zeichnen des Raums fällt nicht auf." },
	{ "Comfortable for everyday work; heavy programs take a moment.",
		"普段の作業は快適。重いプログラムは少し待ちます。",
		"日常工作很舒适；大型程序需要稍等。",
		"평소 작업은 쾌적합니다. 무거운 프로그램은 잠시 기다립니다.",
		"Cómodo para el trabajo diario; los programas pesados tardan un momento.",
		"Confortable au quotidien\xc2\xa0; les gros programmes prennent un moment.",
		"Angenehm für die tägliche Arbeit; schwere Programme brauchen einen Moment." },
	{ "Fine for writing and browsing; heavy programs keep it busy.",
		"文書やブラウザには十分。重いプログラムでは手一杯です。",
		"写文档和上网足够；大型程序会让它很忙。",
		"문서 작성과 웹에는 충분합니다. 무거운 프로그램에서는 벅찹니다.",
		"Bien para escribir y navegar; los programas pesados lo mantienen ocupado.",
		"Bien pour écrire et naviguer\xc2\xa0; les gros programmes l’occupent.",
		"Gut zum Schreiben und Surfen; schwere Programme lasten ihn aus." },
	{ "Slow going: expect to wait for programs to open.",
		"動作は重め。プログラムが開くのを待つことになります。",
		"比较慢：打开程序需要等待。",
		"느린 편입니다. 프로그램이 열리기를 기다리게 됩니다.",
		"Va lento: habrá que esperar a que se abran los programas.",
		"C’est lent\xc2\xa0: attendez-vous à patienter à l’ouverture des programmes.",
		"Langsam: rechnen Sie mit Wartezeit beim Öffnen von Programmen." },
	{ "Room for the desk, a browser and plenty more besides.",
		"机とブラウザ、さらに多くを同時に開いても余裕があります。",
		"书桌、浏览器，再加上很多别的也有余裕。",
		"책상과 브라우저, 그 밖에 많은 것을 함께 열어도 여유가 있습니다.",
		"Sitio para el escritorio, un navegador y mucho más.",
		"De la place pour le bureau, un navigateur et bien plus encore.",
		"Platz für den Schreibtisch, einen Browser und vieles mehr." },
	{ "Enough for the desk and the usual programs side by side.",
		"机といつものプログラムを並べて使うには十分です。",
		"书桌和常用程序同时使用足够。",
		"책상과 평소 프로그램을 나란히 쓰기에 충분합니다.",
		"Suficiente para el escritorio y los programas habituales a la vez.",
		"Assez pour le bureau et les programmes habituels côte à côte.",
		"Genug für den Schreibtisch und die üblichen Programme nebeneinander." },
	{ "The desk and a browser fit; much more than that will drag.",
		"机とブラウザは収まりますが、それ以上は重くなります。",
		"书桌和浏览器放得下；再多就会变慢。",
		"책상과 브라우저는 들어가지만, 그 이상은 느려집니다.",
		"Caben el escritorio y un navegador; mucho más lo hará ir lento.",
		"Le bureau et un navigateur tiennent\xc2\xa0; bien plus le ralentira.",
		"Schreibtisch und Browser passen; viel mehr wird zäh." },
	{ "Too little for comfort: the desk will be short of room.",
		"快適に使うには足りません。机が窮屈になります。",
		"不够舒适：书桌会显得局促。",
		"쾌적하게 쓰기에는 부족합니다. 책상이 비좁아집니다.",
		"Demasiado poco para ir cómodo: al escritorio le faltará espacio.",
		"Trop peu pour être à l’aise\xc2\xa0: le bureau manquera de place.",
		"Zu wenig für angenehmes Arbeiten: dem Schreibtisch fehlt Platz." },
	{ "Paper folds, crumples and turns without a stutter.",
		"紙を折る・丸める・めくる動きが途切れず滑らかです。",
		"纸张的折叠、揉皱和翻页都很流畅。",
		"종이를 접고 구기고 넘기는 움직임이 끊김 없이 매끄럽습니다.",
		"El papel se pliega, se arruga y se pasa sin un tropiezo.",
		"Le papier se plie, se froisse et se tourne sans à-coups.",
		"Papier faltet, knüllt und blättert ohne Ruckeln." },
	{ "Smooth, though a fold may now and then skip a frame.",
		"滑らかですが、折るときにときどきコマが飛びます。",
		"很流畅，不过折纸时偶尔会掉帧。",
		"매끄럽지만, 접을 때 가끔 프레임이 건너뜁니다.",
		"Fluido, aunque un pliegue puede saltarse algún fotograma.",
		"Fluide, même si un pliage saute parfois une image.",
		"Flüssig, auch wenn beim Falten ab und zu ein Bild fehlt." },
	{ "Folding and the bookcase will be visibly jerky at times.",
		"紙を折るときや本棚では、ときどき動きがカクつきます。",
		"折纸和书架有时会明显卡顿。",
		"종이를 접을 때나 책장에서는 가끔 눈에 띄게 끊깁니다.",
		"El plegado y la estantería irán a tirones a veces.",
		"Le pliage et la bibliothèque seront parfois saccadés.",
		"Falten und das Bücherregal ruckeln manchmal sichtbar." },
	{ "The desk's paper will move in fits and starts.",
		"机の紙の動きはぎこちなくなります。",
		"书桌上的纸张会动得断断续续。",
		"책상의 종이가 뚝뚝 끊기며 움직입니다.",
		"El papel del escritorio se moverá a trompicones.",
		"Le papier du bureau bougera par à-coups.",
		"Das Papier des Schreibtischs bewegt sich stockend." },
	{ "The room is traced on the graphics card in seconds.",
		"部屋はグラフィックカードで数秒のうちに描かれます。",
		"房间在显卡上几秒钟就能画完。",
		"방은 그래픽 카드에서 몇 초 만에 그려집니다.",
		"La habitación se traza en la tarjeta gráfica en segundos.",
		"La pièce est tracée sur la carte graphique en quelques secondes.",
		"Der Raum wird auf der Grafikkarte in Sekunden gezeichnet." },
	{ "The room is traced on the graphics card in well under a minute.",
		"部屋はグラフィックカードで 1 分かからずに描かれます。",
		"房间在显卡上不到一分钟就能画完。",
		"방은 그래픽 카드에서 1분이 채 안 되어 그려집니다.",
		"La habitación se traza en la tarjeta gráfica en bastante menos de un minuto.",
		"La pièce est tracée sur la carte graphique en bien moins d’une minute.",
		"Der Raum wird auf der Grafikkarte in deutlich unter einer Minute gezeichnet." },
	{ "The card traces the room slowly; it is kept, so only once.",
		"部屋の描画は遅めですが、保存されるので最初の一度だけです。",
		"显卡画房间比较慢；但会保存下来，所以只需一次。",
		"그래픽 카드가 방을 느리게 그리지만, 저장되므로 처음 한 번뿐입니다.",
		"La tarjeta traza la habitación despacio; se guarda, así que solo una vez.",
		"La carte trace la pièce lentement\xc2\xa0; elle est gardée, donc une seule fois.",
		"Die Karte zeichnet den Raum langsam; er wird aufbewahrt, also nur einmal." },
	{ "Barely up to the room: the processor may as well trace it.",
		"部屋の描画にはほとんど力不足です。",
		"勉强能画房间：还不如让处理器来画。",
		"방을 그리기에는 거의 힘이 부족합니다. 프로세서가 그려도 마찬가지입니다.",
		"Apenas da para la habitación: igual podría trazarla el procesador.",
		"À peine à la hauteur de la pièce\xc2\xa0: le processeur pourrait aussi bien la tracer.",
		"Dem Raum kaum gewachsen: der Prozessor könnte ihn genauso gut zeichnen." },
	{ "Traced on the processor instead, once per screen size, and kept.",
		"部屋は代わりにプロセッサが画面サイズごとに一度描き、保存します。",
		"改由处理器绘制，每种屏幕尺寸一次，并保存下来。",
		"대신 프로세서가 화면 크기마다 한 번 그리고 저장합니다.",
		"Se traza en el procesador, una vez por tamaño de pantalla, y se guarda.",
		"Tracée plutôt sur le processeur, une fois par taille d’écran, et gardée.",
		"Stattdessen auf dem Prozessor gezeichnet, einmal je Bildschirmgröße, und "
		"aufbewahrt." },
	{ "Programs and the room come off the disk at once.",
		"プログラムも部屋も、ディスクからすぐに読み込まれます。",
		"程序和房间都能从磁盘立刻读取。",
		"프로그램도 방도 디스크에서 바로 읽힙니다.",
		"Los programas y la habitación salen del disco al instante.",
		"Les programmes et la pièce sortent du disque aussitôt.",
		"Programme und Raum kommen sofort von der Platte." },
	{ "Programs start quickly; the room comes out of its cache in a moment.",
		"プログラムはすぐ起動し、部屋もキャッシュからすぐ戻ります。",
		"程序启动很快；房间也能从缓存中迅速取出。",
		"프로그램이 빨리 시작되고, 방도 캐시에서 금방 돌아옵니다.",
		"Los programas arrancan rápido; la habitación sale de su caché en un momento.",
		"Les programmes démarrent vite\xc2\xa0; la pièce sort de son cache en un instant.",
		"Programme starten schnell; der Raum kommt im Nu aus dem Cache." },
	{ "Programs take a few seconds to start the first time.",
		"プログラムの初回起動に数秒かかります。",
		"程序第一次启动需要几秒钟。",
		"프로그램이 처음 시작할 때 몇 초 걸립니다.",
		"Los programas tardan unos segundos en arrancar la primera vez.",
		"Les programmes mettent quelques secondes à démarrer la première fois.",
		"Programme brauchen beim ersten Start ein paar Sekunden." },
	{ "A slow disk: whatever is read for the first time keeps you waiting.",
		"遅いディスクです。初めて読むものは待たされます。",
		"磁盘较慢：第一次读取的东西都要等待。",
		"느린 디스크입니다. 처음 읽는 것은 기다리게 됩니다.",
		"Un disco lento: todo lo que se lee por primera vez hace esperar.",
		"Un disque lent\xc2\xa0: tout ce qui est lu pour la première fois fait attendre.",
		"Eine langsame Platte: was zum ersten Mal gelesen wird, lässt warten." },
	/* the base score */
	{ "BASE SCORE",
		"基本スコア",
		"基本分数",
		"기본 점수",
		"PUNTUACIÓN BASE",
		"NOTE DE BASE",
		"BASISWERT" },
	{ "This machine does everything the desk asks of it, easily.",
		"このマシンは、机の求めることを何でも楽にこなせます。",
		"这台机器能轻松完成书桌要求的一切。",
		"이 컴퓨터는 책상이 요구하는 모든 일을 거뜬히 해냅니다.",
		"Esta máquina hace con soltura todo lo que le pide el escritorio.",
		"Cette machine fait sans peine tout ce que lui demande le bureau.",
		"Dieser Rechner schafft alles, was der Schreibtisch verlangt, mühelos." },
	{ "Comfortable for everything the desk does.",
		"机のすることすべてに快適です。",
		"书桌做的一切都很舒适。",
		"책상이 하는 모든 일에 쾌적합니다.",
		"Cómodo para todo lo que hace el escritorio.",
		"Confortable pour tout ce que fait le bureau.",
		"Angenehm für alles, was der Schreibtisch tut." },
	{ "The desk runs well; some of its movements will be less than smooth.",
		"机はよく動きますが、一部の動きは滑らかさに欠けます。",
		"书桌运行良好；部分动作不够流畅。",
		"책상은 잘 동작하지만, 일부 움직임은 매끄럽지 않습니다.",
		"El escritorio va bien; algunos de sus movimientos no serán del todo fluidos.",
		"Le bureau fonctionne bien\xc2\xa0; certains de ses mouvements manqueront de fluidité.",
		"Der Schreibtisch läuft gut; manche Bewegungen sind nicht ganz flüssig." },
	{ "Below what the desk needs to feel right.",
		"机が本来の動きをするには力不足です。",
		"达不到书桌正常运行的要求。",
		"책상이 제대로 움직이기에는 부족합니다.",
		"Por debajo de lo que necesita el escritorio para ir bien.",
		"En dessous de ce qu’il faut au bureau pour bien tourner.",
		"Unter dem, was der Schreibtisch braucht, um sich richtig anzufühlen." },
	{ "The base score is the lowest of the five: the desk is only as quick as its slowest "
		"part.",
		"基本スコアは 5 つのうち最も低いスコアです。机の速さは、いちばん遅い部分で決まり"
		"ます。",
		"基本分数是五项中最低的一项：书桌的速度取决于最慢的部分。",
		"기본 점수는 다섯 가지 중 가장 낮은 점수입니다. 책상의 속도는 가장 느린 부분이 정"
		"합니다.",
		"La puntuación base es la más baja de las cinco: el escritorio solo es tan "
		"rápido como su parte más lenta.",
		"La note de base est la plus basse des cinq\xc2\xa0: le bureau n’est pas plus rapide "
		"que sa partie la plus lente.",
		"Der Basiswert ist der niedrigste der fünf: Der Schreibtisch ist nur so schnell "
		"wie sein langsamstes Teil." },
	{ "Held back by: %s.",
		"足を引っ張っているもの：%s",
		"拖后腿的是：%s。",
		"발목을 잡는 것: %s.",
		"Lo frena: %s.",
		"Freiné par\xc2\xa0: %s.",
		"Gebremst durch: %s." },
	{ "Other programs kept the machine %d%% busy while it was rated, so the scores may be "
		"low.",
		"評価中、ほかのプログラムがマシンを %d%% 使っていたため、スコアは低めに出ているか"
		"もしれません。",
		"评估时其他程序占用了 %d%% 的机器资源，所以分数可能偏低。",
		"평가하는 동안 다른 프로그램이 컴퓨터를 %d%% 사용하고 있어 점수가 낮게 나왔을 수 "
		"있습니다.",
		"Otros programas tuvieron la máquina ocupada al %d%% durante la evaluación, así "
		"que las puntuaciones pueden ser bajas.",
		"D’autres programmes occupaient la machine à %d\xc2\xa0%% pendant l’évaluation\xc2\xa0; les "
		"notes peuvent être basses.",
		"Andere Programme hielten den Rechner während der Bewertung zu %d\xc2\xa0%% "
		"beschäftigt; die Werte können niedrig sein." },
	{ "Rated %s.",
		"%s に評価しました。",
		"评估于 %s。",
		"%s에 평가했습니다.",
		"Evaluado el %s.",
		"Évalué le %s.",
		"Bewertet am %s." },
	{ "Not rated yet.",
		"まだ評価していません。",
		"尚未评估。",
		"아직 평가하지 않았습니다.",
		"Aún sin evaluar.",
		"Pas encore évalué.",
		"Noch nicht bewertet." },
	{ "%s, %d threads, %.1f GB of memory",
		"%s、%d スレッド、メモリ %.1f GB",
		"%s，%d 线程，内存 %.1f GB",
		"%s, %d스레드, 메모리 %.1f GB",
		"%s, %d hilos, %.1f GB de memoria",
		"%s, %d threads, %.1f Go de mémoire",
		"%s, %d Threads, %.1f GB Speicher" },
	/* while it measures */
	{ "Measuring the processor (1 of 5)",
		"プロセッサを測定中（1/5）",
		"正在测量处理器（1/5）",
		"프로세서 측정 중(1/5)",
		"Midiendo el procesador (1 de 5)",
		"Mesure du processeur (1 sur 5)",
		"Prozessor wird gemessen (1 von 5)" },
	{ "Measuring the memory (2 of 5)",
		"メモリを測定中（2/5）",
		"正在测量内存（2/5）",
		"메모리 측정 중(2/5)",
		"Midiendo la memoria (2 de 5)",
		"Mesure de la mémoire (2 sur 5)",
		"Speicher wird gemessen (2 von 5)" },
	{ "Measuring the graphics (3 of 5)",
		"グラフィックスを測定中（3/5）",
		"正在测量图形（3/5）",
		"그래픽 측정 중(3/5)",
		"Midiendo los gráficos (3 de 5)",
		"Mesure des graphismes (3 sur 5)",
		"Grafik wird gemessen (3 von 5)" },
	{ "Measuring the 3D graphics (4 of 5)",
		"3D グラフィックスを測定中（4/5）",
		"正在测量 3D 图形（4/5）",
		"3D 그래픽 측정 중(4/5)",
		"Midiendo los gráficos 3D (4 de 5)",
		"Mesure des graphismes 3D (4 sur 5)",
		"3D-Grafik wird gemessen (4 von 5)" },
	{ "Measuring the disk (5 of 5)",
		"ディスクを測定中（5/5）",
		"正在测量磁盘（5/5）",
		"디스크 측정 중(5/5)",
		"Midiendo el disco (5 de 5)",
		"Mesure du disque (5 sur 5)",
		"Datenträger wird gemessen (5 von 5)" },
	{ "Getting ready",
		"準備中",
		"正在准备",
		"준비 중",
		"Preparando",
		"Préparation",
		"Vorbereitung" },
	{ "Keep your hands off the machine for twenty seconds or so.",
		"20 秒ほど、マシンに触れずにお待ちください。",
		"请在约二十秒内不要碰电脑。",
		"20초 정도 컴퓨터에 손대지 말고 기다려 주세요.",
		"No toques la máquina durante unos veinte segundos.",
		"Ne touchez pas à la machine pendant une vingtaine de secondes.",
		"Lassen Sie den Rechner etwa zwanzig Sekunden lang in Ruhe." },
	{ "measuring", "測定中", "测量中", "측정 중", "midiendo", "mesure", "misst" },
	{ "Rate again",
		"もう一度評価する",
		"重新评估",
		"다시 평가",
		"Evaluar de nuevo",
		"Réévaluer",
		"Neu bewerten" },
	{ "Rate this machine",
		"このマシンを評価する",
		"评估这台机器",
		"이 컴퓨터 평가하기",
		"Evaluar esta máquina",
		"Évaluer cette machine",
		"Diesen Rechner bewerten" },
	{ "Stop", "中止", "停止", "중지", "Detener", "Arrêter", "Stopp" },
	{ "Stopped before it was done.",
		"途中で中止しました。",
		"在完成之前停止了。",
		"끝나기 전에 중지했습니다.",
		"Detenido antes de terminar.",
		"Arrêté avant la fin.",
		"Vor dem Ende gestoppt." },
	{ "Could not measure: %s.",
		"測定できませんでした：%s",
		"无法测量：%s。",
		"측정할 수 없습니다: %s.",
		"No se pudo medir: %s.",
		"Mesure impossible\xc2\xa0: %s.",
		"Messung nicht möglich: %s." },
};

static const char *W(const char *en)
{
	enum desk_lang l = desk_lang_get();
	if (l != DESK_LANG_EN) {
		for (size_t i = 0; i < sizeof WORDS / sizeof WORDS[0]; i++) {
			if (!strcmp(WORDS[i][DESK_LANG_EN], en)) {
				return WORDS[i][l] && *WORDS[i][l] ? WORDS[i][l] : en;
			}
		}
	}
	return TR(en);
}

/* ---- the parts, and what a score means ----
 *
 * Every part is rated the same way: a figure, higher the better, placed
 * between two machines. A weak laptop -- two cores, 4 GB of memory, the
 * Celeron or Core i3 of a few years ago with the graphics that came on the
 * chip and an eMMC or a slow SATA disk -- rates 3.5. A modern desktop rates
 * 8.0. Each doubling of the figure is worth the same number of points
 * between, above and below those two, so the scale is logarithmic, as
 * WEI's was; it is clipped to 1.0 .. 9.9 and cut, not rounded, to one
 * decimal, so that a 7.96 is a 7.9 and a score never flatters.
 *
 * The modern desktop's processor, memory and drawing are the machine these
 * were measured on: a Ryzen 7 7800X3D (eight of its threads, lent to a
 * virtual machine) with DDR5. Its graphics card and its disk are seen only
 * through the virtual machine, so for 3D graphics and the disk the modern
 * desktop is placed from what such hardware is published to do. The weak
 * laptop is placed against the modern desktop from published comparisons
 * of the two kinds of machine, part by part (the anchors below say how).
 * Two machines are not a survey, so a score is a place on a scale and not
 * a measurement to the decimal; but the places are where they are for a
 * reason, and the same machine rates the same from one run to the next to
 * within a tenth or two. */

enum { P_CPU, P_MEM, P_DRAW, P_GPU, P_DISK, N_PARTS };

static const char *const PART_PLATE[N_PARTS] = {
	"PROCESSOR", "MEMORY", "GRAPHICS", "3D GRAPHICS", "DISK",
};
static const char *const PART_NAME[N_PARTS] = {
	"Processor", "Memory", "Graphics", "3D graphics", "Disk",
};
static const char *const PART_KEY[N_PARTS] = {
	"processor", "memory", "graphics", "3d", "disk",
};
static const char *const PART_DOING[N_PARTS] = {
	"Measuring the processor (1 of 5)", "Measuring the memory (2 of 5)",
	"Measuring the graphics (3 of 5)", "Measuring the 3D graphics (4 of 5)",
	"Measuring the disk (5 of 5)",
};

#define SCORE_WEAK 3.5
#define SCORE_STRONG 8.0
#define SCORE_MIN 1.0
#define SCORE_MAX 9.9

/* Where figure x falls between the weak laptop's and the modern desktop's. */
static double rate_raw(double x, double weak, double strong)
{
	if (!(x > 0.0) || !(weak > 0.0) || !(strong > weak)) {
		return SCORE_MIN;
	}
	double s = SCORE_WEAK + (SCORE_STRONG - SCORE_WEAK) * log(x / weak) / log(strong / weak);
	return s < SCORE_MIN ? SCORE_MIN : s > SCORE_MAX ? SCORE_MAX : s;
}

/* The same, cut to the tenth it is shown as. */
static double rate(double x, double weak, double strong)
{
	return floor(rate_raw(x, weak, strong) * 10.0 + 1e-6) / 10.0;
}

static double cut(double s)
{
	s = floor(s * 10.0 + 1e-6) / 10.0;
	return s < SCORE_MIN ? SCORE_MIN : s > SCORE_MAX ? SCORE_MAX : s;
}

/* Processor, in helpings (cpu_helping) a second. On one thread the 7800X3D
 * does what is below; a Celeron N4020 does about a sixth of that and a
 * Core i3-7100U about a third, so the weak laptop is put at a quarter. On
 * all its threads the weak laptop does about a twelfth of what the eight
 * here do: it has two cores to their eight, each a quarter as quick, and
 * gets a little back from a second thread on each. */
#define CPU_ONE_STRONG 26000.0
#define CPU_ONE_WEAK (CPU_ONE_STRONG / 4.0)
#define CPU_ALL_STRONG 190000.0
#define CPU_ALL_WEAK (CPU_ALL_STRONG / 12.0)

/* Memory: a 64 MB block copied, in GB a second, on one thread, which is
 * how the desk copies (dual channel DDR5 against the single channel DDR4
 * a cheap laptop is sold with: a quarter); and the time a load from a
 * random place in 128 MB takes, in nanoseconds, worth a quarter of the
 * score (the laptop's slower memory, and its smaller caches and TLBs,
 * take about half as long again). */
#define MEM_GBS_STRONG 26.0
#define MEM_GBS_WEAK (MEM_GBS_STRONG / 4.0)
#define MEM_NS_STRONG 100.0
#define MEM_NS_WEAK (MEM_NS_STRONG * 1.6)

/* A machine with less memory than the desk wants for what it is used for
 * is no better than that, however quick its memory is: the score is held
 * under these, by the memory there is (or that a control group allows). */
static double mem_cap(double gib)
{
	return gib <= 0.0 ? SCORE_MAX : gib < 1.5 ? 1.9 : gib < 3.0 ? 2.9 : gib < 6.0 ? 4.9
		: gib < 12.0 ? 6.9 : SCORE_MAX;
}

/* Graphics: milliseconds for one test desk's worth of drawing (draw_frame).
 * It is cairo on one thread, so it goes with one core's speed and with the
 * memory's: the weak laptop takes four times as long. */
#define DRAW_MS_STRONG 27.0
#define DRAW_MS_WEAK (DRAW_MS_STRONG * 4.0)

/* 3D graphics: million pixels of the march (MARCH_SRC) a second. A pixel
 * of it takes 19 steps on average, of about 45 floating point operations
 * each, and the half of them that hit something are shaded with six more
 * looks at the field: about MARCH_FLOP operations in all. Graphics
 * hardware runs a loop like that, its pixels leaving it at different
 * steps, at about a third of what it says it can do. The weak laptop's
 * graphics (Intel UHD 600 to 620: 0.2 to 0.4 TFLOPS) are put at 0.3, the
 * modern desktop's (a GeForce RTX 3060 or a Radeon RX 6600: 9 to 13) at
 * 10. Not measured here: the machine these were measured on lends its
 * card to the virtual machine through VMware's SVGA3D, which comes out
 * between the two, at about 400. A rasterizer on the processor is not
 * graphics hardware, whatever it manages: it is held under SOFT_CAP. */
#define MARCH_FLOP 1000.0
#define GPU_MPX(tflops) ((tflops) * 1e12 / 3.0 / MARCH_FLOP / 1e6)
#define GPU_MPX_WEAK GPU_MPX(0.3)
#define GPU_MPX_STRONG GPU_MPX(10.0)
#define SOFT_CAP 2.9

/* Disk: the mean of writing (made sure of with fsync) and reading back,
 * in MB a second. eMMC, or the slow SATA disk of the weak laptop, does
 * 130 or so; a modern NVMe disk does 2500. */
#define DISK_MBS_WEAK 130.0
#define DISK_MBS_STRONG 2500.0

enum { GPU_NONE, GPU_SOFT, GPU_HARD, GPU_WRONG };
static const char *const GPU_KIND[] = { "none", "software", "hardware", "wrong" };

/* One rating: the scores, and the figures behind them. */
struct rating {
	time_t when;              /* 0: not rated */
	double score[N_PARTS];    /* 0: not measured */
	double base;
	double cpu_one, cpu_all;  /* the processor's two halves, as scores */
	int threads;
	double cpu_quota;         /* CPUs' worth a control group allows; 0 for no limit */
	char cpu_name[128];
	double mem_gbs, mem_ns, ram_gib;
	double draw_ms;
	double gpu_mpx;
	int gpu_kind;
	char gpu_name[128];
	bool gpu_own;             /* graphics of its own, not built into the processor (desk_gl_name_own) */
	double disk_w, disk_r;
	bool disk_in_memory;
	double busy;              /* how busy other programs kept the machine, 0..1 */
};

static void rating_base(struct rating *r)
{
	r->base = 0.0;
	for (int i = 0; i < N_PARTS; i++) {
		if (r->score[i] > 0.0 && (r->base == 0.0 || r->score[i] < r->base)) {
			r->base = r->score[i];
		}
	}
}

/* the part that sets the base score: the lowest, the first of equals */
static int rating_lowest(const struct rating *r)
{
	int low = -1;
	for (int i = 0; i < N_PARTS; i++) {
		if (r->score[i] > 0.0 && (low < 0 || r->score[i] < r->score[low])) {
			low = i;
		}
	}
	return low;
}

/* ---- the machine ---- */

static double now_s(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec * 1e-9;
}

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t') {
		s++;
	}
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n'
			|| s[n - 1] == '\r')) {
		s[--n] = '\0';
	}
	return s;
}

/* What the processor calls itself. */
static void cpu_name(char *out, size_t n)
{
	snprintf(out, n, "%s", "processor");
	FILE *f = fopen("/proc/cpuinfo", "re");
	if (!f) {
		return;
	}
	char line[512];
	static const char *const KEYS[] = { "model name", "Model", "Hardware", "cpu model" };
	int got = (int)(sizeof KEYS / sizeof KEYS[0]);
	while (fgets(line, sizeof line, f)) {
		char *colon = strchr(line, ':');
		if (!colon) {
			continue;
		}
		*colon = '\0';
		char *key = trim(line);
		for (int k = 0; k < got; k++) {
			if (!strcmp(key, KEYS[k])) {
				snprintf(out, n, "%s", trim(colon + 1));
				got = k;
				break;
			}
		}
		if (got == 0) {
			break;
		}
	}
	fclose(f);
}

/* How many threads this may run on: what it is allowed (taskset, a
 * container), not what the machine has. */
static int cpu_threads(void)
{
	cpu_set_t set;
	if (sched_getaffinity(0, sizeof set, &set) == 0) {
		int n = CPU_COUNT(&set);
		if (n > 0) {
			return n;
		}
	}
	long n = sysconf(_SC_NPROCESSORS_ONLN);
	return n > 0 ? (int)n : 1;
}

/* This process's control group, walked from its own directory up to the
 * root: the least that any of them allows of `file`, read by `parse`. */
static double cgroup_least(const char *file, double (*parse)(const char *))
{
	char path[512] = "";
	FILE *f = fopen("/proc/self/cgroup", "re");
	if (f) {
		char line[512];
		while (fgets(line, sizeof line, f)) {
			if (!strncmp(line, "0::", 3)) {
				snprintf(path, sizeof path, "%s", trim(line + 3));
			}
		}
		fclose(f);
	}
	double least = 0.0;
	for (;;) {
		char p[700];
		snprintf(p, sizeof p, "/sys/fs/cgroup%s/%s", path, file);
		FILE *g = fopen(p, "re");
		if (g) {
			char v[128];
			if (fgets(v, sizeof v, g)) {
				double x = parse(trim(v));
				if (x > 0.0 && (least == 0.0 || x < least)) {
					least = x;
				}
			}
			fclose(g);
		}
		char *slash = strrchr(path, '/');
		if (!slash || !path[0]) {
			break;
		}
		*slash = '\0';
	}
	return least;
}

static double parse_bytes(const char *v)
{
	return strcmp(v, "max") ? atof(v) : 0.0;
}

static double parse_quota(const char *v)
{
	double q = 0.0, p = 0.0;
	if (strncmp(v, "max", 3) && sscanf(v, "%lf %lf", &q, &p) == 2 && p > 0.0) {
		return q / p;
	}
	return 0.0;
}

/* The memory there is, in GiB: the machine's, or what its control group
 * allows, whichever is less. */
static double ram_gib(void)
{
	double total = 0.0;
	FILE *f = fopen("/proc/meminfo", "re");
	if (f) {
		char line[256];
		while (fgets(line, sizeof line, f)) {
			double kb;
			if (sscanf(line, "MemTotal: %lf kB", &kb) == 1) {
				total = kb * 1024.0;
				break;
			}
		}
		fclose(f);
	}
	double cg = cgroup_least("memory.max", parse_bytes);
	if (cg > 0.0 && (total == 0.0 || cg < total)) {
		total = cg;
	}
	return total / (1024.0 * 1024.0 * 1024.0);
}

/* ---- the measuring, and what it tells the sheet ---- */

struct meter {
	pthread_mutex_t lock;
	int part;                 /* being measured; -1 getting ready, N_PARTS done */
	double frac;              /* how far through it, 0..1 */
	double live;              /* the score it is heading for; 0 not known yet */
	struct rating r;          /* as far as it has got */
	bool finished, stopped;
	int failed;               /* the part that could not be measured, or -1 */
	atomic_bool stop;         /* asked to stop */
	int wake;                 /* written to when anything changes; -1 none */
	double settle;            /* seconds to wait before the first part */
	bool text;                /* desk-index --text: what it is doing, on stderr */
};

static void meter_poke(struct meter *m)
{
	if (m->wake >= 0) {
		char c = 1;
		ssize_t n = write(m->wake, &c, 1);
		(void)n;
	}
}

static void meter_say(struct meter *m, int part, double frac, double live)
{
	pthread_mutex_lock(&m->lock);
	if (m->part != part) {
		m->live = 0.0;
	}
	m->part = part;
	m->frac = frac < 0.0 ? 0.0 : frac > 1.0 ? 1.0 : frac;
	if (live > 0.0) {
		m->live = live;
	}
	pthread_mutex_unlock(&m->lock);
	meter_poke(m);
}

static bool meter_stopped(struct meter *m)
{
	return atomic_load(&m->stop);
}

/* Up to `secs` of doing nothing, or less if asked to stop. */
static void meter_wait(struct meter *m, double secs)
{
	for (double t0 = now_s(); now_s() - t0 < secs && !meter_stopped(m);) {
		struct timespec nap = { 0, 25 * 1000 * 1000 };
		nanosleep(&nap, NULL);
	}
}

/* The machine's time so far, all of it and the part of it spent idle. */
static bool cpu_ticks(unsigned long long *idle, unsigned long long *total)
{
	FILE *f = fopen("/proc/stat", "re");
	if (!f) {
		return false;
	}
	unsigned long long v[8] = { 0 };
	int n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2],
		&v[3], &v[4], &v[5], &v[6], &v[7]);
	fclose(f);
	if (n < 4) {
		return false;
	}
	*idle = v[3] + v[4];
	*total = 0;
	for (int i = 0; i < 8; i++) {
		*total += v[i];
	}
	return true;
}

/* How busy the machine is with other things, over `secs` before anything
 * is measured: the share of its time that was not idle. */
static double machine_busy(struct meter *m, double secs)
{
	unsigned long long i0, t0, i1, t1;
	if (!cpu_ticks(&i0, &t0)) {
		return 0.0;
	}
	meter_wait(m, secs);
	if (!cpu_ticks(&i1, &t1) || t1 <= t0) {
		return 0.0;
	}
	double busy = 1.0 - (double)(i1 - i0) / (double)(t1 - t0);
	return busy < 0.0 ? 0.0 : busy > 1.0 ? 1.0 : busy;
}

/* ---- the march ----
 *
 * The shape of the room tracer: a ray from the eye through each pixel of a
 * 1440 x 900 frame, marched through a distance field until it is close
 * enough to something, and that something shaded. Three spheres and a
 * plane, as desk-gputest has it, because what is being measured is the
 * marching and not the scene. The processor runs some of its pixels (in
 * double precision, as the room's C tracer does) and the graphics card
 * all of them; and the card's answers are checked against these. */

#define FRAME_W 1440
#define FRAME_H 900
#define MARCH_STEPS 260

static double march_map(double x, double y, double z)
{
	static const double S[3][4] = {
		{ -1.2, 0.0, 0.0, 0.9 }, { 0.4, 0.3, 0.6, 0.7 }, { 1.6, -0.2, -0.4, 0.8 },
	};
	double d = y + 1.0;
	for (int i = 0; i < 3; i++) {
		double dx = x - S[i][0], dy = y - S[i][1], dz = z - S[i][2];
		double l = sqrt(dx * dx + dy * dy + dz * dz) - S[i][3];
		d = l < d ? l : d;
	}
	return d;
}

/* The red of pixel (px, py) -- py counted up from the bottom, as the
 * graphics card counts -- exactly as MARCH_SRC works it out. */
static double march_px(int px, int py)
{
	double ux = ((px + 0.5) / FRAME_W) * 2.0 - 1.0;
	double uy = ((py + 0.5) / FRAME_H) * 2.0 - 1.0;
	ux *= (double)FRAME_W / FRAME_H;
	double rl = sqrt(ux * ux + uy * uy + 1.6 * 1.6);
	double rd[3] = { ux / rl, uy / rl, -1.6 / rl };
	double t = 0.02;
	for (int i = 0; i < MARCH_STEPS && t < 40.0; i++) {
		double p[3] = { rd[0] * t, 0.8 + rd[1] * t, 5.0 + rd[2] * t };
		double d = march_map(p[0], p[1], p[2]);
		if (d < 0.0018) {
			double e = 0.0015, n[3];
			n[0] = march_map(p[0] + e, p[1], p[2]) - march_map(p[0] - e, p[1], p[2]);
			n[1] = march_map(p[0], p[1] + e, p[2]) - march_map(p[0], p[1] - e, p[2]);
			n[2] = march_map(p[0], p[1], p[2] + e) - march_map(p[0], p[1], p[2] - e);
			double nl = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) + 1e-12;
			n[0] /= nl;
			n[1] /= nl;
			n[2] /= nl;
			double ll = sqrt(0.25 + 1.0 + 0.16);
			double l[3] = { 0.5 / ll, 1.0 / ll, 0.4 / ll };
			double diff = n[0] * l[0] + n[1] * l[1] + n[2] * l[2];
			diff = diff > 0.0 ? diff : 0.0;
			/* reflect(-l, n), against the way back to the eye */
			double dn = -(n[0] * l[0] + n[1] * l[1] + n[2] * l[2]);
			double rf[3] = { -l[0] - 2.0 * dn * n[0], -l[1] - 2.0 * dn * n[1],
				-l[2] - 2.0 * dn * n[2] };
			double s = -(rf[0] * rd[0] + rf[1] * rd[1] + rf[2] * rd[2]);
			double spec = pow(s > 0.0 ? s : 0.0, 48.0);
			return 0.62 * diff + 0.4 * spec;
		}
		t += d > 0.0009 ? d : 0.0009;
	}
	return 0.02;
}

/* ---- Processor ----
 *
 * A helping is a fixed amount of the desk's kind of work: CPU_RAYS pixels
 * of the march, spread over the frame so that a helping costs what the
 * frame does on average, and CPU_LOOKUPS hashed loads and stores with a
 * branch on each in a 64 KB table, which is what inflating a picture or
 * laying out a line of text mostly is. Helpings a second on one thread,
 * and on every thread there is, each with a table of its own. */

#define CPU_TABLE 16384
#define CPU_RAYS 24
#define CPU_LOOKUPS 8192
#define CPU_ONE_S 1.4
#define CPU_ALL_S 1.6

struct cpu_lane {
	pthread_t th;
	bool started;
	uint32_t table[CPU_TABLE];
	uint32_t seed;
	unsigned ray;
	atomic_long helpings;
	double sink;
	const atomic_int *go;
	double until;
	struct meter *m;
};

static void cpu_helping(struct cpu_lane *l)
{
	double acc = 0.0;
	for (int i = 0; i < CPU_RAYS; i++) {
		unsigned k = (l->ray++ * 2654435761u) % (FRAME_W * FRAME_H);
		acc += march_px((int)(k % FRAME_W), (int)(k / FRAME_W));
	}
	uint32_t h = l->seed, sum = 0;
	for (int k = 0; k < CPU_LOOKUPS; k++) {
		h ^= h << 13;
		h ^= h >> 17;
		h ^= h << 5;
		uint32_t *t = &l->table[h & (CPU_TABLE - 1)];
		if ((*t ^ h) & 1u) {
			*t += h >> 7;
		} else {
			*t ^= h * 2654435761u;
		}
		sum += *t;
	}
	l->seed = h;
	l->sink += acc + sum * 1e-12;
}

static void *cpu_lane_run(void *arg)
{
	struct cpu_lane *l = arg;
	while (!atomic_load(l->go)) {
		sched_yield();
	}
	while (now_s() < l->until && !meter_stopped(l->m)) {
		cpu_helping(l);
		atomic_fetch_add(&l->helpings, 1);
	}
	return NULL;
}

/* Helpings a second over n lanes run for `secs`, the needle following the
 * count as it goes; 0 when stopped. */
static double cpu_race(struct meter *m, int n, double secs, double frac0, double frac1,
	double weak, double strong, double other)
{
	struct cpu_lane *lanes = calloc((size_t)n, sizeof *lanes);
	if (!lanes) {
		return 0.0;
	}
	atomic_int go = 0;
	for (int i = 0; i < n; i++) {
		lanes[i].seed = 0x9e3779b9u * (uint32_t)(i + 1);
		lanes[i].ray = (unsigned)i * 7919u;
		lanes[i].go = &go;
		lanes[i].m = m;
		for (int k = 0; k < CPU_TABLE; k++) {
			lanes[i].table[k] = (uint32_t)k * 2246822519u;
		}
	}
	/* every lane but the first on a thread of its own; the first is this one,
	 * when there is only one -- otherwise this one keeps count */
	int threads = n > 1 ? n : 0;
	for (int i = 0; i < threads; i++) {
		lanes[i].started = pthread_create(&lanes[i].th, NULL, cpu_lane_run, &lanes[i]) == 0;
	}
	double t0 = now_s();
	for (int i = 0; i < n; i++) {
		lanes[i].until = t0 + secs;
	}
	atomic_store(&go, 1);
	double last = t0;
	if (threads == 0) {
		while (now_s() < lanes[0].until && !meter_stopped(m)) {
			cpu_helping(&lanes[0]);
			long done = atomic_fetch_add(&lanes[0].helpings, 1) + 1;
			double t = now_s();
			if (t - last > 0.05) {
				last = t;
				double s = rate_raw(done / (t - t0), weak, strong);
				meter_say(m, P_CPU, frac0 + (frac1 - frac0) * (t - t0) / secs,
					other > 0.0 ? (s + other) * 0.5 : s);
			}
		}
	} else {
		while (now_s() < t0 + secs && !meter_stopped(m)) {
			struct timespec nap = { 0, 50 * 1000 * 1000 };
			nanosleep(&nap, NULL);
			long done = 0;
			for (int i = 0; i < n; i++) {
				done += atomic_load(&lanes[i].helpings);
			}
			double t = now_s();
			double s = rate_raw(done / (t - t0), weak, strong);
			meter_say(m, P_CPU, frac0 + (frac1 - frac0) * (t - t0) / secs,
				other > 0.0 ? (s + other) * 0.5 : s);
		}
	}
	long total = 0;
	for (int i = 0; i < n; i++) {
		if (threads && lanes[i].started) {
			pthread_join(lanes[i].th, NULL);
		}
		/* a lane that never started did nothing, and is counted as nothing */
		total += atomic_load(&lanes[i].helpings);
	}
	double took = now_s() - t0;
	free(lanes);
	return meter_stopped(m) || took <= 0.0 ? 0.0 : total / took;
}

static bool measure_cpu(struct meter *m, struct rating *r)
{
	r->threads = cpu_threads();
	r->cpu_quota = cgroup_least("cpu.max", parse_quota);
	cpu_name(r->cpu_name, sizeof r->cpu_name);
	double one = cpu_race(m, 1, CPU_ONE_S, 0.0, 0.45, CPU_ONE_WEAK, CPU_ONE_STRONG, 0.0);
	if (one <= 0.0) {
		return false;
	}
	r->cpu_one = rate(one, CPU_ONE_WEAK, CPU_ONE_STRONG);
	int n = r->threads < 256 ? r->threads : 256;
	double all = cpu_race(m, n, CPU_ALL_S, 0.5, 1.0, CPU_ALL_WEAK, CPU_ALL_STRONG,
		rate_raw(one, CPU_ONE_WEAK, CPU_ONE_STRONG));
	if (all <= 0.0) {
		return false;
	}
	r->cpu_all = rate(all, CPU_ALL_WEAK, CPU_ALL_STRONG);
	/* the desk does one thing at a time where you are waiting on it, and
	 * the room on every thread there is: half and half */
	r->score[P_CPU] = cut((rate_raw(one, CPU_ONE_WEAK, CPU_ONE_STRONG)
		+ rate_raw(all, CPU_ALL_WEAK, CPU_ALL_STRONG)) * 0.5);
	if (m->text) {
		fprintf(stderr, "  %.0f helpings a second on one thread, %.0f on %d\n", one, all, n);
	}
	return true;
}

/* ---- Memory ---- */

#define MEM_HALF ((size_t)64 << 20)

static bool measure_mem(struct meter *m, struct rating *r)
{
	r->ram_gib = ram_gib();
	char *blk = NULL;
	if (posix_memalign((void **)&blk, 4096, MEM_HALF * 2) != 0) {
		return false;
	}
	/* every page touched first: a page fault is not the memory's speed */
	memset(blk, 0x5a, MEM_HALF * 2);
	meter_say(m, P_MEM, 0.05, 0.0);
	double best = 1e9, t0 = now_s(), last = t0;
	int copies = 0;
	while ((copies < 3 || now_s() - t0 < 0.9) && !meter_stopped(m)) {
		blk[(size_t)copies * 4096 % MEM_HALF] ^= 1;
		double a = now_s();
		memcpy(blk + MEM_HALF, blk, MEM_HALF);
		double took = now_s() - a;
		best = took < best ? took : best;
		copies++;
		if (now_s() - last > 0.05) {
			last = now_s();
			meter_say(m, P_MEM, 0.05 + 0.45 * (last - t0) / 0.9,
				rate_raw(MEM_HALF / best / 1e9, MEM_GBS_WEAK, MEM_GBS_STRONG));
		}
	}
	r->mem_gbs = MEM_HALF / best / 1e9;
	double bw = rate_raw(r->mem_gbs, MEM_GBS_WEAK, MEM_GBS_STRONG);

	/* A ring through every cache line of the 128 MB in a random order
	 * (Sattolo's shuffle makes it one ring, not several), each line holding
	 * where the next is, so that every load waits for the one before. */
	size_t lines = MEM_HALF * 2 / 64;
	uint32_t *next = malloc(lines * sizeof *next);
	if (!next || meter_stopped(m)) {
		free(next);
		free(blk);
		return false;
	}
	for (size_t i = 0; i < lines; i++) {
		next[i] = (uint32_t)i;
	}
	uint64_t s = 0x2545f4914f6cdd1dull;
	for (size_t i = lines - 1; i > 0; i--) {
		s ^= s >> 12;
		s ^= s << 25;
		s ^= s >> 27;
		size_t j = (size_t)((s * 0x2545f4914f6cdd1dull) >> 33) % i;
		uint32_t tmp = next[i];
		next[i] = next[j];
		next[j] = tmp;
	}
	for (size_t i = 0; i < lines; i++) {
		*(char **)(blk + i * 64) = blk + (size_t)next[i] * 64;
	}
	free(next);
	meter_say(m, P_MEM, 0.55, bw);
	char *p = blk;
	long hops = 0;
	t0 = now_s();
	double took;
	do {
		for (int k = 0; k < (1 << 16); k++) {
			p = *(char **)p;
		}
		hops += 1 << 16;
		took = now_s() - t0;
		if (took - (last - t0) > 0.05) {
			last = now_s();
			double ns = took * 1e9 / hops;
			meter_say(m, P_MEM, 0.55 + 0.45 * took / 0.8,
				bw * 0.75 + rate_raw(1.0 / ns, 1.0 / MEM_NS_WEAK, 1.0 / MEM_NS_STRONG) * 0.25);
		}
	} while (took < 0.8 && !meter_stopped(m));
	/* where the walk ended up, so that it is not thrown away unwalked */
	r->mem_ns = took * 1e9 / hops + (p == NULL ? 1.0 : 0.0);
	free(blk);
	if (meter_stopped(m)) {
		return false;
	}
	double lat = rate_raw(1.0 / r->mem_ns, 1.0 / MEM_NS_WEAK, 1.0 / MEM_NS_STRONG);
	double sc = cut(bw * 0.75 + lat * 0.25);
	double cap = mem_cap(r->ram_gib);
	r->score[P_MEM] = sc < cap ? sc : cap;
	return true;
}

/* ---- Graphics ----
 *
 * One desk's worth of drawing, through the very functions the desk draws
 * with, into a 1440 x 900 frame: the room's sheet laid in, scaled, as the
 * desk lays its traced room; a window's paper with its shadow and a sheet
 * of settings in it; Shell Settings' form; a letter part way through being
 * folded and another being screwed up, each at a different moment every
 * frame; and a sheet out of a binder. Not the room itself: that is traced
 * once and kept, and asking for it here would set the tracer going. */

struct draw_bench {
	cairo_surface_t *frame, *room, *letter;
	struct desk_fold *fold;
	struct desk_crumple *ball;
	struct desk_form_row rows[7];
	struct desk_shell_form form;
};

static void draw_bench_free(struct draw_bench *b)
{
	if (b->fold) {
		desk_fold_free(b->fold);
	}
	if (b->ball) {
		desk_crumple_free(b->ball);
	}
	if (b->letter) {
		cairo_surface_destroy(b->letter);
	}
	if (b->room) {
		cairo_surface_destroy(b->room);
	}
	if (b->frame) {
		cairo_surface_destroy(b->frame);
	}
}

static bool draw_bench_make(struct draw_bench *b)
{
	memset(b, 0, sizeof *b);
	b->frame = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, FRAME_W, FRAME_H);
	/* the room is traced a little smaller than most screens and scaled up */
	b->room = cairo_image_surface_create(CAIRO_FORMAT_RGB24, FRAME_W, 838);
	b->letter = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 300, 400);
	if (cairo_surface_status(b->frame) || cairo_surface_status(b->room)
		|| cairo_surface_status(b->letter)) {
		return false;
	}
	cairo_t *cr = cairo_create(b->room);
	cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, 0, 838);
	cairo_pattern_add_color_stop_rgb(g, 0.0, 0.30, 0.19, 0.11);
	cairo_pattern_add_color_stop_rgb(g, 1.0, 0.16, 0.09, 0.05);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	/* grain, so that it is not one colour scaled */
	uint32_t h = 12345u;
	for (int i = 0; i < 1600; i++) {
		h = h * 1664525u + 1013904223u;
		double y = (h >> 8) % 838;
		h = h * 1664525u + 1013904223u;
		cairo_set_source_rgba(cr, 0.05, 0.03, 0.02, 0.10 + (h >> 24) / 1024.0);
		cairo_rectangle(cr, 0, y, FRAME_W, 0.6 + (h % 3) * 0.4);
		cairo_fill(cr);
	}
	cairo_destroy(cr);
	cr = cairo_create(b->letter);
	desk_draw_window_frame(cr, 0, 0, 300, 400, "Letter", true, 0, false);
	cairo_destroy(cr);
	b->fold = desk_fold_new(b->letter, 7u, 120.0, NULL, "Letter");
	b->ball = desk_crumple_new(b->letter, 11u);
	static const char *const OPTS[] = { "Brisk", "Normal", "Gentle", NULL };
	b->rows[0] = (struct desk_form_row){ DESK_FORM_SECTION, "WINDOWS", NULL, NULL, 0, NULL, NULL, 0, 0 };
	b->rows[1] = (struct desk_form_row){ DESK_FORM_CHECK, "Lay every window on paper",
		"Programs that draw their own frame and buttons get the shell's paper too.",
		NULL, 1, NULL, NULL, 0, 0 };
	b->rows[2] = (struct desk_form_row){ DESK_FORM_CHECK, "Screw up a closed window",
		"Off, a window that is closed simply goes.", NULL, 0, NULL, NULL, 0, 0 };
	b->rows[3] = (struct desk_form_row){ DESK_FORM_SECTION, "DESK", NULL, NULL, 0, NULL, NULL, 0, 0 };
	b->rows[4] = (struct desk_form_row){ DESK_FORM_CHOICE, "Motion",
		"How long everything that moves takes.", OPTS, 1, NULL, NULL, 0, 0 };
	b->rows[5] = (struct desk_form_row){ DESK_FORM_CHECK, "Bring the lamp up slowly",
		"The room starts dark and the lamp comes up as it is made.", NULL, 1, NULL, NULL, 0, 0 };
	b->rows[6] = (struct desk_form_row){ DESK_FORM_CHOICE, "Sounds",
		"What each thing on the desk sounds like.", OPTS, 2, NULL, NULL, 0, 0 };
	b->form.rows = b->rows;
	b->form.n = 7;
	b->form.hover = 4;
	b->form.hover_opt = -1;
	return b->fold && b->ball;
}

static void draw_frame(struct draw_bench *b, int f)
{
	double t = (f % 24) / 24.0;
	cairo_t *cr = cairo_create(b->frame);
	cairo_save(cr);
	cairo_scale(cr, 1.0, (double)FRAME_H / 838.0);
	cairo_set_source_surface(cr, b->room, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
	cairo_paint(cr);
	cairo_restore(cr);

	desk_draw_window_frame(cr, 150, 70, 820, 560, "Settings", true, 0, true);
	desk_draw_settings_body(cr, 150 + DESK_BORDER, 70 + DESK_TITLE_H,
		820 - DESK_BORDER * 2, 560 - DESK_TITLE_H - DESK_BORDER);

	cairo_save(cr);
	cairo_rectangle(cr, 1010, 60, 380, 520);
	cairo_clip(cr);
	cairo_translate(cr, 1010, 60);
	desk_draw_shell_form(cr, 380, 520, &b->form);
	cairo_restore(cr);

	cairo_save(cr);
	cairo_translate(cr, 1040, 600);
	desk_fold_draw(cr, b->fold, t);
	cairo_restore(cr);

	cairo_save(cr);
	cairo_translate(cr, 640, 640);
	desk_crumple_draw(cr, b->ball, 1.0 - t);
	cairo_restore(cr);

	cairo_save(cr);
	cairo_translate(cr, 40, 600);
	desk_draw_app_sheet(cr, 220, 280, "Terminal", "Terminal emulator", "B-12", NULL, "T",
		(unsigned)f, 0.3);
	cairo_restore(cr);
	cairo_destroy(cr);
	cairo_surface_flush(b->frame);
}

#define DRAW_S 2.2

static bool measure_draw(struct meter *m, struct rating *r)
{
	struct draw_bench b;
	bool ok = draw_bench_make(&b);
	if (ok) {
		/* the first frame finds the faces, warms the caches: not timed */
		draw_frame(&b, 0);
		meter_say(m, P_DRAW, 0.05, 0.0);
		double t0 = now_s(), last = t0, took = 0.0;
		int frames = 0;
		while ((frames < 3 || took < DRAW_S) && !meter_stopped(m)) {
			draw_frame(&b, ++frames);
			took = now_s() - t0;
			if (took - (last - t0) > 0.05) {
				last = now_s();
				meter_say(m, P_DRAW, 0.05 + 0.95 * took / DRAW_S,
					rate_raw(frames / took, 1000.0 / DRAW_MS_WEAK, 1000.0 / DRAW_MS_STRONG));
			}
		}
		if (frames > 0 && took > 0.0) {
			r->draw_ms = took * 1000.0 / frames;
			r->score[P_DRAW] = rate(1000.0 / r->draw_ms, 1000.0 / DRAW_MS_WEAK,
				1000.0 / DRAW_MS_STRONG);
		}
	}
	draw_bench_free(&b);
	return ok && !meter_stopped(m);
}

/* ---- 3D graphics ---- */

/* What the graphics card calls itself, short: up to what it says in
 * brackets, or after a slash or a semicolon. */
static void gpu_short(const char *name, char *out, size_t n)
{
	snprintf(out, n, "%s", name);
	char *cut_at = strstr(out, " (");
	if (cut_at) {
		*cut_at = '\0';
	}
	cut_at = strpbrk(out, ";/");
	if (cut_at) {
		*cut_at = '\0';
	}
	trim(out);
	if (!out[0]) {
		snprintf(out, n, "%s", "?");
	}
}

#ifdef DESK_HAVE_GPU
/* desk-gputest's march, on a window of the frame (u_origin) at a time. */
static const char *MARCH_SRC =
	"#version 300 es\n"
	"precision highp float;\n"
	"uniform vec2 u_res;\n"
	"uniform ivec2 u_origin;\n"
	"uniform int u_steps;\n"
	"out vec4 o;\n"
	"float map(vec3 p) {\n"
	"  float d = p.y + 1.0;\n"
	"  d = min(d, length(p - vec3(-1.2, 0.0, 0.0)) - 0.9);\n"
	"  d = min(d, length(p - vec3( 0.4, 0.3, 0.6)) - 0.7);\n"
	"  d = min(d, length(p - vec3( 1.6, -0.2, -0.4)) - 0.8);\n"
	"  return d;\n"
	"}\n"
	"vec3 nrm(vec3 p) {\n"
	"  vec2 e = vec2(0.0015, 0.0);\n"
	"  return normalize(vec3(map(p + e.xyy) - map(p - e.xyy),\n"
	"                        map(p + e.yxy) - map(p - e.yxy),\n"
	"                        map(p + e.yyx) - map(p - e.yyx)));\n"
	"}\n"
	"void main() {\n"
	"  vec2 fc = gl_FragCoord.xy + vec2(u_origin);\n"
	"  vec2 uv = (fc / u_res) * 2.0 - 1.0;\n"
	"  uv.x *= u_res.x / u_res.y;\n"
	"  vec3 ro = vec3(0.0, 0.8, 5.0);\n"
	"  vec3 rd = normalize(vec3(uv, -1.6));\n"
	"  float t = 0.02;\n"
	"  vec3 col = vec3(0.02, 0.02, 0.03);\n"
	"  for (int i = 0; i < u_steps && t < 40.0; i++) {\n"
	"    vec3 p = ro + rd * t;\n"
	"    float d = map(p);\n"
	"    if (d < 0.0018) {\n"
	"      vec3 n = nrm(p);\n"
	"      vec3 l = normalize(vec3(0.5, 1.0, 0.4));\n"
	"      float diff = max(dot(n, l), 0.0);\n"
	"      float spec = pow(max(dot(reflect(-l, n), -rd), 0.0), 48.0);\n"
	"      col = vec3(0.62, 0.52, 0.40) * diff + vec3(1.0) * spec * 0.4;\n"
	"      break;\n"
	"    }\n"
	"    t += max(d, 0.0009);\n"
	"  }\n"
	"  o = vec4(col, 1.0);\n"
	"}\n";

/* The frame is marched a band at a time -- a draw is what a watchdog
 * times -- and each whole frame waited for, so that the needle can follow.
 * Not each band: waiting is a round trip to the card, which in a virtual
 * machine costs as much as a band does. And the card is woken first, for
 * GPU_WARM_S untimed: one that has been idle is clocked down, and takes a
 * moment to come up to speed. */
#define GPU_BAND_H 450
#define GPU_WARM_S 0.4
#define GPU_S 2.0

/* Whether a band the card marched (rows y0.. up from the bottom) agrees
 * with the C, at a scattering of its pixels: a pixel near an edge may fall
 * either side of it in single precision, so a few may differ, and no more. */
static bool gpu_band_right(const float *px, int y0)
{
	int bad = 0, n = 0;
	for (int k = 0; k < 240; k++) {
		int x = (int)((k * 2654435761u) % FRAME_W);
		int y = (int)((k * 40503u + 17u) % GPU_BAND_H);
		double want = march_px(x, y0 + y);
		double got = px[((size_t)y * FRAME_W + x) * 4];
		if (!(fabs(got - want) < 0.03)) {
			bad++;
		}
		n++;
	}
	return bad * 20 <= n;
}
#endif

/* With no graphics card the desk can use, the room is traced by the
 * processor: the same march, in C, a row at a time on every thread there
 * is, rated as a card would have been -- and held under SOFT_CAP, as a
 * rasterizer on the processor is. */
#define CPU_MARCH_S 1.5

struct march_job {
	atomic_int next;          /* the next row to march */
	atomic_long pixels;       /* marched so far, by all of them */
	double until;
	struct meter *m;
};

struct march_lane {
	pthread_t th;
	bool up;
	struct march_job *job;
	double sink;              /* kept, so that the marching is not for nothing */
};

static void *march_lane_run(void *arg)
{
	struct march_lane *l = arg;
	struct march_job *j = l->job;
	while (now_s() < j->until && !meter_stopped(j->m)) {
		int row = atomic_fetch_add(&j->next, 1) % FRAME_H;
		for (int x = 0; x < FRAME_W; x++) {
			l->sink += march_px(x, row);
		}
		atomic_fetch_add(&j->pixels, FRAME_W);
	}
	return NULL;
}

static double cpu_march_rate(struct meter *m, int threads)
{
	int n = threads < 1 ? 1 : threads > 256 ? 256 : threads;
	struct march_lane *lanes = calloc((size_t)n, sizeof *lanes);
	if (!lanes) {
		return 0.0;
	}
	struct march_job job;
	atomic_init(&job.next, 0);
	atomic_init(&job.pixels, 0);
	double t0 = now_s();
	job.until = t0 + CPU_MARCH_S;
	job.m = m;
	for (int i = 0; i < n; i++) {
		lanes[i].job = &job;
		lanes[i].up = pthread_create(&lanes[i].th, NULL, march_lane_run, &lanes[i]) == 0;
	}
	while (now_s() < job.until && !meter_stopped(m)) {
		struct timespec nap = { 0, 50 * 1000 * 1000 };
		nanosleep(&nap, NULL);
		double t = now_s() - t0;
		double s = rate_raw(atomic_load(&job.pixels) / t / 1e6, GPU_MPX_WEAK, GPU_MPX_STRONG);
		meter_say(m, P_GPU, 0.1 + 0.9 * t / CPU_MARCH_S, s < SOFT_CAP ? s : SOFT_CAP);
	}
	for (int i = 0; i < n; i++) {
		if (lanes[i].up) {
			pthread_join(lanes[i].th, NULL);
		}
	}
	double took = now_s() - t0;
	free(lanes);
	return meter_stopped(m) || took <= 0.0 ? 0.0 : atomic_load(&job.pixels) / took / 1e6;
}

static bool measure_gpu(struct meter *m, struct rating *r)
{
	r->gpu_kind = GPU_NONE;
	r->gpu_mpx = 0.0;
	snprintf(r->gpu_name, sizeof r->gpu_name, "%s", "none");
#ifdef DESK_HAVE_GPU
	meter_say(m, P_GPU, 0.02, 0.0);
	struct desk_gpu *g = desk_gpu_open();
	if (g) {
		gpu_short(desk_gpu_name(g), r->gpu_name, sizeof r->gpu_name);
		r->gpu_own = desk_gl_name_own(desk_gpu_name(g));
		char err[2048];
		float *band = malloc((size_t)FRAME_W * GPU_BAND_H * 4 * sizeof *band);
		if (band && desk_gpu_program(g, MARCH_SRC, err, sizeof err)) {
			bool soft = desk_gpu_software(g);
			float res[2] = { FRAME_W, FRAME_H };
			desk_gpu_set(g, "u_res", res, 2);
			desk_gpu_set_int(g, "u_steps", MARCH_STEPS);
			int target = 0;
			float one[4];
			bool ok = true;
			double t0 = now_s(), last = t0, took = 0.0;
			long pixels = 0;
			double weak = GPU_MPX_WEAK, strong = GPU_MPX_STRONG;
			/* the first frames compile, allocate and wake the card: not timed */
			for (bool warm = true; ok && !meter_stopped(m);) {
				for (int y0 = 0; ok && y0 < FRAME_H; y0 += GPU_BAND_H) {
					ok = desk_gpu_pass(g, 0, NULL, 0, &target, 1, 0, y0, FRAME_W, GPU_BAND_H);
				}
				ok = ok && desk_gpu_read(g, target, 1, 1, one);
				took = now_s() - t0;
				if (warm) {
					if (took >= GPU_WARM_S) {
						warm = false;
						t0 = last = now_s();
						took = 0.0;
					}
					continue;
				}
				pixels += (long)FRAME_W * FRAME_H;
				if (took >= GPU_S) {
					break;
				}
				if (took - (last - t0) > 0.05) {
					last = now_s();
					double s = rate_raw(pixels / took / 1e6, weak, strong);
					meter_say(m, P_GPU, 0.05 + 0.85 * took / GPU_S,
						soft && s > SOFT_CAP ? SOFT_CAP : s);
				}
			}
			if (ok && pixels > 0 && took > 0.0 && !meter_stopped(m)) {
				r->gpu_mpx = pixels / took / 1e6;
				/* and was it the march: one band read back whole and checked */
				int y0 = (FRAME_H / 2 / GPU_BAND_H) * GPU_BAND_H;
				bool right = desk_gpu_pass(g, 0, NULL, 0, &target, 1, 0, y0, FRAME_W,
					GPU_BAND_H) && desk_gpu_read(g, target, FRAME_W, GPU_BAND_H, band)
					&& gpu_band_right(band, y0);
				r->gpu_kind = !right ? GPU_WRONG : soft ? GPU_SOFT : GPU_HARD;
			} else if (!ok) {
				fprintf(stderr, "desk-index: the graphics card would not march\n");
			}
		} else if (band) {
			fprintf(stderr, "desk-index: the march would not compile: %s\n", err);
		}
		free(band);
		desk_gpu_close(g);
	}
#endif
	if (r->gpu_kind == GPU_NONE || r->gpu_kind == GPU_WRONG) {
		r->gpu_mpx = cpu_march_rate(m, cpu_threads());
	}
	if (meter_stopped(m)) {
		return false;
	}
	double s = rate(r->gpu_mpx, GPU_MPX_WEAK, GPU_MPX_STRONG);
	r->score[P_GPU] = r->gpu_kind == GPU_HARD || s < SOFT_CAP ? s : SOFT_CAP;
	return true;
}

/* ---- Disk ---- */

#define DISK_BYTES ((size_t)128 << 20)
#define DISK_CHUNK ((size_t)1 << 20)
#define DISK_WRITE_MAX_S 6.0
#define DISK_READ_MAX_S 3.0

static bool in_memory(const char *dir)
{
	struct statfs fs;
	/* tmpfs and ramfs */
	return statfs(dir, &fs) == 0 && (fs.f_type == 0x01021994 || fs.f_type == 0x858458f6);
}

static void cache_dir(char *out, size_t n)
{
	const char *c = getenv("XDG_CACHE_HOME");
	const char *home = getenv("HOME");
	if (c && *c == '/') {
		snprintf(out, n, "%s/desk", c);
	} else if (home && *home) {
		snprintf(out, n, "%s/.cache/desk", home);
	} else {
		snprintf(out, n, "%s", "/tmp");
	}
}

static void mkdirs(const char *dir)
{
	char d[600];
	snprintf(d, sizeof d, "%s/", dir);
	for (char *p = d + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(d, 0755);
			*p = '/';
		}
	}
}

static bool measure_disk(struct meter *m, struct rating *r)
{
	char dir[512], path[600];
	cache_dir(dir, sizeof dir);
	mkdirs(dir);
	/* A cache kept in memory says nothing about the disk: then where the
	 * system keeps what it writes for later, if that is on one. */
	if (in_memory(dir) && !in_memory("/var/tmp") && access("/var/tmp", W_OK) == 0) {
		snprintf(dir, sizeof dir, "%s", "/var/tmp");
	}
	snprintf(path, sizeof path, "%s/desk-index-%ld.tmp", dir, (long)getpid());
	r->disk_in_memory = in_memory(dir);
	/* never more than a quarter of the room there is */
	size_t bytes = DISK_BYTES;
	struct statvfs vfs;
	if (statvfs(dir, &vfs) == 0) {
		unsigned long long room = (unsigned long long)vfs.f_bavail * vfs.f_frsize / 4;
		while (bytes > DISK_CHUNK * 8 && bytes > room) {
			bytes /= 2;
		}
	}
	char *buf = NULL;
	if (posix_memalign((void **)&buf, 4096, DISK_CHUNK) != 0) {
		return false;
	}
	/* noise, which no file system can make smaller than it is */
	uint64_t s = 0x9e3779b97f4a7c15ull;
	for (size_t i = 0; i < DISK_CHUNK; i += 8) {
		s ^= s << 13;
		s ^= s >> 7;
		s ^= s << 17;
		memcpy(buf + i, &s, 8);
	}
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0) {
		fprintf(stderr, "desk-index: cannot write %s: %s\n", path, strerror(errno));
		free(buf);
		return false;
	}
	meter_say(m, P_DISK, 0.02, 0.0);
	double t0 = now_s(), last = t0;
	size_t wrote = 0;
	bool ok = true;
	while (wrote < bytes && now_s() - t0 < DISK_WRITE_MAX_S && !meter_stopped(m)) {
		buf[0] = (char)(wrote >> 20);
		if (write(fd, buf, DISK_CHUNK) != (ssize_t)DISK_CHUNK) {
			ok = false;
			break;
		}
		wrote += DISK_CHUNK;
		/* made sure of as it goes, so that the needle says what the disk does
		 * and not what the page cache takes in */
		if (wrote % (DISK_CHUNK * 32) == 0 && fdatasync(fd) != 0) {
			ok = false;
			break;
		}
		if (now_s() - last > 0.05) {
			last = now_s();
			double mbs = wrote / (last - t0) / 1e6;
			meter_say(m, P_DISK, 0.05 + 0.5 * wrote / bytes,
				rate_raw(mbs, DISK_MBS_WEAK, DISK_MBS_STRONG));
		}
	}
	ok = ok && fsync(fd) == 0;
	double wt = now_s() - t0;
	/* out of the page cache, so that what is read comes off the disk */
	posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
	close(fd);
	r->disk_w = ok && wt > 0.0 ? wrote / wt / 1e6 : 0.0;
	size_t got = 0;
	double rt = 0.0;
	fd = ok && !meter_stopped(m) ? open(path, O_RDONLY | O_CLOEXEC) : -1;
	if (fd >= 0) {
		posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
		posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
		t0 = now_s();
		for (;;) {
			ssize_t n = read(fd, buf, DISK_CHUNK);
			if (n <= 0 || meter_stopped(m)) {
				break;
			}
			got += (size_t)n;
			rt = now_s() - t0;
			if (rt > DISK_READ_MAX_S) {
				break;
			}
			if (now_s() - last > 0.05) {
				last = now_s();
				double mbs = sqrt(r->disk_w * (got / rt / 1e6));
				meter_say(m, P_DISK, 0.55 + 0.45 * got / wrote,
					rate_raw(mbs, DISK_MBS_WEAK, DISK_MBS_STRONG));
			}
		}
		close(fd);
	}
	unlink(path);
	free(buf);
	if (meter_stopped(m) || !ok) {
		return false;
	}
	r->disk_r = rt > 0.0 ? got / rt / 1e6 : 0.0;
	r->score[P_DISK] = rate(sqrt(r->disk_w * r->disk_r), DISK_MBS_WEAK, DISK_MBS_STRONG);
	return true;
}

/* ---- the whole run ---- */

static bool (*const MEASURE[N_PARTS])(struct meter *, struct rating *) = {
	measure_cpu, measure_mem, measure_draw, measure_gpu, measure_disk,
};

#define BUSY_S 0.4
/* busier than this, and the report says the scores may be low */
#define BUSY_NOTE 0.3

static void *meter_run(void *arg)
{
	struct meter *m = arg;
	struct rating r;
	memset(&r, 0, sizeof r);
	/* A moment first, for the sheet to be put down and the machine to
	 * settle: a window that has just opened is still being drawn, and the
	 * desk's own animation is not what is being measured. The end of it is
	 * spent seeing how busy the machine is with everything else, which is
	 * worth knowing about the scores it gets. */
	pthread_mutex_lock(&m->lock);
	m->failed = -1;
	pthread_mutex_unlock(&m->lock);
	meter_say(m, -1, 0.0, 0.0);
	meter_wait(m, m->settle - BUSY_S);
	r.busy = machine_busy(m, BUSY_S);
	for (int i = 0; i < N_PARTS && !meter_stopped(m); i++) {
		meter_say(m, i, 0.0, 0.0);
		if (m->text) {
			fprintf(stderr, "%s\n", W(PART_DOING[i]));
		}
		double t0 = now_s();
		if (!MEASURE[i](m, &r)) {
			if (!meter_stopped(m)) {
				pthread_mutex_lock(&m->lock);
				m->failed = i;
				pthread_mutex_unlock(&m->lock);
				fprintf(stderr, "desk-index: could not measure the %s\n", PART_KEY[i]);
			}
			break;
		}
		if (m->text) {
			fprintf(stderr, "  %.1f s\n", now_s() - t0);
		}
		/* the figures so far, for the report to type as they come in */
		pthread_mutex_lock(&m->lock);
		m->r = r;
		pthread_mutex_unlock(&m->lock);
		meter_poke(m);
	}
	pthread_mutex_lock(&m->lock);
	m->stopped = meter_stopped(m) || r.score[N_PARTS - 1] <= 0.0;
	if (!m->stopped) {
		r.when = time(NULL);
		rating_base(&r);
		m->r = r;
	}
	m->part = N_PARTS;
	m->finished = true;
	pthread_mutex_unlock(&m->lock);
	meter_poke(m);
	return NULL;
}

/* ---- index.conf ---- */

static void state_path(char *out, size_t n)
{
	const char *st = getenv("XDG_STATE_HOME");
	const char *home = getenv("HOME");
	if (st && *st == '/') {
		snprintf(out, n, "%s/desk/index.conf", st);
	} else if (home && *home) {
		snprintf(out, n, "%s/.local/state/desk/index.conf", home);
	} else {
		out[0] = '\0';
	}
}

static bool rating_save(const struct rating *r)
{
	char path[512], tmp[600];
	state_path(path, sizeof path);
	if (!path[0]) {
		return false;
	}
	char dir[512];
	snprintf(dir, sizeof dir, "%s", path);
	char *slash = strrchr(dir, '/');
	if (slash) {
		*slash = '\0';
		mkdirs(dir);
	}
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *f = fopen(tmp, "we");
	if (!f) {
		fprintf(stderr, "desk-index: cannot write %s: %s\n", tmp, strerror(errno));
		return false;
	}
	fprintf(f, "# The DEsk Experience Index: how this machine did, the last time it was\n"
		"# rated. desk-index writes this.\n");
	fprintf(f, "when = %lld\nbase = %.1f\n", (long long)r->when, r->base);
	for (int i = 0; i < N_PARTS; i++) {
		fprintf(f, "%s = %.1f\n", PART_KEY[i], r->score[i]);
	}
	fprintf(f, "processor.one = %.1f\nprocessor.all = %.1f\nprocessor.threads = %d\n"
		"processor.quota = %.2f\nprocessor.name = %s\n", r->cpu_one, r->cpu_all, r->threads,
		r->cpu_quota, r->cpu_name);
	fprintf(f, "memory.copy = %.2f\nmemory.latency = %.1f\nmemory.total = %.2f\n",
		r->mem_gbs, r->mem_ns, r->ram_gib);
	fprintf(f, "graphics.frame = %.2f\n", r->draw_ms);
	fprintf(f, "3d.rate = %.1f\n3d.kind = %s\n3d.device = %s\n", r->gpu_mpx,
		GPU_KIND[r->gpu_kind], r->gpu_name);
	fprintf(f, "disk.write = %.1f\ndisk.read = %.1f\ndisk.memory = %s\n", r->disk_w,
		r->disk_r, r->disk_in_memory ? "yes" : "no");
	fprintf(f, "busy = %.2f\n", r->busy);
	bool ok = fflush(f) == 0 && !ferror(f);
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		fprintf(stderr, "desk-index: cannot write %s\n", path);
		unlink(tmp);
		return false;
	}
	return true;
}

static bool rating_load(struct rating *r)
{
	memset(r, 0, sizeof *r);
	char path[512];
	state_path(path, sizeof path);
	FILE *f = path[0] ? fopen(path, "re") : NULL;
	if (!f) {
		return false;
	}
	char line[512];
	while (fgets(line, sizeof line, f)) {
		char *s = trim(line);
		char *eq = strchr(s, '=');
		if (*s == '#' || !eq) {
			continue;
		}
		*eq = '\0';
		char *k = trim(s), *v = trim(eq + 1);
		double x = atof(v);
		for (int i = 0; i < N_PARTS; i++) {
			if (!strcmp(k, PART_KEY[i])) {
				r->score[i] = x >= SCORE_MIN && x <= SCORE_MAX ? x : 0.0;
			}
		}
		if (!strcmp(k, "when")) {
			r->when = (time_t)atoll(v);
		} else if (!strcmp(k, "processor.one")) {
			r->cpu_one = x;
		} else if (!strcmp(k, "processor.all")) {
			r->cpu_all = x;
		} else if (!strcmp(k, "processor.threads")) {
			r->threads = atoi(v);
		} else if (!strcmp(k, "processor.quota")) {
			r->cpu_quota = x;
		} else if (!strcmp(k, "processor.name")) {
			snprintf(r->cpu_name, sizeof r->cpu_name, "%s", v);
		} else if (!strcmp(k, "memory.copy")) {
			r->mem_gbs = x;
		} else if (!strcmp(k, "memory.latency")) {
			r->mem_ns = x;
		} else if (!strcmp(k, "memory.total")) {
			r->ram_gib = x;
		} else if (!strcmp(k, "graphics.frame")) {
			r->draw_ms = x;
		} else if (!strcmp(k, "3d.rate")) {
			r->gpu_mpx = x;
		} else if (!strcmp(k, "3d.kind")) {
			for (int i = 0; i < 4; i++) {
				if (!strcmp(v, GPU_KIND[i])) {
					r->gpu_kind = i;
				}
			}
		} else if (!strcmp(k, "3d.device")) {
			snprintf(r->gpu_name, sizeof r->gpu_name, "%s", v);
		} else if (!strcmp(k, "disk.write")) {
			r->disk_w = x;
		} else if (!strcmp(k, "disk.read")) {
			r->disk_r = x;
		} else if (!strcmp(k, "disk.memory")) {
			r->disk_in_memory = !strcmp(v, "yes");
		} else if (!strcmp(k, "busy")) {
			r->busy = x;
		}
	}
	fclose(f);
	bool whole = r->when > 0;
	for (int i = 0; i < N_PARTS; i++) {
		whole = whole && r->score[i] > 0.0;
	}
	if (!whole) {
		memset(r, 0, sizeof *r);
		return false;
	}
	rating_base(r);
	return true;
}

/* ---- what the scores mean ---- */

static const char *part_meaning(const struct rating *r, int i)
{
	static const char *const SAY[N_PARTS][4] = {
		{ "Programs open briskly; the room's first trace goes by unnoticed.",
			"Comfortable for everyday work; heavy programs take a moment.",
			"Fine for writing and browsing; heavy programs keep it busy.",
			"Slow going: expect to wait for programs to open." },
		{ "Room for the desk, a browser and plenty more besides.",
			"Enough for the desk and the usual programs side by side.",
			"The desk and a browser fit; much more than that will drag.",
			"Too little for comfort: the desk will be short of room." },
		{ "Paper folds, crumples and turns without a stutter.",
			"Smooth, though a fold may now and then skip a frame.",
			"Folding and the bookcase will be visibly jerky at times.",
			"The desk's paper will move in fits and starts." },
		{ "The room is traced on the graphics card in seconds.",
			"The room is traced on the graphics card in well under a minute.",
			"The card traces the room slowly; it is kept, so only once.",
			"Barely up to the room: the processor may as well trace it." },
		{ "Programs and the room come off the disk at once.",
			"Programs start quickly; the room comes out of its cache in a moment.",
			"Programs take a few seconds to start the first time.",
			"A slow disk: whatever is read for the first time keeps you waiting." },
	};
	double s = r->score[i];
	if (i == P_GPU && r->gpu_kind != GPU_HARD) {
		return W("Traced on the processor instead, once per screen size, and kept.");
	}
	return W(SAY[i][s >= 7.0 ? 0 : s >= 5.0 ? 1 : s >= 3.0 ? 2 : 3]);
}

static const char *base_meaning(double base)
{
	return W(base >= 7.0 ? "This machine does everything the desk asks of it, easily."
		: base >= 5.0 ? "Comfortable for everything the desk does."
		: base >= 3.0 ? "The desk runs well; some of its movements will be less than smooth."
		: "Below what the desk needs to feel right.");
}

/* The figures behind a score, in a line. */
static void part_figures(const struct rating *r, int i, char *out, size_t n)
{
	switch (i) {
	case P_CPU:
		if (r->cpu_quota > 0.0 && r->cpu_quota < r->threads) {
			snprintf(out, n, W("one thread %.1f, all %d threads %.1f (held to %.1f CPUs)"),
				r->cpu_one, r->threads, r->cpu_all, r->cpu_quota);
		} else {
			snprintf(out, n, W("one thread %.1f, all %d threads %.1f"), r->cpu_one,
				r->threads, r->cpu_all);
		}
		break;
	case P_MEM:
		if (mem_cap(r->ram_gib) < SCORE_MAX && r->score[P_MEM] >= mem_cap(r->ram_gib)) {
			/* held under its cap: say so */
			snprintf(out, n, W("copies %.1f GB/s, %.0f ns to a random place; %.1f GB holds it "
				"to %.1f"), r->mem_gbs, r->mem_ns, r->ram_gib, mem_cap(r->ram_gib));
		} else {
			snprintf(out, n, W("copies %.1f GB/s, %.0f ns to a random place, %.1f GB in all"),
				r->mem_gbs, r->mem_ns, r->ram_gib);
		}
		break;
	case P_DRAW:
		snprintf(out, n, W("the test desk at 1440 \xc3\x97 900 drawn in %.1f ms"), r->draw_ms);
		break;
	case P_GPU:
		if (r->gpu_kind == GPU_NONE) {
			snprintf(out, n, W("no graphics card the desk can use: the processor marches "
				"%.1f million pixels a second"), r->gpu_mpx);
		} else if (r->gpu_kind == GPU_WRONG) {
			snprintf(out, n, W("%s answered wrongly: the processor marches %.1f million "
				"pixels a second"), r->gpu_name, r->gpu_mpx);
		} else {
			snprintf(out, n, W("%s: %.0f million pixels of the room's march a second"),
				r->gpu_name, r->gpu_mpx);
		}
		break;
	default:
		snprintf(out, n, W(r->disk_in_memory
			? "writes %.0f MB/s, reads %.0f MB/s (kept in memory, not on a disk)"
			: "writes %.0f MB/s, reads %.0f MB/s"), r->disk_w, r->disk_r);
		break;
	}
}

/* When, as the desk's clock tells the time (shell.conf: clock). */
static bool g_clock_12h;

static void when_words(time_t when, char *out, size_t n)
{
	static const char *const MONTH[12] = { "January", "February", "March", "April", "May",
		"June", "July", "August", "September", "October", "November", "December" };
	struct tm tm;
	localtime_r(&when, &tm);
	char hm[32];
	enum desk_lang l = desk_lang_get();
	/* the morning and the afternoon, written before the time in Chinese,
	 * Japanese and Korean */
	static const char *const HALF[DESK_LANG_COUNT][2] = {
		{ "AM", "PM" }, { "午前", "午後" }, { "上午", "下午" }, { "오전", "오후" },
		{ "a. m.", "p. m." }, { "AM", "PM" }, { "AM", "PM" },
	};
	if (g_clock_12h) {
		int h = tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12;
		const char *half = HALF[l][0] ? HALF[l][tm.tm_hour >= 12]
			: HALF[DESK_LANG_EN][tm.tm_hour >= 12];
		if (desk_lang_wide(l)) {
			snprintf(hm, sizeof hm, "%s %d:%02d", half, h, tm.tm_min);
		} else {
			snprintf(hm, sizeof hm, "%d:%02d %s", h, tm.tm_min, half);
		}
	} else {
		snprintf(hm, sizeof hm, "%02d:%02d", tm.tm_hour, tm.tm_min);
	}
	int y = tm.tm_year + 1900, m = tm.tm_mon + 1, d = tm.tm_mday;
	/* the month's name as in a sentence: small in Spanish and French */
	char mon[32];
	snprintf(mon, sizeof mon, "%s", TR(MONTH[tm.tm_mon]));
	if (l == DESK_LANG_ES || l == DESK_LANG_FR) {
		mon[0] = (char)tolower((unsigned char)mon[0]);
	}
	switch (l) {
	case DESK_LANG_JA:
	case DESK_LANG_ZH:
		snprintf(out, n, "%d年%d月%d日 %s", y, m, d, hm);
		break;
	case DESK_LANG_KO:
		snprintf(out, n, "%d년 %d월 %d일 %s", y, m, d, hm);
		break;
	case DESK_LANG_ES:
		snprintf(out, n, "%d de %s de %d, %s", d, mon, y, hm);
		break;
	case DESK_LANG_FR:
		snprintf(out, n, "%d %s %d à %s", d, mon, y, hm);
		break;
	case DESK_LANG_DE:
		snprintf(out, n, "%d. %s %d, %s", d, mon, y, hm);
		break;
	default:
		snprintf(out, n, "%d %s %d, %s", d, mon, y, hm);
		break;
	}
}

/* ---- shell.conf: the language, the clock, how loud ---- */

static double g_sound_level = 1.0;

static void shell_conf_read(void)
{
	char path[512];
	const char *cfg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (cfg && *cfg == '/') {
		snprintf(path, sizeof path, "%s/desk/shell.conf", cfg);
	} else if (home && *home) {
		snprintf(path, sizeof path, "%s/.config/desk/shell.conf", home);
	} else {
		path[0] = '\0';
	}
	enum desk_lang lang = desk_lang_from_locale();
	g_clock_12h = false;
	g_sound_level = 1.0;
	FILE *f = path[0] ? fopen(path, "re") : NULL;
	if (f) {
		char line[512];
		while (fgets(line, sizeof line, f)) {
			char *s = trim(line);
			char *eq = strchr(s, '=');
			if (*s == '#' || !eq) {
				continue;
			}
			*eq = '\0';
			char *k = trim(s), *v = trim(eq + 1);
			if (!strcmp(k, "language")) {
				lang = desk_lang_parse(v);
			} else if (!strcmp(k, "clock")) {
				g_clock_12h = !strcmp(v, "12h") || !strcmp(v, "12");
			} else if (!strcmp(k, "sound")) {
				g_sound_level = !strcmp(v, "off") ? 0.0 : !strcmp(v, "quiet") ? 0.45 : 1.0;
			}
		}
		fclose(f);
	}
	desk_lang_set(lang);
}

/* ---- the sheet, as it is laid out ----
 *
 * Everything is placed on a sheet SHEET_W by SHEET_H, scaled to the size
 * of the window and kept in the middle of it. At the top, screwed to the
 * paper, an instrument panel in black crackle finish with the five brass
 * meters on it, a brass plate under each with what it measures cut into
 * it and a jewel lamp beside it that is lit while its part is measured.
 * Under that the report, typed: a row for each part with its score, a line
 * of what that means at the desk and the figures behind it. At the foot
 * the base score stamped in red ink, what it means, when the machine was
 * rated and what it is -- and "Rate again", written in pencil. */

#define SHEET_W 760.0
#define SHEET_H 600.0
#define PANEL_X 16.0
#define PANEL_Y 10.0
#define PANEL_W 728.0
#define PANEL_H 180.0
#define GAUGE_R 54.0
#define GAUGE_Y (PANEL_Y + 16.0 + GAUGE_R)
#define PLATE_Y (GAUGE_Y + GAUGE_R + 23.0)
#define ROW_X 28.0
#define ROW_Y 212.0
#define ROW_H 41.0
#define TEXT_X 214.0
#define TEXT_W (SHEET_W - 26.0 - TEXT_X)
#define FOOT_Y 432.0
#define STAMP_X 110.0
#define STAMP_Y 506.0
#define STAMP_R 62.0
#define NEEDLE_REST 0.75    /* where a needle lies, against its stop pin */
#define STAMP_FALL 0.16     /* seconds the stamp takes to come down */

static double gauge_x(int i)
{
	return PANEL_X + PANEL_W / (2.0 * N_PARTS) * (2 * i + 1);
}

/* The angle a value is shown at: 1 down to the left, 10 down to the right,
 * three quarters of a turn round through the top. */
static double gauge_angle(double v)
{
	return (135.0 + (v - 1.0) / 9.0 * 270.0) * M_PI / 180.0;
}

/* 0..1, the same every time for the same n */
static double noise(uint32_t n)
{
	n ^= n >> 16;
	n *= 0x7feb352du;
	n ^= n >> 15;
	n *= 0x846ca68bu;
	n ^= n >> 16;
	return n / 4294967296.0;
}

/* how many bytes the letter at p is */
static int letter_len(const char *p)
{
	unsigned char c = (unsigned char)*p;
	int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
	for (int i = 1; i < n; i++) {
		if (!p[i]) {
			return i;
		}
	}
	return n;
}

static void rounded(cairo_t *cr, double x, double y, double w, double h, double r)
{
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0, 0.0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0.0, M_PI / 2.0);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2.0, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
	cairo_close_path(cr);
}

/* ---- words on the sheet ---- */

static void face(cairo_t *cr, enum desk_font f, const char *s, double size, bool bold)
{
	desk_font_select(cr, f, s, bold, false);
	cairo_set_font_size(cr, size);
}

static double text_w(cairo_t *cr, enum desk_font f, const char *s, double size, bool bold)
{
	cairo_save(cr);
	face(cr, f, s, size, bold);
	cairo_text_extents_t e;
	cairo_text_extents(cr, s, &e);
	cairo_restore(cr);
	return e.x_advance;
}

/* s set with its left (align 0), its middle (0.5) or its right (1) at x,
 * in whatever the source is */
static void text_at(cairo_t *cr, enum desk_font f, const char *s, double x, double y,
	double size, bool bold, double align)
{
	cairo_save(cr);
	face(cr, f, s, size, bold);
	if (align > 0.0) {
		cairo_text_extents_t e;
		cairo_text_extents(cr, s, &e);
		x -= e.x_advance * align;
	}
	cairo_move_to(cr, x, y);
	cairo_show_text(cr, s);
	/* the path is not part of what save and restore keep: a current point
	 * left at the end of the text would join whatever is drawn next to it */
	cairo_new_path(cr);
	cairo_restore(cr);
}

/* Typed onto the sheet: every letter struck a little differently, a hair
 * high or low and its ink a little heavier or lighter, as a typewriter
 * strikes them. Made smaller to fit in w, if w is not 0. The letters are
 * laid out once and inked in a few strengths, one pass each, rather than
 * one at a time: a report is hundreds of them. */
#define TYPE_INKS 4

static void typed(cairo_t *cr, const char *s, double x, double y, double size, bool bold,
	double r, double g, double b, double a, double w, unsigned seed)
{
	if (w > 0.0) {
		double tw = text_w(cr, DESK_FONT_TYPE, s, size, bold);
		size = tw > w ? size * w / tw : size;
	}
	cairo_save(cr);
	face(cr, DESK_FONT_TYPE, s, size, bold);
	cairo_glyph_t *gl = NULL;
	int n = 0;
	if (cairo_scaled_font_text_to_glyphs(cairo_get_scaled_font(cr), x, y, s, -1, &gl, &n,
			NULL, NULL, NULL) != CAIRO_STATUS_SUCCESS || n <= 0) {
		cairo_restore(cr);
		return;
	}
	cairo_glyph_t *ink = malloc((size_t)n * sizeof *ink);
	for (int pass = 0; ink && pass < TYPE_INKS; pass++) {
		int m = 0;
		for (int k = 0; k < n; k++) {
			unsigned h = seed * 977u + (unsigned)k;
			if ((int)(noise(h * 2u + 1u) * TYPE_INKS) == pass) {
				ink[m] = gl[k];
				ink[m].y += (noise(h * 2u) - 0.5) * size * 0.05;
				m++;
			}
		}
		cairo_set_source_rgba(cr, r, g, b, a * (0.78 + 0.22 * (pass + 0.5) / TYPE_INKS));
		cairo_show_glyphs(cr, ink, m);
	}
	free(ink);
	cairo_glyph_free(gl);
	cairo_new_path(cr);
	cairo_restore(cr);
}

/* s in lines no wider than w in the face it will be typed in: broken at a
 * space, or between any two letters of a language that has none. */
static int wrap(cairo_t *cr, const char *s, double size, bool bold, double w, char out[][256],
	int max)
{
	cairo_save(cr);
	face(cr, DESK_FONT_TYPE, s, size, bold);
	int n = 0;
	const char *line = s;
	while (*line && n < max) {
		while (*line == ' ') {
			line++;
		}
		const char *p = line, *end = line, *brk = NULL;
		while (*p) {
			int k = letter_len(p);
			char buf[256];
			size_t len = (size_t)(p + k - line);
			if (len >= sizeof buf) {
				break;
			}
			memcpy(buf, line, len);
			buf[len] = '\0';
			cairo_text_extents_t e;
			cairo_text_extents(cr, buf, &e);
			if (e.x_advance > w && end > line) {
				break;
			}
			p += k;
			end = p;
			if (*p == ' ' || k >= 3) {
				brk = p;
			}
		}
		if (*p && brk && brk > line) {
			end = brk;
		}
		size_t len = (size_t)(end - line);
		len = len > 255 ? 255 : len;
		memcpy(out[n], line, len);
		out[n][len] = '\0';
		if (len > 0) {
			n++;
		}
		if (end == line) {
			break;
		}
		line = end;
	}
	cairo_restore(cr);
	return n;
}

/* Cut into brass: the letters dark, with the light catching the lower lip
 * of each cut. Spaced out a little, and narrowed to fit in w. */
static void engraved(cairo_t *cr, const char *s, double cx, double y, double size, double w)
{
	double sp = size * 0.14;
	double tw = 0.0;
	int n = 0;
	for (const char *p = s; *p; n++) {
		int k = letter_len(p);
		char ch[8];
		memcpy(ch, p, (size_t)k);
		ch[k] = '\0';
		tw += text_w(cr, DESK_FONT_ENGRAVE, ch, size, true);
		p += k;
	}
	tw += sp * (n > 0 ? n - 1 : 0);
	double sx = tw > w ? w / tw : 1.0;
	cairo_save(cr);
	cairo_translate(cr, cx - tw * sx * 0.5, y);
	cairo_scale(cr, sx, 1.0);
	for (int pass = 0; pass < 2; pass++) {
		double x = 0.0;
		if (pass == 0) {
			cairo_set_source_rgba(cr, 1.0, 0.95, 0.80, 0.55);
		} else {
			cairo_set_source_rgba(cr, 0.16, 0.11, 0.05, 0.92);
		}
		for (const char *p = s; *p;) {
			int k = letter_len(p);
			char ch[8];
			memcpy(ch, p, (size_t)k);
			ch[k] = '\0';
			p += k;
			text_at(cr, DESK_FONT_ENGRAVE, ch, x, pass == 0 ? 0.8 : 0.0, size, true, 0.0);
			x += text_w(cr, DESK_FONT_ENGRAVE, ch, size, true) + sp;
		}
	}
	cairo_restore(cr);
}

/* Lettering on a circle of radius r about the origin, its middle at angle
 * mid: round the top reading clockwise, or round the bottom (below) reading
 * the other way, upright either way. */
static void arc_text(cairo_t *cr, const char *s, double r, double mid, double size, bool below)
{
	double sp = size * 0.18, total = 0.0;
	for (const char *p = s; *p;) {
		int k = letter_len(p);
		char ch[8];
		memcpy(ch, p, (size_t)k);
		ch[k] = '\0';
		p += k;
		total += text_w(cr, DESK_FONT_ENGRAVE, ch, size, true) + sp;
	}
	total -= sp;
	double at = 0.0;
	for (const char *p = s; *p;) {
		int k = letter_len(p);
		char ch[8];
		memcpy(ch, p, (size_t)k);
		ch[k] = '\0';
		p += k;
		double adv = text_w(cr, DESK_FONT_ENGRAVE, ch, size, true);
		double off = (at + adv * 0.5 - total * 0.5) / r;
		cairo_save(cr);
		if (below) {
			cairo_rotate(cr, mid - off - M_PI / 2.0);
			text_at(cr, DESK_FONT_ENGRAVE, ch, -adv * 0.5, r + size * 0.36, size, true, 0.0);
		} else {
			cairo_rotate(cr, mid + off + M_PI / 2.0);
			text_at(cr, DESK_FONT_ENGRAVE, ch, -adv * 0.5, -r + size * 0.36, size, true, 0.0);
		}
		cairo_restore(cr);
		at += adv + sp;
	}
}

/* ---- the panel and what is on it ---- */

static void brass_stops(cairo_pattern_t *g, double lift)
{
	cairo_pattern_add_color_stop_rgb(g, 0.00, 0.98 * lift, 0.89 * lift, 0.64 * lift);
	cairo_pattern_add_color_stop_rgb(g, 0.42, 0.82 * lift, 0.64 * lift, 0.32 * lift);
	cairo_pattern_add_color_stop_rgb(g, 0.58, 0.64 * lift, 0.47 * lift, 0.20 * lift);
	cairo_pattern_add_color_stop_rgb(g, 1.00, 0.36 * lift, 0.25 * lift, 0.10 * lift);
}

static void screw(cairo_t *cr, double x, double y, double r, unsigned seed)
{
	cairo_pattern_t *g = cairo_pattern_create_radial(x - r * 0.35, y - r * 0.4, r * 0.1,
		x, y, r);
	cairo_pattern_add_color_stop_rgb(g, 0.0, 0.96, 0.89, 0.70);
	cairo_pattern_add_color_stop_rgb(g, 0.6, 0.66, 0.55, 0.34);
	cairo_pattern_add_color_stop_rgb(g, 1.0, 0.34, 0.26, 0.14);
	cairo_arc(cr, x, y, r, 0.0, 2.0 * M_PI);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
	cairo_set_source_rgba(cr, 0.10, 0.07, 0.03, 0.6);
	cairo_set_line_width(cr, 0.6);
	cairo_stroke(cr);
	double a = noise(seed) * M_PI, c = cos(a) * r * 0.78, s = sin(a) * r * 0.78;
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
	cairo_set_line_width(cr, r * 0.30);
	cairo_set_source_rgba(cr, 1.0, 0.96, 0.85, 0.35);
	cairo_move_to(cr, x - c, y - s + 0.5);
	cairo_line_to(cr, x + c, y + s + 0.5);
	cairo_stroke(cr);
	cairo_set_source_rgba(cr, 0.17, 0.12, 0.06, 0.85);
	cairo_move_to(cr, x - c, y - s);
	cairo_line_to(cr, x + c, y + s);
	cairo_stroke(cr);
}

static void panel_paint(cairo_t *cr)
{
	double x = PANEL_X, y = PANEL_Y, w = PANEL_W, h = PANEL_H;
	/* its shadow on the paper, soft, and a little below it */
	for (int k = 7; k >= 1; k--) {
		rounded(cr, x - k * 0.7, y + 2.0 + k * 0.6, w + k * 1.4, h + k * 1.1, 10.0 + k);
		cairo_set_source_rgba(cr, 0.16, 0.10, 0.04, 0.035);
		cairo_fill(cr);
	}
	rounded(cr, x, y, w, h, 10.0);
	cairo_set_source_rgb(cr, 0.105, 0.098, 0.092);
	cairo_fill_preserve(cr);
	cairo_save(cr);
	cairo_clip(cr);
	/* crackle: the wrinkles of a stoved black enamel, seen as a scatter of
	 * lighter and darker grains */
	for (int light = 0; light < 2; light++) {
		for (int i = 0; i < 1400; i++) {
			uint32_t k = (uint32_t)(i * 4 + light * 7919);
			cairo_rectangle(cr, x + noise(k) * w, y + noise(k + 1u) * h,
				0.6 + noise(k + 2u) * 0.9, 0.6 + noise(k + 3u) * 0.6);
		}
		cairo_set_source_rgba(cr, light, light, light, light ? 0.06 : 0.22);
		cairo_fill(cr);
	}
	/* the lamp over the desk, catching its top */
	cairo_pattern_t *g = cairo_pattern_create_linear(0, y, 0, y + h);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 1, 1, 1, 0.10);
	cairo_pattern_add_color_stop_rgba(g, 0.35, 1, 1, 1, 0.0);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 0, 0, 0, 0.20);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	cairo_restore(cr);
	/* its edge, rolled over: lit along the top, dark along the bottom */
	rounded(cr, x + 0.5, y + 0.5, w - 1.0, h - 1.0, 9.5);
	g = cairo_pattern_create_linear(0, y, 0, y + h);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 1, 1, 1, 0.30);
	cairo_pattern_add_color_stop_rgba(g, 0.5, 1, 1, 1, 0.04);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 0, 0, 0, 0.55);
	cairo_set_source(cr, g);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);
	cairo_pattern_destroy(g);
	screw(cr, x + 11.0, y + 11.0, 4.2, 1u);
	screw(cr, x + w - 11.0, y + 11.0, 4.2, 2u);
	screw(cr, x + 11.0, y + h - 11.0, 4.2, 3u);
	screw(cr, x + w - 11.0, y + h - 11.0, 4.2, 4u);
}

/* Where the score shows on a meter: a little window at the foot of the
 * dial, between the two ends of its scale. */
static void gauge_window(double cx, double cy, double box[4])
{
	box[2] = GAUGE_R * 0.56;
	box[3] = GAUGE_R * 0.25;
	box[0] = cx - box[2] * 0.5;
	box[1] = cy + GAUGE_R * 0.40;
}

/* A meter, all but its needle and its score: the brass bezel, the dial
 * with its scale, the stop pin, the window the score shows in. */
static void gauge_paint(cairo_t *cr, double cx, double cy)
{
	double R = GAUGE_R;
	/* its shadow on the panel */
	cairo_pattern_t *g = cairo_pattern_create_radial(cx + 2.0, cy + 4.0, R * 0.9,
		cx + 2.0, cy + 4.0, R * 1.16);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 0, 0, 0, 0.55);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 0, 0, 0, 0.0);
	cairo_arc(cr, cx + 2.0, cy + 4.0, R * 1.16, 0.0, 2.0 * M_PI);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	/* the bezel: turned brass, lit from the top left */
	g = cairo_pattern_create_linear(cx - R, cy - R, cx + R, cy + R);
	brass_stops(g, 1.0);
	cairo_arc(cr, cx, cy, R, 0.0, 2.0 * M_PI);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	/* the rings the lathe left in it */
	cairo_set_line_width(cr, 0.5);
	for (double r = R * 0.87; r < R - 0.6; r += 1.3) {
		double v = noise((uint32_t)(r * 10.0));
		cairo_set_source_rgba(cr, v > 0.5 ? 1 : 0, v > 0.5 ? 0.95 : 0, v > 0.5 ? 0.8 : 0,
			0.05 + 0.06 * v);
		cairo_arc(cr, cx, cy, r, 0.0, 2.0 * M_PI);
		cairo_stroke(cr);
	}
	/* the bezel's inner lip, the opposite way to its face */
	g = cairo_pattern_create_linear(cx - R, cy - R, cx + R, cy + R);
	cairo_pattern_add_color_stop_rgb(g, 0.0, 0.30, 0.21, 0.08);
	cairo_pattern_add_color_stop_rgb(g, 1.0, 0.95, 0.84, 0.58);
	cairo_arc(cr, cx, cy, R * 0.865, 0.0, 2.0 * M_PI);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	/* the dial, cream, a little darker to its edge */
	g = cairo_pattern_create_radial(cx - R * 0.15, cy - R * 0.2, R * 0.1, cx, cy, R * 0.84);
	cairo_pattern_add_color_stop_rgb(g, 0.0, 0.975, 0.955, 0.890);
	cairo_pattern_add_color_stop_rgb(g, 0.8, 0.935, 0.905, 0.815);
	cairo_pattern_add_color_stop_rgb(g, 1.0, 0.840, 0.800, 0.700);
	cairo_arc(cr, cx, cy, R * 0.835, 0.0, 2.0 * M_PI);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	/* the red, below 3: not enough */
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
	cairo_set_line_width(cr, R * 0.055);
	cairo_set_source_rgba(cr, 0.72, 0.15, 0.10, 0.85);
	cairo_arc(cr, cx, cy, R * 0.665, gauge_angle(1.0), gauge_angle(3.0));
	cairo_stroke(cr);
	/* the scale: an arc, and a tick every quarter, longer at the halves
	 * and longest at the whole numbers */
	cairo_set_source_rgba(cr, 0.12, 0.10, 0.08, 0.92);
	cairo_set_line_width(cr, 0.8);
	cairo_arc(cr, cx, cy, R * 0.74, gauge_angle(1.0), gauge_angle(10.0));
	cairo_stroke(cr);
	for (int q = 0; q <= 36; q++) {
		double v = 1.0 + q * 0.25, a = gauge_angle(v);
		double len = q % 4 == 0 ? 0.13 : q % 2 == 0 ? 0.085 : 0.05;
		cairo_set_line_width(cr, q % 4 == 0 ? 1.5 : q % 2 == 0 ? 1.0 : 0.7);
		cairo_move_to(cr, cx + cos(a) * R * 0.74, cy + sin(a) * R * 0.74);
		cairo_line_to(cr, cx + cos(a) * R * (0.74 - len), cy + sin(a) * R * (0.74 - len));
		cairo_stroke(cr);
	}
	for (int v = 1; v <= 10; v++) {
		double a = gauge_angle(v);
		char num[4];
		snprintf(num, sizeof num, "%d", v);
		cairo_set_source_rgba(cr, 0.12, 0.10, 0.08, 0.92);
		text_at(cr, DESK_FONT_ENGRAVE, num, cx + cos(a) * R * 0.48,
			cy + sin(a) * R * 0.48 + R * 0.055, R * (v == 10 ? 0.13 : 0.155), true, 0.5);
	}
	/* whose it is */
	cairo_set_source_rgba(cr, 0.30, 0.22, 0.14, 0.75);
	text_at(cr, DESK_FONT_DISPLAY, "DEsk", cx, cy - R * 0.22, R * 0.12, false, 0.5);
	/* the pin it rests against */
	double pa = gauge_angle(NEEDLE_REST - 0.12);
	screw(cr, cx + cos(pa) * R * 0.69, cy + sin(pa) * R * 0.69, R * 0.035, 9u);
	/* the window the score shows in */
	double box[4];
	gauge_window(cx, cy, box);
	double wx = box[0], wy = box[1], ww = box[2], wh = box[3];
	rounded(cr, wx - 1.5, wy - 1.5, ww + 3.0, wh + 3.0, 2.5);
	cairo_set_source_rgb(cr, 0.16, 0.12, 0.08);
	cairo_fill(cr);
	g = cairo_pattern_create_linear(0, wy, 0, wy + wh);
	cairo_pattern_add_color_stop_rgb(g, 0.0, 0.80, 0.78, 0.72);
	cairo_pattern_add_color_stop_rgb(g, 0.3, 0.99, 0.98, 0.95);
	cairo_pattern_add_color_stop_rgb(g, 1.0, 0.82, 0.80, 0.74);
	cairo_rectangle(cr, wx, wy, ww, wh);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
}

/* The score in a meter's window, or a dash for none yet. */
static void gauge_score(cairo_t *cr, double cx, double cy, double score)
{
	double box[4];
	gauge_window(cx, cy, box);
	char sc[16];
	snprintf(sc, sizeof sc, "%s", "\xe2\x80\x93.\xe2\x80\x93");
	if (score > 0.0) {
		snprintf(sc, sizeof sc, "%.1f", score);
	}
	cairo_set_source_rgba(cr, 0.06, 0.05, 0.04, score > 0.0 ? 0.95 : 0.45);
	text_at(cr, DESK_FONT_ENGRAVE, sc, cx, box[1] + box[3] * 0.80, box[3] * 0.82, true, 0.5);
}

/* The needle at value v, over the dial, and the hub and the glass over it. */
static void needle_paint(cairo_t *cr, double cx, double cy, double v)
{
	double R = GAUGE_R, a = gauge_angle(v);
	double ux = cos(a), uy = sin(a), nx = -uy, ny = ux;
	cairo_save(cr);
	cairo_arc(cr, cx, cy, R * 0.835, 0.0, 2.0 * M_PI);
	cairo_clip(cr);
	/* its shadow on the dial, then it: a tapered pointer, and behind the
	 * pivot a short arm with the round counterweight that balances it */
	for (int pass = 0; pass < 2; pass++) {
		double ox = pass == 0 ? 1.6 : 0.0, oy = pass == 0 ? 2.6 : 0.0;
		double tip = R * 0.76, tail = R * 0.14, wide = R * 0.028;
		cairo_move_to(cr, cx + ox + ux * tip, cy + oy + uy * tip);
		cairo_line_to(cr, cx + ox + nx * wide, cy + oy + ny * wide);
		cairo_line_to(cr, cx + ox - ux * tail + nx * wide * 0.7,
			cy + oy - uy * tail + ny * wide * 0.7);
		cairo_line_to(cr, cx + ox - ux * tail - nx * wide * 0.7,
			cy + oy - uy * tail - ny * wide * 0.7);
		cairo_line_to(cr, cx + ox - nx * wide, cy + oy - ny * wide);
		cairo_close_path(cr);
		cairo_new_sub_path(cr);
		cairo_arc(cr, cx + ox - ux * tail, cy + oy - uy * tail, R * 0.05, 0.0, 2.0 * M_PI);
		if (pass == 0) {
			cairo_set_source_rgba(cr, 0.20, 0.14, 0.06, 0.22);
		} else {
			cairo_set_source_rgb(cr, 0.10, 0.10, 0.12);
		}
		cairo_fill(cr);
	}
	/* the hub, a brass cap */
	cairo_pattern_t *g = cairo_pattern_create_radial(cx - R * 0.03, cy - R * 0.04, R * 0.01,
		cx, cy, R * 0.09);
	cairo_pattern_add_color_stop_rgb(g, 0.0, 1.0, 0.94, 0.74);
	cairo_pattern_add_color_stop_rgb(g, 0.7, 0.70, 0.54, 0.26);
	cairo_pattern_add_color_stop_rgb(g, 1.0, 0.36, 0.26, 0.10);
	cairo_arc(cr, cx, cy, R * 0.09, 0.0, 2.0 * M_PI);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	/* the glass: the lamp's reflection in its top left, and its curve
	 * catching the light opposite */
	g = cairo_pattern_create_radial(cx - R * 0.42, cy - R * 0.52, R * 0.05,
		cx - R * 0.30, cy - R * 0.36, R * 0.80);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 1, 1, 1, 0.32);
	cairo_pattern_add_color_stop_rgba(g, 0.45, 1, 1, 1, 0.07);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 1, 1, 1, 0.0);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	cairo_set_line_width(cr, R * 0.03);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.20);
	cairo_arc(cr, cx, cy, R * 0.79, 0.15 * M_PI, 0.45 * M_PI);
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* A brass plate screwed on under a meter, what it measures cut into it. */
static void plate_paint(cairo_t *cr, double cx, double cy, const char *s, unsigned seed)
{
	double w = 118.0, h = 22.0, x = cx - w * 0.5, y = cy - h * 0.5;
	rounded(cr, x + 1.0, y + 2.0, w, h, 3.0);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.45);
	cairo_fill(cr);
	cairo_pattern_t *g = cairo_pattern_create_linear(0, y, 0, y + h);
	cairo_pattern_add_color_stop_rgb(g, 0.0, 0.95, 0.83, 0.55);
	cairo_pattern_add_color_stop_rgb(g, 0.5, 0.80, 0.64, 0.33);
	cairo_pattern_add_color_stop_rgb(g, 1.0, 0.60, 0.45, 0.20);
	rounded(cr, x, y, w, h, 3.0);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
	cairo_set_source_rgba(cr, 0.20, 0.13, 0.04, 0.7);
	cairo_set_line_width(cr, 0.8);
	cairo_stroke(cr);
	screw(cr, x + 7.0, cy, 2.6, seed * 3u);
	screw(cr, x + w - 7.0, cy, 2.6, seed * 3u + 1u);
	engraved(cr, s, cx, cy + 3.6, 9.5, w - 26.0);
}

/* A jewel lamp in a brass ring: amber, lit while its part is measured. */
static void lamp_paint(cairo_t *cr, double x, double y, bool lit)
{
	if (lit) {
		cairo_pattern_t *g = cairo_pattern_create_radial(x, y, 2.0, x, y, 18.0);
		cairo_pattern_add_color_stop_rgba(g, 0.0, 1.0, 0.70, 0.25, 0.45);
		cairo_pattern_add_color_stop_rgba(g, 1.0, 1.0, 0.55, 0.15, 0.0);
		cairo_arc(cr, x, y, 18.0, 0.0, 2.0 * M_PI);
		cairo_set_source(cr, g);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
	}
	screw(cr, x, y, 6.2, 17u);
	cairo_pattern_t *g = cairo_pattern_create_radial(x - 1.2, y - 1.4, 0.4, x, y, 4.4);
	if (lit) {
		cairo_pattern_add_color_stop_rgb(g, 0.0, 1.0, 0.97, 0.80);
		cairo_pattern_add_color_stop_rgb(g, 0.5, 1.0, 0.72, 0.22);
		cairo_pattern_add_color_stop_rgb(g, 1.0, 0.80, 0.36, 0.05);
	} else {
		cairo_pattern_add_color_stop_rgb(g, 0.0, 0.62, 0.34, 0.12);
		cairo_pattern_add_color_stop_rgb(g, 0.6, 0.34, 0.15, 0.04);
		cairo_pattern_add_color_stop_rgb(g, 1.0, 0.16, 0.06, 0.02);
	}
	cairo_arc(cr, x, y, 4.4, 0.0, 2.0 * M_PI);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	cairo_arc(cr, x - 1.3, y - 1.5, 1.1, 0.0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.65);
	cairo_fill(cr);
}

/* ---- the stamp ---- */

static void stamp_date(time_t when, char *out, size_t n)
{
	struct tm tm;
	localtime_r(&when, &tm);
	if (desk_lang_wide(desk_lang_get())) {
		snprintf(out, n, "%d.%d.%d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
	} else {
		snprintf(out, n, "%d %s %d", tm.tm_mday, desk_month_name(tm.tm_mon, DESK_NAME_SHORT),
			tm.tm_year + 1900);
	}
}

/* A five pointed star, of radius r, at x, y. */
static void star(cairo_t *cr, double x, double y, double r)
{
	for (int k = 0; k < 10; k++) {
		double a = -M_PI / 2.0 + k * M_PI / 5.0, rr = k % 2 ? r * 0.42 : r;
		if (k == 0) {
			cairo_move_to(cr, x + cos(a) * rr, y + sin(a) * rr);
		} else {
			cairo_line_to(cr, x + cos(a) * rr, y + sin(a) * rr);
		}
	}
	cairo_close_path(cr);
	cairo_fill(cr);
}

/* The base score, stamped in red ink: a ring with BASE SCORE round its
 * top and the date round its bottom, the score big in the middle; never
 * quite evenly inked. `grow` is how far above the paper it still is. */
static void stamp_paint(cairo_t *cr, double score, time_t when, double alpha, double grow)
{
	double r = STAMP_R;
	unsigned seed = (unsigned)(score * 10.0) + (unsigned)when;
	cairo_save(cr);
	cairo_translate(cr, STAMP_X, STAMP_Y);
	cairo_rotate(cr, -0.13 + (noise(seed) - 0.5) * 0.06);
	cairo_scale(cr, grow, grow);
	/* inked on a sheet of its own, as big as the stamp and no bigger */
	cairo_arc(cr, 0, 0, r * 1.04, 0.0, 2.0 * M_PI);
	cairo_clip(cr);
	cairo_push_group(cr);
	cairo_set_source_rgb(cr, 0.62, 0.09, 0.12);
	cairo_set_line_width(cr, r * 0.065);
	cairo_arc(cr, 0, 0, r - r * 0.035, 0.0, 2.0 * M_PI);
	cairo_stroke(cr);
	cairo_set_line_width(cr, r * 0.025);
	cairo_arc(cr, 0, 0, r * 0.74, 0.0, 2.0 * M_PI);
	cairo_stroke(cr);
	arc_text(cr, W("BASE SCORE"), r * 0.855, -M_PI / 2.0, r * 0.15, false);
	char date[48];
	stamp_date(when, date, sizeof date);
	arc_text(cr, date, r * 0.855, M_PI / 2.0, r * 0.12, true);
	star(cr, -r * 0.855, 0.0, r * 0.07);
	star(cr, r * 0.855, 0.0, r * 0.07);
	char sc[16];
	snprintf(sc, sizeof sc, "%.1f", score);
	text_at(cr, DESK_FONT_DISPLAY, sc, 0.0, r * 0.22, r * 0.66, true, 0.5);
	/* where the ink did not take: specks, in a few strengths, and a patch
	 * or two light */
	cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT);
	for (int pass = 0; pass < 3; pass++) {
		for (int i = pass; i < 420; i += 3) {
			double a = noise(seed + i * 3u) * 2.0 * M_PI, d = sqrt(noise(seed + i * 3u + 1u)) * r;
			cairo_new_sub_path(cr);
			cairo_arc(cr, cos(a) * d, sin(a) * d, 0.3 + noise(seed + i * 3u + 2u) * 1.1, 0.0,
				2.0 * M_PI);
		}
		cairo_set_source_rgba(cr, 0, 0, 0, 0.55 + 0.2 * pass);
		cairo_fill(cr);
	}
	for (int i = 0; i < 3; i++) {
		double px = (noise(seed + 900u + i) - 0.5) * r * 1.4;
		double py = (noise(seed + 950u + i) - 0.5) * r * 1.4;
		cairo_pattern_t *g = cairo_pattern_create_radial(px, py, 0.0, px, py, r * 0.45);
		cairo_pattern_add_color_stop_rgba(g, 0.0, 0, 0, 0, 0.35);
		cairo_pattern_add_color_stop_rgba(g, 1.0, 0, 0, 0, 0.0);
		cairo_set_source(cr, g);
		cairo_paint(cr);
		cairo_pattern_destroy(g);
	}
	cairo_pop_group_to_source(cr);
	cairo_paint_with_alpha(cr, 0.88 * alpha);
	cairo_restore(cr);
}

/* ---- pencil ---- */

/* A line in pencil from (x0, y) to (x1, y), never quite straight. */
static void pencil_line(cairo_t *cr, double x0, double x1, double y, unsigned seed)
{
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_width(cr, 1.6);
	cairo_set_source_rgba(cr, 0.24, 0.24, 0.26, 0.72);
	int n = (int)((x1 - x0) / 6.0) + 1;
	for (int k = 0; k <= n; k++) {
		double x = x0 + (x1 - x0) * k / n;
		double yy = y + sin(x * 0.05 + seed) * 0.6 + (noise(seed + k) - 0.5) * 0.5;
		if (k == 0) {
			cairo_move_to(cr, x, yy);
		} else {
			cairo_line_to(cr, x, yy);
		}
	}
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* Where the stamp will go: a ring pencilled in, lightly, in dashes. */
static void stamp_place(cairo_t *cr)
{
	cairo_save(cr);
	double dash[2] = { 7.0, 6.0 };
	cairo_set_dash(cr, dash, 2, 0.0);
	cairo_set_line_width(cr, 1.3);
	cairo_set_source_rgba(cr, 0.26, 0.26, 0.28, 0.35);
	cairo_arc(cr, STAMP_X, STAMP_Y, STAMP_R - 2.0, 0.0, 2.0 * M_PI);
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* A word or two written in pencil at the foot, right aligned at x; ringed
 * when the hand is on it. Its box, for the hand to find. */
static void pencil_words(cairo_t *cr, const char *s, double x, double y, bool hot,
	double box[4])
{
	double size = 21.0;
	double w = text_w(cr, DESK_FONT_HAND, s, size, false);
	double x0 = x - w;
	cairo_set_source_rgba(cr, 0.22, 0.22, 0.25, hot ? 0.95 : 0.80);
	text_at(cr, DESK_FONT_HAND, s, x0, y, size, false, 0.0);
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_source_rgba(cr, 0.22, 0.22, 0.25, hot ? 0.90 : 0.60);
	if (hot) {
		/* ringed, once round and a little more, never an ellipse */
		cairo_set_line_width(cr, 1.7);
		double cx = x0 + w * 0.5, cy = y - size * 0.32, rx = w * 0.5 + 12.0, ry = size * 0.75;
		for (int k = 0; k <= 64; k++) {
			double t = k / 64.0, a = -2.3 + t * (2.0 * M_PI + 0.5);
			double wob = 1.0 + 0.04 * sin(a * 3.0) + 0.05 * t;
			double px = cx + cos(a) * rx * wob, py = cy + sin(a) * ry * wob - t;
			if (k == 0) {
				cairo_move_to(cr, px, py);
			} else {
				cairo_line_to(cr, px, py);
			}
		}
	} else {
		/* underlined, with a flick */
		cairo_set_line_width(cr, 1.4);
		cairo_move_to(cr, x0 - 2.0, y + 6.0);
		cairo_curve_to(cr, x0 + w * 0.35, y + 7.5, x0 + w * 0.7, y + 4.5, x + 6.0, y + 5.5);
	}
	cairo_stroke(cr);
	cairo_restore(cr);
	box[0] = x0 - 14.0;
	box[1] = y - size - 4.0;
	box[2] = w + 28.0;
	box[3] = size + 16.0;
}

/* ---- the window ---- */

struct buffer {
	struct wl_buffer *wl;
	cairo_surface_t *surface;
	void *data;
	size_t size;
	int w, h;
	bool busy;
};

struct output {
	struct wl_output *wl;
	uint32_t name;
	int scale;
	bool on;                  /* the sheet is on it */
};

#define MAX_OUTPUTS 8

struct app {
	struct wl_display *dpy;
	struct wl_registry *reg;
	struct wl_compositor *comp;
	struct wl_shm *shm;
	struct xdg_wm_base *wm;
	struct zxdg_decoration_manager_v1 *decoman;
	struct wl_seat *seat;
	struct wl_pointer *ptr;
	struct wl_keyboard *kbd;
	struct wl_surface *surf;
	struct xdg_surface *xsurf;
	struct xdg_toplevel *top;
	struct zxdg_toplevel_decoration_v1 *deco;
	struct wl_callback *frame;      /* the compositor has not asked for the next yet */
#ifdef DESK_HAVE_WL_CURSOR
	struct wl_cursor_theme *cursor_theme;
	struct wl_surface *cursor_surf;
#endif
	struct output outs[MAX_OUTPUTS];
	int nout;
	int scale;                      /* buffer pixels to a point */
	struct buffer buf[2];
	int w, h;
	int pending_w, pending_h;
	int pending_mode;               /* 0 its own size, 1 maximised or full screen, 2 resized */
	int own_w, own_h;
	bool configured_once, closed, dirty;
	bool own_paper;                 /* not framed by the desk: the sheet is its own paper */
	/* what does not move, drawn once for as long as it stays the same: the
	 * panel, and the panel with the report over it */
	cairo_surface_t *fixtures, *still;
	bool fixtures_dirty, still_dirty;
	unsigned still_gen, shown_gen;
	/* the hand */
	double px, py;
	uint32_t enter_serial;
	bool hot, pressed;
	const char *cursor;
	double ctl[4];                  /* "Rate again" or "Stop", on the sheet */
	/* the rating kept, and the one being made */
	struct rating kept, shown;
	struct meter m;
	pthread_t th;
	bool running;
	int part;
	double frac, live;
	int note;                       /* NOTE_NONE, or why the last run has no rating */
	int failed;                     /* and for NOTE_FAILED, the part it could not measure */
	int wake[2];
	/* what moves */
	double pos[N_PARTS], vel[N_PARTS];
	double clock, last_tick, last_draw;
	double stamp_t;                 /* the stamp coming down: seconds since; < -1 not */
	bool stamp_down;
	char machine[200];
};

/* The sheet's place in the window: scaled to fit, in the middle. */
static void sheet_place(const struct app *a, double *k, double *ox, double *oy)
{
	double kw = a->w / SHEET_W, kh = a->h / SHEET_H;
	*k = kw < kh ? kw : kh;
	*ox = (a->w - SHEET_W * *k) * 0.5;
	*oy = (a->h - SHEET_H * *k) * 0.5;
}

enum { NOTE_NONE, NOTE_STOPPED, NOTE_FAILED };

/* Why the last run left no rating, in a line. */
static void note_words(const struct app *a, char *out, size_t n)
{
	if (a->note == NOTE_FAILED && a->failed >= 0 && a->failed < N_PARTS) {
		snprintf(out, n, W("Could not measure: %s."), W(PART_NAME[a->failed]));
	} else if (a->note == NOTE_STOPPED) {
		snprintf(out, n, "%s", W("Stopped before it was done."));
	} else {
		snprintf(out, n, "%s", W("Not rated yet."));
	}
}

/* What the machine is, as the report says it at the foot. */
static void machine_line(const struct app *a, char *out, size_t n)
{
	const struct rating *r = &a->shown;
	if (r->cpu_name[0] && r->threads > 0 && r->ram_gib > 0.0) {
		snprintf(out, n, W("%s, %d threads, %.1f GB of memory"), r->cpu_name, r->threads,
			r->ram_gib);
	} else {
		snprintf(out, n, "%s", a->machine);
	}
}

static void rows_paint(cairo_t *cr, const struct app *a)
{
	const double ink[3] = { 0.13, 0.12, 0.11 };
	for (int i = 0; i < N_PARTS; i++) {
		double y = ROW_Y + i * ROW_H;
		const struct rating *r = &a->shown;
		bool known = r->score[i] > 0.0;
		bool now = a->running && a->part == i;
		typed(cr, W(PART_NAME[i]), ROW_X, y + 14.0, 13.0, true, ink[0], ink[1], ink[2], 0.95,
			TEXT_X - 70.0 - ROW_X, (unsigned)i + 1u);
		char sc[16];
		snprintf(sc, sizeof sc, "%s", "\xe2\x80\x93.\xe2\x80\x93");
		if (known) {
			snprintf(sc, sizeof sc, "%.1f", r->score[i]);
		}
		double sw = text_w(cr, DESK_FONT_TYPE, sc, 17.0, true);
		typed(cr, sc, TEXT_X - 18.0 - sw, y + 15.0, 17.0, true, ink[0], ink[1], ink[2],
			known ? 0.95 : 0.35, 0.0, (unsigned)i + 11u);
		if (known) {
			typed(cr, part_meaning(r, i), TEXT_X, y + 13.0, 12.5, false, ink[0], ink[1], ink[2],
				0.92, TEXT_W, (unsigned)i + 21u);
			char fig[300];
			part_figures(r, i, fig, sizeof fig);
			typed(cr, fig, TEXT_X, y + 29.0, 10.5, false, 0.30, 0.28, 0.26, 0.85, TEXT_W,
				(unsigned)i + 31u);
		} else if (now) {
			typed(cr, W("measuring"), TEXT_X, y + 13.0, 12.5, false, ink[0], ink[1], ink[2], 0.6,
				0.0, (unsigned)i + 41u);
		}
	}
}

/* The foot: the base score and what it means, or what is being done. */
static void foot_paint(cairo_t *cr, struct app *a)
{
	const struct rating *r = &a->shown;
	char lines[4][256];
	double y = FOOT_Y + 18.0;
	if (a->running) {
		stamp_place(cr);
		const char *doing = a->part >= 0 && a->part < N_PARTS ? W(PART_DOING[a->part])
			: W("Getting ready");
		typed(cr, doing, TEXT_X, y, 13.0, true, 0.13, 0.12, 0.11, 0.95, TEXT_W, 51u);
		int n = wrap(cr, W("Keep your hands off the machine for twenty seconds or so."), 11.5,
			false, TEXT_W, lines, 2);
		for (int k = 0; k < n; k++) {
			typed(cr, lines[k], TEXT_X, y + 20.0 + k * 15.0, 11.5, false, 0.28, 0.26, 0.24, 0.9,
				0.0, 52u + (unsigned)k);
		}
	} else if (r->when > 0) {
		if (a->stamp_down) {
			stamp_paint(cr, r->base, r->when, 1.0, 1.0);
		}
		int n = wrap(cr, base_meaning(r->base), 13.0, true, TEXT_W, lines, 2);
		for (int k = 0; k < n; k++) {
			typed(cr, lines[k], TEXT_X, y + k * 17.0, 13.0, true, 0.13, 0.12, 0.11, 0.95, 0.0,
				61u + (unsigned)k);
		}
		y += n * 17.0 + 4.0;
		n = wrap(cr, W("The base score is the lowest of the five: the desk is only as quick as "
			"its slowest part."), 11.5, false, TEXT_W, lines, 3);
		for (int k = 0; k < n; k++) {
			typed(cr, lines[k], TEXT_X, y + k * 15.0, 11.5, false, 0.28, 0.26, 0.24, 0.9, 0.0,
				65u + (unsigned)k);
		}
		y += n * 15.0 + 6.0;
		int low = rating_lowest(r);
		char held[200];
		snprintf(held, sizeof held, W("Held back by: %s."), low >= 0 ? W(PART_NAME[low]) : "?");
		typed(cr, held, TEXT_X, y, 12.0, false, 0.13, 0.12, 0.11, 0.95, TEXT_W - 190.0, 71u);
		if (r->busy > BUSY_NOTE) {
			char busy[300];
			snprintf(busy, sizeof busy, W("Other programs kept the machine %d%% busy while it "
				"was rated, so the scores may be low."), (int)(r->busy * 100.0 + 0.5));
			n = wrap(cr, busy, 11.0, false, TEXT_W - 190.0, lines, 2);
			for (int k = 0; k < n; k++) {
				typed(cr, lines[k], TEXT_X, y + 18.0 + k * 14.0, 11.0, false, 0.55, 0.12, 0.10,
					0.9, 0.0, 73u + (unsigned)k);
			}
		}
	} else {
		stamp_place(cr);
		char note[200];
		note_words(a, note, sizeof note);
		typed(cr, note, TEXT_X, y, 13.0, true, 0.13, 0.12, 0.11, 0.95, TEXT_W, 81u);
	}
	/* when, and on what */
	char when[160], line[300];
	if (!a->running && r->when > 0) {
		char w[96];
		when_words(r->when, w, sizeof w);
		snprintf(when, sizeof when, W("Rated %s."), w);
		if (a->note != NOTE_NONE) {
			note_words(a, when, sizeof when);
		}
		typed(cr, when, TEXT_X, SHEET_H - 30.0, 10.5, false, 0.30, 0.28, 0.26, 0.85,
			TEXT_W - 190.0, 91u);
	}
	machine_line(a, line, sizeof line);
	typed(cr, line, TEXT_X, SHEET_H - 15.0, 10.5, false, 0.30, 0.28, 0.26, 0.85, TEXT_W, 92u);
	pencil_words(cr, W(a->running ? "Stop" : r->when > 0 ? "Rate again" : "Rate this machine"),
		SHEET_W - 26.0, SHEET_H - 52.0, a->hot, a->ctl);
}

/* The instrument panel and what is fixed to it: the same until the sheet
 * changes its size or its language. */
static void fixtures_paint(cairo_t *cr)
{
	panel_paint(cr);
	for (int i = 0; i < N_PARTS; i++) {
		double cx = gauge_x(i);
		gauge_paint(cr, cx, GAUGE_Y);
		plate_paint(cr, cx, PLATE_Y, W(PART_PLATE[i]), (unsigned)i + 5u);
	}
}

/* Over that, everything that stays put between one change in the rating
 * and the next. */
static void still_paint(cairo_t *cr, struct app *a)
{
	for (int i = 0; i < N_PARTS; i++) {
		double cx = gauge_x(i);
		gauge_score(cr, cx, GAUGE_Y, a->shown.score[i]);
		lamp_paint(cr, cx + GAUGE_R * 0.86, GAUGE_Y - GAUGE_R * 0.86,
			a->running && a->part == i);
	}
	rows_paint(cr, a);
	foot_paint(cr, a);
}

/* And what moves: the needles, the pencil going along as the parts are
 * measured, the stamp coming down. */
static void live_paint(cairo_t *cr, const struct app *a)
{
	for (int i = 0; i < N_PARTS; i++) {
		needle_paint(cr, gauge_x(i), GAUGE_Y, a->pos[i]);
	}
	if (a->running) {
		double done = a->part < 0 ? 0.0 : (a->part + a->frac) / N_PARTS;
		if (done > 0.0) {
			pencil_line(cr, TEXT_X, TEXT_X + (TEXT_W - 10.0) * done, FOOT_Y + 74.0, 5u);
		}
	}
	if (a->stamp_t >= 0.0 && !a->stamp_down) {
		double e = a->stamp_t / STAMP_FALL;
		e = e > 1.0 ? 1.0 : e;
		stamp_paint(cr, a->shown.base, a->shown.when, 0.3 + 0.7 * e, 1.0 + 0.35 * (1.0 - e * e));
	}
}

/* the parts of the sheet that move, for the compositor to look at again */
static const double LIVE_BOXES[2][4] = {
	{ PANEL_X, PANEL_Y, PANEL_W, PANEL_H },
	{ 0.0, FOOT_Y - 30.0, SHEET_W, SHEET_H - FOOT_Y + 30.0 },
};

static int shm_file(size_t size)
{
	int fd = -1;
#ifdef MFD_CLOEXEC
	fd = memfd_create("desk-index", MFD_CLOEXEC);
#endif
	if (fd < 0) {
		char name[64];
		snprintf(name, sizeof name, "/desk-index-%ld-%ld", (long)getpid(), (long)time(NULL));
		fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			shm_unlink(name);
		}
	}
	if (fd < 0) {
		return -1;
	}
	if (ftruncate(fd, (off_t)size) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void buffer_release(void *data, struct wl_buffer *wl)
{
	(void)wl;
	struct buffer *b = data;
	b->busy = false;
}

static const struct wl_buffer_listener buffer_listener = { .release = buffer_release };

static void buffer_free(struct buffer *b)
{
	if (b->wl) {
		wl_buffer_destroy(b->wl);
	}
	if (b->surface) {
		cairo_surface_destroy(b->surface);
	}
	if (b->data) {
		munmap(b->data, b->size);
	}
	memset(b, 0, sizeof *b);
}

static bool buffer_make(struct app *a, struct buffer *b, int w, int h)
{
	buffer_free(b);
	int stride = w * 4;
	size_t size = (size_t)stride * h;
	int fd = shm_file(size);
	if (fd < 0) {
		return false;
	}
	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		close(fd);
		return false;
	}
	struct wl_shm_pool *pool = wl_shm_create_pool(a->shm, fd, (int32_t)size);
	b->wl = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	wl_buffer_add_listener(b->wl, &buffer_listener, b);
	b->data = data;
	b->size = size;
	b->w = w;
	b->h = h;
	b->surface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, w, h, stride);
	return true;
}

/* A plain sheet of cream paper, for a compositor that will not put this on
 * its own: a little darker toward its edges, and its fibres. */
static void paper_paint(cairo_t *cr, int w, int h)
{
	cairo_set_source_rgb(cr, 0.955, 0.935, 0.880);
	cairo_paint(cr);
	cairo_pattern_t *g = cairo_pattern_create_radial(w * 0.5, h * 0.45, 0.0, w * 0.5, h * 0.45,
		hypot(w, h) * 0.6);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 1, 1, 1, 0.0);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 0.45, 0.35, 0.20, 0.10);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	for (int i = 0; i < w * h / 400; i++) {
		uint32_t k = (uint32_t)i * 5u;
		cairo_rectangle(cr, noise(k) * w, noise(k + 1u) * h, 1.0 + noise(k + 2u) * 3.0, 0.6);
	}
	cairo_set_source_rgba(cr, 0.40, 0.32, 0.20, 0.06);
	cairo_fill(cr);
}

/* A layer w by h, made again if it is not that size: whether it was. */
static bool layer_fit(cairo_surface_t **s, int w, int h)
{
	if (*s && cairo_image_surface_get_width(*s) == w && cairo_image_surface_get_height(*s) == h) {
		return false;
	}
	if (*s) {
		cairo_surface_destroy(*s);
	}
	*s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	return true;
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
	(void)time;
	struct app *a = data;
	wl_callback_destroy(cb);
	a->frame = NULL;
}

static const struct wl_callback_listener frame_listener = { .done = frame_done };

static void draw(struct app *a)
{
	if (!a->configured_once || a->closed) {
		return;
	}
	struct buffer *b = !a->buf[0].busy ? &a->buf[0] : !a->buf[1].busy ? &a->buf[1] : NULL;
	if (!b || a->frame) {
		a->dirty = true;   /* when one comes back, or the compositor asks */
		return;
	}
	int bw = a->w * a->scale, bh = a->h * a->scale;
	if (b->w != bw || b->h != bh || !b->wl) {
		if (!buffer_make(a, b, bw, bh)) {
			return;
		}
	}
	double k, ox, oy;
	sheet_place(a, &k, &ox, &oy);
	if (layer_fit(&a->fixtures, bw, bh) || a->fixtures_dirty) {
		cairo_t *cr = cairo_create(a->fixtures);
		/* nothing under the ink: the paper is the desk's, under this --
		 * unless this is not on the desk, and nobody frames it */
		cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		cairo_paint(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		if (a->own_paper) {
			paper_paint(cr, bw, bh);
		}
		cairo_scale(cr, a->scale, a->scale);
		cairo_translate(cr, ox, oy);
		cairo_scale(cr, k, k);
		fixtures_paint(cr);
		cairo_destroy(cr);
		cairo_surface_flush(a->fixtures);
		a->fixtures_dirty = false;
		a->still_dirty = true;
	}
	if (layer_fit(&a->still, bw, bh) || a->still_dirty) {
		cairo_t *cr = cairo_create(a->still);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_surface(cr, a->fixtures, 0, 0);
		cairo_paint(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		cairo_scale(cr, a->scale, a->scale);
		cairo_translate(cr, ox, oy);
		cairo_scale(cr, k, k);
		still_paint(cr, a);
		cairo_destroy(cr);
		cairo_surface_flush(a->still);
		a->still_dirty = false;
		a->still_gen++;
	}
	cairo_t *cr = cairo_create(b->surface);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(cr, a->still, 0, 0);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_scale(cr, a->scale, a->scale);
	cairo_translate(cr, ox, oy);
	cairo_scale(cr, k, k);
	live_paint(cr, a);
	cairo_destroy(cr);
	cairo_surface_flush(b->surface);
	wl_surface_set_buffer_scale(a->surf, a->scale);
	wl_surface_attach(a->surf, b->wl, 0, 0);
	if (a->shown_gen != a->still_gen) {
		wl_surface_damage_buffer(a->surf, 0, 0, bw, bh);
	} else {
		for (int i = 0; i < 2; i++) {
			const double *bx = LIVE_BOXES[i];
			double s = k * a->scale;
			wl_surface_damage_buffer(a->surf, (int32_t)floor((ox + bx[0] * k) * a->scale) - 2,
				(int32_t)floor((oy + bx[1] * k) * a->scale) - 2, (int32_t)ceil(bx[2] * s) + 4,
				(int32_t)ceil(bx[3] * s) + 4);
		}
	}
	a->shown_gen = a->still_gen;
	a->frame = wl_surface_frame(a->surf);
	wl_callback_add_listener(a->frame, &frame_listener, a);
	wl_surface_commit(a->surf);
	b->busy = true;
	a->dirty = false;
	a->last_draw = now_s();
}

/* ---- the run, from the window's side ---- */

static double pan_of(const struct app *a, double sheet_x)
{
	double k, ox, oy;
	sheet_place(a, &k, &ox, &oy);
	double x = ox + sheet_x * k;
	return (x / (a->w > 0 ? a->w : 1)) * 0.6 - 0.3;
}

static void run_start(struct app *a, double settle)
{
	if (a->running) {
		return;
	}
	pthread_mutex_lock(&a->m.lock);
	a->m.part = -1;
	a->m.frac = a->m.live = 0.0;
	memset(&a->m.r, 0, sizeof a->m.r);
	a->m.finished = a->m.stopped = false;
	a->m.failed = -1;
	a->m.settle = settle;
	atomic_store(&a->m.stop, false);
	pthread_mutex_unlock(&a->m.lock);
	if (pthread_create(&a->th, NULL, meter_run, &a->m) != 0) {
		fprintf(stderr, "desk-index: cannot start measuring: %s\n", strerror(errno));
		return;
	}
	a->running = true;
	a->part = -1;
	a->frac = a->live = 0.0;
	a->note = NOTE_NONE;
	memset(&a->shown, 0, sizeof a->shown);
	a->stamp_t = -2.0;
	a->stamp_down = false;
	a->still_dirty = true;
	/* a lever thrown on the panel */
	desk_sound_play(DESK_SND_TOGGLE, pan_of(a, SHEET_W - 60.0), 0.8, 0.0);
}

static void run_stop(struct app *a)
{
	if (a->running) {
		atomic_store(&a->m.stop, true);
	}
}

/* What the measuring has got to, since it last said. */
static void run_sync(struct app *a)
{
	pthread_mutex_lock(&a->m.lock);
	int part = a->m.part, failed = a->m.failed;
	double frac = a->m.frac, live = a->m.live;
	bool finished = a->m.finished, stopped = a->m.stopped;
	struct rating r = a->m.r;
	pthread_mutex_unlock(&a->m.lock);
	if (!a->running) {
		return;
	}
	if (part != a->part) {
		if (a->part >= 0 && a->part < N_PARTS && part > a->part) {
			/* a part done: its needle let go, onto its score */
			desk_sound_play(DESK_SND_KNOB_TICK, pan_of(a, gauge_x(a->part)), 0.6, 0.0);
		}
		a->part = part;
		a->still_dirty = true;
	}
	a->frac = frac;
	a->live = live;
	for (int i = 0; i < N_PARTS; i++) {
		if (r.score[i] != a->shown.score[i]) {
			a->shown = r;
			a->still_dirty = true;
			break;
		}
	}
	if (!finished) {
		return;
	}
	pthread_join(a->th, NULL);
	a->running = false;
	if (!stopped) {
		a->kept = r;
		a->shown = r;
		rating_save(&r);
		/* the stamp comes down once the last needle has had a moment */
		a->stamp_t = -0.45;
	} else {
		a->shown = a->kept;
		a->note = failed >= 0 ? NOTE_FAILED : NOTE_STOPPED;
		a->failed = failed;
		a->stamp_down = a->kept.when > 0;
	}
	a->still_dirty = true;
}

/* Where each needle is going: its score, once its part is done; while it
 * is being measured, what it is heading for, hunting about it the way a
 * needle does on a live reading; feeling for it, before there is one. */
static double needle_target(const struct app *a, int i)
{
	double t = a->clock;
	if (a->running) {
		if (a->part == i) {
			double hunt = (0.30 * sin(t * 8.2 + i * 1.7) + 0.14 * sin(t * 21.0 + i))
				* (1.0 - 0.6 * a->frac);
			if (a->live > 0.0) {
				return a->live + hunt;
			}
			return 1.0 + 3.0 * (0.5 - 0.5 * cos(t * 3.1)) + hunt;
		}
		if (a->part < i || a->shown.score[i] <= 0.0) {
			return NEEDLE_REST;
		}
	}
	return a->shown.score[i] > 0.0 ? a->shown.score[i] : NEEDLE_REST;
}

/* A needle on a spring, damped enough to settle and not so much that it
 * does not go past first; stopped by its pins at both ends. Whether
 * anything is still moving. */
static bool needles_step(struct app *a, double dt)
{
	bool moving = false;
	const double k = 38.0, c = 2.0 * 0.32 * sqrt(38.0);
	for (int i = 0; i < N_PARTS; i++) {
		double target = needle_target(a, i);
		for (double left = dt; left > 0.0; left -= 1.0 / 240.0) {
			double h = left < 1.0 / 240.0 ? left : 1.0 / 240.0;
			a->vel[i] += (k * (target - a->pos[i]) - c * a->vel[i]) * h;
			a->pos[i] += a->vel[i] * h;
			if (a->pos[i] < NEEDLE_REST) {
				a->pos[i] = NEEDLE_REST;
				a->vel[i] = -a->vel[i] * 0.3;
			} else if (a->pos[i] > 10.2) {
				a->pos[i] = 10.2;
				a->vel[i] = -a->vel[i] * 0.3;
			}
		}
		if (fabs(target - a->pos[i]) > 0.004 || fabs(a->vel[i]) > 0.02) {
			moving = true;
		}
	}
	return moving;
}

/* The clock moved on: the needles and the stamp with it. Whether there is
 * anything moving to draw. */
static bool tick(struct app *a)
{
	double now = now_s();
	double dt = a->last_tick > 0.0 ? now - a->last_tick : 0.0;
	dt = dt > 0.1 ? 0.1 : dt;
	a->last_tick = now;
	a->clock += dt;
	bool moving = needles_step(a, dt) || a->running;
	if (a->stamp_t > -1.0 && !a->stamp_down) {
		double before = a->stamp_t;
		a->stamp_t += dt;
		if (before < STAMP_FALL && a->stamp_t >= STAMP_FALL) {
			desk_sound_play(DESK_SND_PAPER_PRESS, pan_of(a, STAMP_X), 1.0, 0.8);
		}
		if (a->stamp_t >= STAMP_FALL + 0.12) {
			a->stamp_down = true;
			a->still_dirty = true;
		}
		moving = true;
	}
	return moving || a->still_dirty || a->fixtures_dirty;
}

/* ---- the sheet ---- */

static void wm_ping(void *data, struct xdg_wm_base *wm, uint32_t serial)
{
	(void)data;
	xdg_wm_base_pong(wm, serial);
}

static const struct xdg_wm_base_listener wm_listener = { .ping = wm_ping };

static void xsurf_configure(void *data, struct xdg_surface *xs, uint32_t serial)
{
	struct app *a = data;
	xdg_surface_ack_configure(xs, serial);
	/* its own size, unless it is maximised, has the whole screen, or is
	 * being made bigger or smaller by hand */
	if (a->pending_mode && a->pending_w > 0 && a->pending_h > 0) {
		a->w = a->pending_w < 380 ? 380 : a->pending_w;
		a->h = a->pending_h < 300 ? 300 : a->pending_h;
		if (a->pending_mode == 2) {
			a->own_w = a->w;
			a->own_h = a->h;
		}
	} else {
		a->w = a->own_w;
		a->h = a->own_h;
	}
	a->configured_once = true;
	a->fixtures_dirty = true;
	draw(a);
}

static const struct xdg_surface_listener xsurf_listener = { .configure = xsurf_configure };

static void top_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h,
	struct wl_array *states)
{
	(void)t;
	struct app *a = data;
	a->pending_w = w;
	a->pending_h = h;
	a->pending_mode = 0;
	uint32_t *st;
	wl_array_for_each(st, states) {
		if (*st == XDG_TOPLEVEL_STATE_MAXIMIZED || *st == XDG_TOPLEVEL_STATE_FULLSCREEN) {
			a->pending_mode = 1;
		} else if (*st == XDG_TOPLEVEL_STATE_RESIZING && a->pending_mode == 0) {
			a->pending_mode = 2;
		}
	}
}

static void top_close(void *data, struct xdg_toplevel *t)
{
	(void)t;
	struct app *a = data;
	a->closed = true;
}

static const struct xdg_toplevel_listener top_listener = {
	.configure = top_configure,
	.close = top_close,
};

/* Whether the desk frames the sheet after all: a compositor that is not
 * DEsk may say it will not. */
static void deco_configure(void *data, struct zxdg_toplevel_decoration_v1 *d, uint32_t mode)
{
	(void)d;
	struct app *a = data;
	bool own = mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
	if (own != a->own_paper) {
		a->own_paper = own;
		a->fixtures_dirty = true;
	}
}

static const struct zxdg_toplevel_decoration_v1_listener deco_listener = {
	.configure = deco_configure,
};

/* The sheet is drawn as finely as the finest screen it is on. */
static void scale_update(struct app *a)
{
	int s = 1;
	for (int i = 0; i < a->nout; i++) {
		if (a->outs[i].on && a->outs[i].scale > s) {
			s = a->outs[i].scale;
		}
	}
	if (s != a->scale) {
		a->scale = s;
		a->fixtures_dirty = true;
		draw(a);
	}
}

static struct output *output_of(struct app *a, struct wl_output *o)
{
	for (int i = 0; i < a->nout; i++) {
		if (a->outs[i].wl == o) {
			return &a->outs[i];
		}
	}
	return NULL;
}

static void surf_enter(void *data, struct wl_surface *s, struct wl_output *o)
{
	(void)s;
	struct app *a = data;
	struct output *out = output_of(a, o);
	if (out) {
		out->on = true;
		scale_update(a);
	}
}

static void surf_leave(void *data, struct wl_surface *s, struct wl_output *o)
{
	(void)s;
	struct app *a = data;
	struct output *out = output_of(a, o);
	if (out) {
		out->on = false;
		scale_update(a);
	}
}

static void surf_pref_scale(void *data, struct wl_surface *s, int32_t factor)
{
	(void)data;
	(void)s;
	(void)factor;
}

static void surf_pref_transform(void *data, struct wl_surface *s, uint32_t transform)
{
	(void)data;
	(void)s;
	(void)transform;
}

static const struct wl_surface_listener surf_listener = {
	.enter = surf_enter,
	.leave = surf_leave,
	.preferred_buffer_scale = surf_pref_scale,
	.preferred_buffer_transform = surf_pref_transform,
};

static void out_geometry(void *data, struct wl_output *o, int32_t x, int32_t y, int32_t pw,
	int32_t ph, int32_t sub, const char *make, const char *model, int32_t transform)
{
	(void)data;
	(void)o;
	(void)x;
	(void)y;
	(void)pw;
	(void)ph;
	(void)sub;
	(void)make;
	(void)model;
	(void)transform;
}

static void out_mode(void *data, struct wl_output *o, uint32_t flags, int32_t w, int32_t h,
	int32_t refresh)
{
	(void)data;
	(void)o;
	(void)flags;
	(void)w;
	(void)h;
	(void)refresh;
}

static void out_done(void *data, struct wl_output *o)
{
	(void)data;
	(void)o;
}

static void out_scale(void *data, struct wl_output *o, int32_t factor)
{
	struct app *a = data;
	struct output *out = output_of(a, o);
	if (out && factor > 0) {
		out->scale = factor;
		scale_update(a);
	}
}

static void out_name(void *data, struct wl_output *o, const char *name)
{
	(void)data;
	(void)o;
	(void)name;
}

static void out_description(void *data, struct wl_output *o, const char *desc)
{
	(void)data;
	(void)o;
	(void)desc;
}

static const struct wl_output_listener out_listener = {
	.geometry = out_geometry,
	.mode = out_mode,
	.done = out_done,
	.scale = out_scale,
	.name = out_name,
	.description = out_description,
};

/* ---- the hand ---- */

static void cursor_set(struct app *a, const char *name)
{
	if (a->cursor == name || !a->ptr) {
		return;
	}
	a->cursor = name;
#ifdef DESK_HAVE_WL_CURSOR
	struct wl_cursor *c = a->cursor_theme
		? wl_cursor_theme_get_cursor(a->cursor_theme, name) : NULL;
	if (!c && a->cursor_theme && strcmp(name, "left_ptr")) {
		c = wl_cursor_theme_get_cursor(a->cursor_theme, "hand2");
	}
	if (!c && a->cursor_theme) {
		c = wl_cursor_theme_get_cursor(a->cursor_theme, "left_ptr");
	}
	if (c && c->image_count > 0) {
		struct wl_cursor_image *img = c->images[0];
		wl_pointer_set_cursor(a->ptr, a->enter_serial, a->cursor_surf,
			(int32_t)img->hotspot_x, (int32_t)img->hotspot_y);
		wl_surface_attach(a->cursor_surf, wl_cursor_image_get_buffer(img), 0, 0);
		wl_surface_damage(a->cursor_surf, 0, 0, (int32_t)img->width, (int32_t)img->height);
		wl_surface_commit(a->cursor_surf);
	}
#endif
}

/* Whether the hand is on "Rate again" (or "Stop"). */
static bool on_control(const struct app *a)
{
	double k, ox, oy;
	sheet_place(a, &k, &ox, &oy);
	double x = (a->px - ox) / k, y = (a->py - oy) / k;
	return a->ctl[2] > 0.0 && x >= a->ctl[0] && x <= a->ctl[0] + a->ctl[2]
		&& y >= a->ctl[1] && y <= a->ctl[1] + a->ctl[3];
}

static void track(struct app *a)
{
	bool hot = on_control(a);
	cursor_set(a, hot ? "pointer" : "left_ptr");
	if (hot != a->hot) {
		a->hot = hot;
		a->still_dirty = true;
	}
}

static void use_control(struct app *a)
{
	if (a->running) {
		run_stop(a);
	} else {
		run_start(a, 0.6);
	}
}

static void ptr_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s,
	wl_fixed_t x, wl_fixed_t y)
{
	(void)p;
	(void)s;
	struct app *a = data;
	a->enter_serial = serial;
	a->cursor = NULL;
	/* The language may have been changed meanwhile, in Shell Settings --
	 * not while measuring, though: the desk's drawing is being timed on the
	 * other thread, and it prints in the language. */
	enum desk_lang was = desk_lang_get();
	if (!a->running) {
		shell_conf_read();
		desk_sound_set_level(g_sound_level);
	}
	if (desk_lang_get() != was && a->top) {
		xdg_toplevel_set_title(a->top, W("DEsk Experience Index"));
		a->fixtures_dirty = true;
	}
	a->px = wl_fixed_to_double(x);
	a->py = wl_fixed_to_double(y);
	track(a);
}

static void ptr_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s)
{
	(void)p;
	(void)serial;
	(void)s;
	struct app *a = data;
	a->pressed = false;
	if (a->hot) {
		a->hot = false;
		a->still_dirty = true;
	}
}

static void ptr_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x,
	wl_fixed_t y)
{
	(void)p;
	(void)time;
	struct app *a = data;
	a->px = wl_fixed_to_double(x);
	a->py = wl_fixed_to_double(y);
	track(a);
}

static void ptr_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time,
	uint32_t button, uint32_t state)
{
	(void)p;
	(void)serial;
	(void)time;
	struct app *a = data;
	if (button != BTN_LEFT) {
		return;
	}
	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		a->pressed = on_control(a);
	} else {
		/* let go where it was taken hold of */
		if (a->pressed && on_control(a)) {
			use_control(a);
		}
		a->pressed = false;
	}
}

static void ptr_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis,
	wl_fixed_t value)
{
	(void)data;
	(void)p;
	(void)time;
	(void)axis;
	(void)value;
}

static const struct wl_pointer_listener ptr_listener = {
	.enter = ptr_enter,
	.leave = ptr_leave,
	.motion = ptr_motion,
	.button = ptr_button,
	.axis = ptr_axis,
};

static void kbd_keymap(void *data, struct wl_keyboard *k, uint32_t format, int32_t fd,
	uint32_t size)
{
	(void)data;
	(void)k;
	(void)format;
	(void)size;
	close(fd);
}

static void kbd_enter(void *data, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s,
	struct wl_array *keys)
{
	(void)data;
	(void)k;
	(void)serial;
	(void)s;
	(void)keys;
}

static void kbd_leave(void *data, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s)
{
	(void)data;
	(void)k;
	(void)serial;
	(void)s;
}

static void kbd_key(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t time,
	uint32_t key, uint32_t state)
{
	(void)k;
	(void)serial;
	(void)time;
	struct app *a = data;
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		return;
	}
	if (key == KEY_ESC) {
		a->closed = true;
	} else if (key == KEY_ENTER || key == KEY_SPACE || key == KEY_R) {
		use_control(a);
	}
}

static void kbd_modifiers(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t dep,
	uint32_t lat, uint32_t lock, uint32_t group)
{
	(void)data;
	(void)k;
	(void)serial;
	(void)dep;
	(void)lat;
	(void)lock;
	(void)group;
}

static void kbd_repeat(void *data, struct wl_keyboard *k, int32_t rate, int32_t delay)
{
	(void)data;
	(void)k;
	(void)rate;
	(void)delay;
}

static const struct wl_keyboard_listener kbd_listener = {
	.keymap = kbd_keymap,
	.enter = kbd_enter,
	.leave = kbd_leave,
	.key = kbd_key,
	.modifiers = kbd_modifiers,
	.repeat_info = kbd_repeat,
};

static void seat_caps(void *data, struct wl_seat *seat, uint32_t caps)
{
	struct app *a = data;
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !a->ptr) {
		a->ptr = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(a->ptr, &ptr_listener, a);
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !a->kbd) {
		a->kbd = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(a->kbd, &kbd_listener, a);
	}
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
	(void)data;
	(void)seat;
	(void)name;
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_caps,
	.name = seat_name,
};

static void reg_global(void *data, struct wl_registry *reg, uint32_t name, const char *iface,
	uint32_t version)
{
	struct app *a = data;
	if (!strcmp(iface, wl_compositor_interface.name)) {
		a->comp = wl_registry_bind(reg, name, &wl_compositor_interface,
			version < 4 ? version : 4);
	} else if (!strcmp(iface, wl_shm_interface.name)) {
		a->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
	} else if (!strcmp(iface, xdg_wm_base_interface.name)) {
		a->wm = wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(a->wm, &wm_listener, a);
	} else if (!strcmp(iface, zxdg_decoration_manager_v1_interface.name)) {
		a->decoman = wl_registry_bind(reg, name, &zxdg_decoration_manager_v1_interface, 1);
	} else if (!strcmp(iface, wl_seat_interface.name) && !a->seat) {
		a->seat = wl_registry_bind(reg, name, &wl_seat_interface, version < 4 ? version : 4);
		wl_seat_add_listener(a->seat, &seat_listener, a);
	} else if (!strcmp(iface, wl_output_interface.name) && version >= 2
		&& a->nout < MAX_OUTPUTS) {
		struct output *o = &a->outs[a->nout++];
		o->wl = wl_registry_bind(reg, name, &wl_output_interface, 2);
		o->name = name;
		o->scale = 1;
		wl_output_add_listener(o->wl, &out_listener, a);
	}
}

static void reg_remove(void *data, struct wl_registry *reg, uint32_t name)
{
	(void)reg;
	struct app *a = data;
	for (int i = 0; i < a->nout; i++) {
		if (a->outs[i].name == name) {
			wl_output_destroy(a->outs[i].wl);
			a->outs[i] = a->outs[--a->nout];
			scale_update(a);
			break;
		}
	}
}

static const struct wl_registry_listener reg_listener = {
	.global = reg_global,
	.global_remove = reg_remove,
};

static int run_window(void)
{
	struct app a;
	memset(&a, 0, sizeof a);
	pthread_mutex_init(&a.m.lock, NULL);
	a.w = a.own_w = (int)SHEET_W;
	a.h = a.own_h = (int)SHEET_H;
	a.scale = 1;
	a.stamp_t = -2.0;
	if (pipe2(a.wake, O_CLOEXEC | O_NONBLOCK) != 0) {
		fprintf(stderr, "desk-index: %s\n", strerror(errno));
		return 1;
	}
	a.m.wake = a.wake[1];
	for (int i = 0; i < N_PARTS; i++) {
		a.pos[i] = NEEDLE_REST;
	}
	bool kept = rating_load(&a.kept);
	a.shown = a.kept;
	a.stamp_down = kept;
	char cpu[128];
	cpu_name(cpu, sizeof cpu);
	snprintf(a.machine, sizeof a.machine, W("%s, %d threads, %.1f GB of memory"), cpu,
		cpu_threads(), ram_gib());

	a.dpy = wl_display_connect(NULL);
	if (!a.dpy) {
		fprintf(stderr, "desk-index: no Wayland display to open on (desk-index --text "
			"rates the machine without one)\n");
		return 1;
	}
	desk_sound_set_level(g_sound_level);
	desk_sound_init("DEsk Experience Index");
	a.reg = wl_display_get_registry(a.dpy);
	wl_registry_add_listener(a.reg, &reg_listener, &a);
	wl_display_roundtrip(a.dpy);
	if (!a.comp || !a.shm || !a.wm) {
		fprintf(stderr, "desk-index: the compositor lacks what a window needs\n");
		return 1;
	}
#ifdef DESK_HAVE_WL_CURSOR
	a.cursor_theme = wl_cursor_theme_load(getenv("XCURSOR_THEME"),
		getenv("XCURSOR_SIZE") && atoi(getenv("XCURSOR_SIZE")) > 0
			? atoi(getenv("XCURSOR_SIZE")) : 24, a.shm);
	a.cursor_surf = wl_compositor_create_surface(a.comp);
#endif
	a.surf = wl_compositor_create_surface(a.comp);
	wl_surface_add_listener(a.surf, &surf_listener, &a);
	a.xsurf = xdg_wm_base_get_xdg_surface(a.wm, a.surf);
	xdg_surface_add_listener(a.xsurf, &xsurf_listener, &a);
	a.top = xdg_surface_get_toplevel(a.xsurf);
	xdg_toplevel_add_listener(a.top, &top_listener, &a);
	xdg_toplevel_set_title(a.top, W("DEsk Experience Index"));
	xdg_toplevel_set_app_id(a.top, "desk-index");
	xdg_toplevel_set_min_size(a.top, 380, 300);
	/* the desk's frame: this is one of its sheets */
	a.own_paper = !a.decoman;
	if (a.decoman) {
		a.deco = zxdg_decoration_manager_v1_get_toplevel_decoration(a.decoman, a.top);
		zxdg_toplevel_decoration_v1_add_listener(a.deco, &deco_listener, &a);
		zxdg_toplevel_decoration_v1_set_mode(a.deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
	wl_surface_commit(a.surf);
	/* never rated here: rate it now, once the sheet is down */
	if (!kept) {
		run_start(&a, 1.5);
	}

	int fd = wl_display_get_fd(a.dpy);
	while (!a.closed) {
		while (wl_display_prepare_read(a.dpy) != 0) {
			if (wl_display_dispatch_pending(a.dpy) < 0) {
				a.closed = true;
				break;
			}
		}
		if (a.closed) {
			break;
		}
		wl_display_flush(a.dpy);
		bool moving = tick(&a);
		/* While a part is being measured, the needles are drawn thirty
		 * times a second and no more: what is drawn is taken from what is
		 * being measured. */
		double every = a.running ? 1.0 / 30.0 : 0.0;
		int timeout = -1;
		if ((moving || a.dirty) && a.configured_once) {
			double wait = a.last_draw + every - now_s();
			timeout = a.frame ? 50 : wait > 0.0 ? (int)(wait * 1000.0) + 1 : 0;
		}
		struct pollfd pfd[2] = { { fd, POLLIN, 0 }, { a.wake[0], POLLIN, 0 } };
		int r = poll(pfd, 2, timeout);
		if (r > 0 && (pfd[0].revents & POLLIN)) {
			if (wl_display_read_events(a.dpy) < 0) {
				break;
			}
		} else {
			wl_display_cancel_read(a.dpy);
			if (r > 0 && (pfd[0].revents & (POLLHUP | POLLERR))) {
				break;
			}
		}
		if (wl_display_dispatch_pending(a.dpy) < 0) {
			break;
		}
		if (r > 0 && (pfd[1].revents & POLLIN)) {
			char drain[64];
			while (read(a.wake[0], drain, sizeof drain) > 0) {
			}
			run_sync(&a);
		}
		moving = tick(&a);
		if ((moving || a.dirty) && !a.frame && now_s() - a.last_draw >= every) {
			draw(&a);
		}
	}

	if (a.running) {
		run_stop(&a);
		pthread_join(a.th, NULL);
	}
	buffer_free(&a.buf[0]);
	buffer_free(&a.buf[1]);
	if (a.still) {
		cairo_surface_destroy(a.still);
	}
	if (a.fixtures) {
		cairo_surface_destroy(a.fixtures);
	}
	if (a.frame) {
		wl_callback_destroy(a.frame);
	}
	if (a.deco) {
		zxdg_toplevel_decoration_v1_destroy(a.deco);
	}
	xdg_toplevel_destroy(a.top);
	xdg_surface_destroy(a.xsurf);
	wl_surface_destroy(a.surf);
#ifdef DESK_HAVE_WL_CURSOR
	if (a.cursor_theme) {
		wl_cursor_theme_destroy(a.cursor_theme);
	}
	if (a.cursor_surf) {
		wl_surface_destroy(a.cursor_surf);
	}
#endif
	if (a.ptr) {
		wl_pointer_destroy(a.ptr);
	}
	if (a.kbd) {
		wl_keyboard_destroy(a.kbd);
	}
	if (a.seat) {
		wl_seat_destroy(a.seat);
	}
	for (int i = 0; i < a.nout; i++) {
		wl_output_destroy(a.outs[i].wl);
	}
	if (a.decoman) {
		zxdg_decoration_manager_v1_destroy(a.decoman);
	}
	xdg_wm_base_destroy(a.wm);
	wl_shm_destroy(a.shm);
	wl_compositor_destroy(a.comp);
	wl_registry_destroy(a.reg);
	wl_display_disconnect(a.dpy);
	desk_sound_shutdown();
	close(a.wake[0]);
	close(a.wake[1]);
	pthread_mutex_destroy(&a.m.lock);
	return 0;
}

/* ---- desk-index --text ---- */

/* s, and spaces after it to make it cols wide on a terminal, where a
 * Japanese letter takes two. */
static void print_padded(const char *s, int cols)
{
	int w = 0;
	for (const char *p = s; *p;) {
		int k = letter_len(p);
		w += k >= 3 ? 2 : 1;
		p += k;
	}
	printf("%s%*s", s, cols > w ? cols - w : 0, "");
}

static int run_text(void)
{
	struct meter m;
	memset(&m, 0, sizeof m);
	pthread_mutex_init(&m.lock, NULL);
	m.wake = -1;
	m.text = true;
	meter_run(&m);
	pthread_mutex_destroy(&m.lock);
	if (m.stopped) {
		fprintf(stderr, "desk-index: the rating did not finish\n");
		return 1;
	}
	struct rating *r = &m.r;
	printf("%s\n", W("DEsk Experience Index"));
	for (int i = 0; i < N_PARTS; i++) {
		char fig[300];
		part_figures(r, i, fig, sizeof fig);
		printf("  ");
		print_padded(W(PART_NAME[i]), 18);
		printf("%.1f   %s\n", r->score[i], fig);
	}
	int low = rating_lowest(r);
	char held[200];
	snprintf(held, sizeof held, W("Held back by: %s."), low >= 0 ? W(PART_NAME[low]) : "?");
	printf("  ");
	print_padded(W("BASE SCORE"), 18);
	printf("%.1f   %s\n", r->base, held);
	printf("%s\n", base_meaning(r->base));
	if (r->busy > BUSY_NOTE) {
		printf(W("Other programs kept the machine %d%% busy while it was rated, so the scores "
			"may be low."), (int)(r->busy * 100.0 + 0.5));
		printf("\n");
	}
	fflush(stdout);
	rating_save(r);
	return 0;
}

/* desk-index --suggest: a quick look -- the graphics (or, with none, the
 * processor) marching for a couple of seconds, the processor's threads and
 * the memory -- and the Graphics level this machine is best started at,
 * printed as shell.conf takes it (ultra, high, medium, low or 2d), with
 * why on stderr. For the installers.
 *
 * What it goes by is how long the first room would take to trace: the
 * march's pixels a second against SUGGEST_HIGH_MPXS, the march a High room
 * is worth (measured: VMware's SVGA3D marches about 730 million a second
 * and traces a High room in about 6 s; eight threads of a Ryzen 7 7800X3D,
 * on the processor, 25 million and 161 s), and each level's share of that
 * (SUGGEST_COST). The highest level whose first room is ready within
 * SUGGEST_BUDGET_S is the one, as the memory allows; Ultra only with
 * graphics of its own (gpu.c desk_gl_name_own), eight threads and 12 GB, and no more than
 * Low where the desk is drawn on the processor. Graphics built into the
 * processor, or a virtual machine's, start at Medium, unless the looks say
 * so by a wide margin: the paper folding within SUGGEST_SHARED_FOLD_MS and a
 * High room within SUGGEST_SHARED_HIGH_S -- what these figures took for
 * smooth was measured on a desktop's processor in a virtual machine, and
 * laptops with the same figures were not (2026-11-16, on real machines: the
 * Control Panel's key, the calendar, the memo pad and the ALL APPS book
 * heavy at the level chosen, Medium better). A machine changes its mind in
 * Shell Settings. */
#define SUGGEST_HIGH_MPXS 4200.0
#define SUGGEST_BUDGET_S 60.0
#define SUGGEST_SHARED_FOLD_MS 3.0
#define SUGGEST_SHARED_HIGH_S 10.0

/* And what the processor makes of the paper moving, which the graphics have
 * nothing to do with: a letter the size of a window folded a frame at a
 * time, the heaviest thing any level draws every frame (the desk draws a
 * sheet in motion at the level's motion resolution: whole at High and up,
 * three quarters across at Medium, half at Low -- SUGGEST_MOTION, by the
 * pixel). The highest level that keeps a frame of it within
 * SUGGEST_FOLD_MS, which leaves the rest of a sixtieth of a second for
 * everything else. Measured: 3.7 ms on two threads of a Ryzen 7 7800X3D
 * (High), 6.8 on them held to an i3-6100U's pace (Medium) and 9.0 to a
 * laptop of 2010's (Low); going by the graphics alone all three had been
 * High, and the slower two folded paper at thirty frames a second. */
#define SUGGEST_FOLD_MS 5.0
static const double SUGGEST_MOTION[] = { 0.0, 0.25, 0.5625, 1.0, 1.0 };

static double now_fold_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* milliseconds a frame, or 0 if it could not be measured */
static double fold_ms(void)
{
	const int lw = 720, lh = 520;
	cairo_surface_t *letter = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, lw, lh);
	cairo_surface_t *frame = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, lw + 240, lh + 240);
	if (cairo_surface_status(letter) || cairo_surface_status(frame)) {
		cairo_surface_destroy(letter);
		cairo_surface_destroy(frame);
		return 0.0;
	}
	cairo_t *cr = cairo_create(letter);
	desk_draw_window_frame(cr, 0, 0, lw, lh, "Letter", true, 0, false);
	cairo_destroy(cr);
	struct desk_fold *f = desk_fold_new(letter, 7u, 120.0, NULL, "Letter");
	double ms = 0.0;
	if (f) {
		cr = cairo_create(frame);
		cairo_translate(cr, 120, 120);
		desk_fold_draw(cr, f, 0.5);   /* the first finds the faces: not timed */
		double t0 = now_fold_s(), took = 0.0;
		int n = 0;
		while (n < 4 || (took < 0.6 && n < 400)) {
			cairo_save(cr);
			cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
			cairo_paint(cr);
			cairo_restore(cr);
			desk_fold_draw(cr, f, (n % 24 + 0.5) / 24.0);
			cairo_surface_flush(frame);
			n++;
			took = now_fold_s() - t0;
		}
		ms = took * 1000.0 / n;
		cairo_destroy(cr);
		desk_fold_free(f);
	}
	cairo_surface_destroy(letter);
	cairo_surface_destroy(frame);
	return ms;
}

static int run_suggest(void)
{
	static const char *const LEVEL[] = { "2d", "low", "medium", "high", "ultra" };
	static const double SUGGEST_COST[] = { 0.12, 0.40, 0.79, 1.0, 3.0 };
	struct meter m;
	memset(&m, 0, sizeof m);
	pthread_mutex_init(&m.lock, NULL);
	m.wake = -1;
	m.part = -1;
	struct rating r;
	memset(&r, 0, sizeof r);
	int threads = cpu_threads();
	double ram = ram_gib();
	measure_gpu(&m, &r);
	pthread_mutex_destroy(&m.lock);
	bool hard = r.gpu_kind == GPU_HARD;
	double mpx = r.gpu_mpx > 0.0 ? r.gpu_mpx : 0.1;
	double high_s = SUGGEST_HIGH_MPXS / mpx;
	int top = hard ? 4 : 1;   /* drawn on the processor: Low at most */
	if (top == 4 && (!r.gpu_own || threads < 8 || ram < 12.0)) {
		top = 3;
	}
	if (ram < 2.0) {
		top = 0;
	} else if (ram < 3.0 && top > 1) {
		top = 1;
	}
	/* the paper moving, on the processor */
	double fold = fold_ms();
	int moving = 4;
	while (fold > 0.0 && moving > 0 && fold * SUGGEST_MOTION[moving] > SUGGEST_FOLD_MS) {
		moving--;
	}
	top = top < moving ? top : moving;
	bool shared = hard && !r.gpu_own;
	if (shared && top > 2 && !(fold > 0.0 && fold <= SUGGEST_SHARED_FOLD_MS
		&& high_s <= SUGGEST_SHARED_HIGH_S)) {
		top = 2;
	}
	int level = 0;
	for (int i = top; i > 0; i--) {
		if (high_s * SUGGEST_COST[i] <= SUGGEST_BUDGET_S) {
			level = i;
			break;
		}
	}
	fprintf(stderr, "desk-index: graphics %s (%s%s, %.0f million a second); %d threads; "
		"%.1f GB; a High room in about %.0f s; a sheet folding %.1f ms a frame, "
		"smooth up to %s -- %s\n", r.gpu_name,
		GPU_KIND[r.gpu_kind], shared ? ", built in or virtual" : hard ? ", its own" : "",
		r.gpu_mpx, threads, ram, high_s, fold, LEVEL[moving], LEVEL[level]);
	printf("%s\n", LEVEL[level]);
	return 0;
}

int main(int argc, char **argv)
{
	desk_compat();
	shell_conf_read();
	if (argc > 1 && !strcmp(argv[1], "--text")) {
		return run_text();
	}
	if (argc > 1 && !strcmp(argv[1], "--suggest")) {
		return run_suggest();
	}
	if (argc > 1 && !strcmp(argv[1], "--version")) {
		printf("%s\n", DESK_RELEASE);
		return 0;
	}
	if (argc > 1) {
		fprintf(stderr, "usage: desk-index [--text | --suggest | --version]\n"
			"  The DEsk Experience Index: how this machine does at what the desk asks\n"
			"  of it. --text rates it with no window and prints the figures; --suggest\n"
			"  takes a quick look and prints the Graphics level to start it at.\n");
		return strcmp(argv[1], "--help") && strcmp(argv[1], "-h") ? 2 : 0;
	}
	return run_window();
}
