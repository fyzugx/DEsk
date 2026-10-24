/* The desk's own words, in each language it speaks (i18n.h). */

#include "i18n.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* Every word the desk prints, and the same in each language, in the order
 * of enum desk_lang: English (the key), Japanese, Chinese (simplified),
 * Korean, Spanish, French, German. The English is as the code has it;
 * "context|English" where one English word is two in another language.
 * Kept short, as a plate or a key has room for -- each is what such a
 * thing would have printed on it there, not a gloss. NULL: as in English. */
static const char *const WORDS[][DESK_LANG_COUNT] = {
	/* a window's corner, and a binder sheet's, and a letter's */
	{ "Fold", "たたむ", "折起", "접기", "Doblar", "Plier", "Falten" },
	{ "Unfold", "広げる", "展开", "펼치기", "Desdoblar", "Déplier", "Entfalten" },
	{ "Refold", "元に戻す", "复原", "되돌리기", "Volver a doblar", "Replier", "Zurückfalten" },
	{ "Discard", "捨てる", "丢掉", "버리기", "Tirar", "Jeter", "Wegwerfen" },
	{ "Open", "開く", "打开", "열기", "Abrir", "Ouvrir", "Öffnen" },
	{ "Into the drawer",
		"引き出しへ",
		"放进抽屉",
		"서랍에 넣기",
		"Al cajón",
		"Dans le tiroir",
		"In die Schublade" },
	{ "Out of the drawer",
		"引き出しから出す",
		"从抽屉取出",
		"서랍에서 꺼내기",
		"Sacar del cajón",
		"Sortir du tiroir",
		"Aus der Schublade" },
	{ "Read", "読む", "阅读", "읽기", "Leer", "Lire", "Lesen" },
	{ "Mark read",
		"既読にする",
		"标为已读",
		"읽음으로 표시",
		"Marcar como leída",
		"Marquer comme lue",
		"Als gelesen markieren" },
	{ "Mark unread",
		"未読にする",
		"标为未读",
		"안 읽음으로 표시",
		"Marcar como no leída",
		"Marquer comme non lue",
		"Als ungelesen markieren" },
	{ "Window", "ウィンドウ", "窗口", "창", "Ventana", "Fenêtre", "Fenster" },
	{ "Settings", "設定", "设置", "설정", "Ajustes", "Réglages", "Einstellungen" },
	{ "Letter", "手紙", "信", "편지", "Carta", "Lettre", "Brief" },
	{ "Fold me like a letter.",
		"手紙のようにたたんでください。",
		"像折信一样把我折起来。",
		"편지처럼 접어 주세요.",
		"Dóblame como una carta.",
		"Pliez-moi comme une lettre.",
		"Falte mich wie einen Brief." },
	/* the plates along the back of the desk */
	{ "CONTROL PANEL \xe2\x86\x91",
		"コントロールパネル ↑",
		"控制面板 ↑",
		"제어판 ↑",
		"PANEL DE CONTROL ↑",
		"COMMANDES ↑",
		"BEDIENFELD ↑" },
	{ "POST \xe2\x86\x91",
		"ポスト ↑",
		"信箱 ↑",
		"우편함 ↑",
		"BUZÓN ↑",
		"COURRIER ↑",
		"POST ↑" },
	{ "POST %d \xe2\x86\x91",
		"ポスト %d ↑",
		"信箱 %d ↑",
		"우편함 %d ↑",
		"BUZÓN %d ↑",
		"COURRIER %d ↑",
		"POST %d ↑" },
	{ "ALL APPS",
		"すべてのアプリ",
		"所有应用",
		"모든 앱",
		"TODAS LAS APPS",
		"TOUTES LES APPS",
		"ALLE APPS" },
	{ "Control Panel",
		"コントロールパネル",
		"控制面板",
		"제어판",
		"Panel de control",
		"Panneau de commandes",
		"Bedienfeld" },
	/* the post box */
	{ "From", "差出人", "寄件人", "보낸 사람", "De", "De", "Von" },
	{ "%d more below",
		"ほか %d 通",
		"下面还有 %d 封",
		"아래에 %d통 더",
		"%d más debajo",
		"%d de plus en dessous",
		"%d weitere darunter" },
	{ "NO POST",
		"郵便はありません",
		"没有信件",
		"편지 없음",
		"SIN CORREO",
		"PAS DE COURRIER",
		"KEINE POST" },
	{ "Nothing in it",
		"何もありません",
		"里面是空的",
		"비어 있음",
		"No hay nada",
		"Rien dedans",
		"Nichts darin" },
	/* the console */
	{ "WIRELESS", "無線", "无线", "무선", "INALÁMBRICO", "SANS FIL", "FUNK" },
	{ "ON", "入", "开", "켬", "ON", "ON", "EIN" },
	{ "OFF", "切", "关", "끔", "OFF", "OFF", "AUS" },
	{ "VOLUME", "音量", "音量", "음량", "VOLUMEN", "VOLUME", "LAUTSTÄRKE" },
	{ "LAMP", "照明", "台灯", "조명", "LÁMPARA", "LAMPE", "LAMPE" },
	{ "SCREEN", "画面", "屏幕", "화면", "PANTALLA", "ÉCRAN", "BILDSCHIRM" },
	{ "POWER", "電源", "电源", "전원", "ENERGÍA", "ÉNERGIE", "STROM" },
	{ "SHUT DOWN", "電源を切る", "关机", "종료", "APAGAR", "ÉTEINDRE", "AUSSCHALTEN" },
	{ "RESTART", "再起動", "重启", "재시작", "REINICIAR", "REDÉMARRER", "NEUSTART" },
	{ "LOG OUT", "ログアウト", "注销", "로그아웃", "SALIR", "QUITTER", "ABMELDEN" },
	{ "LOCK", "ロック", "锁定", "잠금", "BLOQUEAR", "VERROUILLER", "SPERREN" },
	{ "BATTERY", "電池", "电池", "배터리", "BATERÍA", "BATTERIE", "AKKU" },
	{ "LINE", "AC", "AC", "AC", "CA", "SECTEUR", "NETZ" },
	{ "FULL", "満", "满", "완충", "LLENA", "PLEINE", "VOLL" },
	{ "NO LINE", "回線なし", "未连接", "연결 없음", "SIN CONEXIÓN", "HORS LIGNE", "OFFLINE" },
	/* the cards out from under the console's controls */
	{ "Wireless networks",
		"無線ネットワーク",
		"无线网络",
		"무선 네트워크",
		"Redes inalámbricas",
		"Réseaux sans fil",
		"Funknetze" },
	{ "Output", "出力", "输出", "출력", "Salida", "Sortie", "Ausgabe" },
	{ "Input", "入力", "输入", "입력", "Entrada", "Entrée", "Eingabe" },
	{ "CHOOSE", "選ぶ", "选择", "선택", "ELEGIR", "CHOISIR", "WÄHLEN" },
	{ "Not connected", "未接続", "未连接", "연결 안 됨", "Sin conexión", "Non connecté",
		"Nicht verbunden" },
	{ "%d connected", "%d 台接続中", "已连接 %d 个", "%d개 연결됨", "%d conectados", "%d connectés",
		"%d verbunden" },
	{ "RESCAN", "再検索", "重新扫描", "다시 검색", "BUSCAR", "RECHERCHER", "SUCHEN" },
	{ "SEARCH", "探す", "搜索", "찾기", "BUSCAR", "CHERCHER", "SUCHEN" },
	{ "Connected", "接続中", "已连接", "연결됨", "Conectado", "Connecté", "Verbunden" },
	{ "Saved", "保存済み", "已保存", "저장됨", "Guardada", "Enregistré", "Gespeichert" },
	{ "Password", "パスワード", "密码", "암호", "Contraseña", "Mot de passe", "Passwort" },
	{ "No password", "パスワードなし", "无密码", "암호 없음", "Sin contraseña", "Sans mot de passe",
		"Ohne Passwort" },
	{ "Paired", "ペアリング済み", "已配对", "페어링됨", "Emparejado", "Appairé", "Gekoppelt" },
	{ "Not paired", "未ペアリング", "未配对", "페어링 안 됨", "Sin emparejar", "Non appairé",
		"Nicht gekoppelt" },
	{ "In use", "使用中", "使用中", "사용 중", "En uso", "Utilisé", "In Gebrauch" },
	{ "Wireless is off",
		"無線は切れています",
		"无线已关闭",
		"무선이 꺼져 있음",
		"La conexión inalámbrica está apagada",
		"Le sans-fil est éteint",
		"Funk ist aus" },
	{ "Bluetooth is off",
		"Bluetooth は切れています",
		"蓝牙已关闭",
		"블루투스가 꺼져 있음",
		"Bluetooth está apagado",
		"Le Bluetooth est éteint",
		"Bluetooth ist aus" },
	{ "Not on this machine",
		"この機械にはありません",
		"本机没有",
		"이 기기에는 없음",
		"No hay en este equipo",
		"Absent de cette machine",
		"Auf diesem Rechner nicht vorhanden" },
	{ "That did not work",
		"うまくいきませんでした",
		"没有成功",
		"되지 않았습니다",
		"No ha funcionado",
		"Cela n’a pas marché",
		"Das hat nicht geklappt" },
	{ "NO BACKLIGHT",
		"調光なし",
		"无背光",
		"백라이트 없음",
		"SIN LUZ DE FONDO",
		"SANS RÉTROÉCLAIRAGE",
		"OHNE BELEUCHTUNG" },
	/* the clock's keys */
	{ "clock|CLOCK", "時計", "时钟", "시계", "RELOJ", "HEURE", "UHR" },
	{ "clock|DAY", "日付", "日期", "날짜", "FECHA", "DATE", "DATUM" },
	{ "clock|MEDIA", "音楽", "音乐", "음악", "MÚSICA", "MUSIQUE", "MUSIK" },
	{ "clock|POWER", "電源", "电源", "전원", "ENERGÍA", "ÉNERGIE", "STROM" },
	/* the room being set out */
	/* the calendar */
	{ "tab|YEAR", "年", "年", "연", "AÑO", "ANNÉE", "JAHR" },
	{ "tab|MONTH", "月", "月", "월", "MES", "MOIS", "MONAT" },
	{ "tab|WEEK", "週", "周", "주", "SEMANA", "SEMAINE", "WOCHE" },
	{ "tab|DAY", "日", "日", "일", "DÍA", "JOUR", "TAG" },
	{ "head|YEAR", "年", "年", "년", "AÑO", "ANNÉE", "JAHR" },
	{ "head|WEEK", "週目", "周", "주차", "SEMANA", "SEMAINE", "WOCHE" },
	{ "%d DAYS", "%d 日", "%d 天", "%d일", "%d DÍAS", "%d JOURS", "%d TAGE" },
	{ "DAY %d  \xc2\xb7  %d TO GO",
		"%d 日目  ·  残り %d 日",
		"第 %d 天  ·  还剩 %d 天",
		"%d일째  ·  %d일 남음",
		"DÍA %d  ·  QUEDAN %d",
		"JOUR %d  ·  ENCORE %d",
		"TAG %d  ·  NOCH %d" },
	{ "DAY %d", "%d 日目", "第 %d 天", "%d일째", "DÍA %d", "JOUR %d", "TAG %d" },
	{ "NOTES", "メモ", "备注", "메모", "NOTAS", "NOTES", "NOTIZEN" },
	{ "TO DO", "やること", "待办", "할 일", "PENDIENTE", "À FAIRE", "AUFGABEN" },
	{ "ALL DAY", "終日", "全天", "종일", "TODO EL DÍA", "JOURNÉE", "GANZTÄGIG" },
	{ "TODAY", "今日", "今天", "오늘", "HOY", "AUJOURD’HUI", "HEUTE" },
	{ "PLANS", "予定", "计划", "일정", "PLANES", "PROGRAMME", "TERMINE" },
	{ "STICKY NOTES",
		"付箋",
		"便利贴",
		"메모지",
		"NOTAS ADHESIVAS",
		"NOTES ADHÉSIVES",
		"HAFTNOTIZEN" },
	{ "pick a colour",
		"色を選ぶ",
		"选一种颜色",
		"색을 고르세요",
		"elige un color",
		"choisissez une couleur",
		"Farbe wählen" },
	{ "+  stick a note here",
		"＋  ここに付箋を貼る",
		"+  在这里贴一张",
		"+  여기에 메모 붙이기",
		"+  pega una nota aquí",
		"+  collez une note ici",
		"+  hier einen Zettel aufkleben" },
	{ "+%d more",
		"ほか %d 件",
		"还有 %d 条",
		"%d개 더",
		"+%d más",
		"+%d autres",
		"+%d weitere" },
	{ "click or drag along it for the time",
		"時刻はクリックかドラッグで",
		"点击或沿着它拖动来定时间",
		"클릭하거나 끌어서 시간을 정하세요",
		"haz clic o arrastra para la hora",
		"cliquez ou glissez pour l’heure",
		"klicken oder ziehen für die Uhrzeit" },
	{ "Enter: stick it on the calendar    Esc: stop",
		"Enter：カレンダーに貼る    Esc：やめる",
		"Enter：贴到日历上    Esc：取消",
		"Enter: 달력에 붙이기    Esc: 그만두기",
		"Intro: pegarla en el calendario    Esc: dejarlo",
		"Entrée\xc2\xa0: la coller au calendrier    Échap\xc2\xa0: arrêter",
		"Enter: an den Kalender kleben    Esc: abbrechen" },
	{ "Enter: stick it on the calendar   Esc: stop",
		"Enter：カレンダーに貼る   Esc：やめる",
		"Enter：贴到日历上   Esc：取消",
		"Enter: 달력에 붙이기   Esc: 그만두기",
		"Intro: pegarla en el calendario   Esc: dejarlo",
		"Entrée\xc2\xa0: la coller au calendrier   Échap\xc2\xa0: arrêter",
		"Enter: an den Kalender kleben   Esc: abbrechen" },
	{ "type to write a plan   click a line: change   right-click: peel off",
		"打つと予定になる   行をクリック：直す   右クリック：はがす",
		"打字写计划   点一行：修改   右键：撕下",
		"입력하면 일정   줄 클릭: 고치기   오른쪽 클릭: 떼기",
		"escribe un plan   clic en una línea: cambiar   clic derecho: despegar",
		"tapez un rendez-vous   clic sur une ligne\xc2\xa0: modifier   clic droit\xc2\xa0: décoller",
		"tippen für einen Termin   Zeile anklicken: ändern   Rechtsklick: abziehen" },
	/* the book of every program */
	{ "every program on this machine, A to Z",
		"この機械のすべてのプログラムを A から Z まで",
		"这台机器上的所有程序，从 A 到 Z",
		"이 컴퓨터의 모든 프로그램, A부터 Z까지",
		"todos los programas de esta máquina, de la A a la Z",
		"tous les programmes de cette machine, de A à Z",
		"alle Programme auf diesem Rechner, von A bis Z" },
	{ "write a name, or what it is for\xe2\x80\xa6",
		"名前か、何に使うものかを書く…",
		"写名字，或者它是做什么用的…",
		"이름이나 용도를 적으세요…",
		"escribe un nombre, o para qué sirve…",
		"écrivez un nom, ou à quoi il sert…",
		"Name oder Zweck schreiben…" },
	{ "%d FOUND",
		"%d 件",
		"找到 %d 个",
		"%d개 찾음",
		"%d ENCONTRADOS",
		"%d TROUVÉS",
		"%d GEFUNDEN" },
	{ "%d PROGRAMS",
		"全 %d 件",
		"共 %d 个程序",
		"프로그램 %d개",
		"%d PROGRAMAS",
		"%d PROGRAMMES",
		"%d PROGRAMME" },
	{ "Nothing answers to that.",
		"当てはまるものがありません。",
		"没有符合的程序。",
		"맞는 것이 없습니다.",
		"Nada coincide con eso.",
		"Rien ne correspond.",
		"Nichts passt dazu." },
	{ "Fewer letters, or what it is for?",
		"字を減らすか、何に使うものかで探してください。",
		"少写几个字，或者按用途找？",
		"글자를 줄이거나 용도로 찾아 보세요.",
		"¿Menos letras, o para qué sirve?",
		"Moins de lettres, ou à quoi il sert\xc2\xa0?",
		"Weniger Buchstaben, oder wofür es ist?" },
	{ "FILED IN",
		"収めてある所",
		"收在",
		"꽂혀 있는 곳",
		"ARCHIVADO EN",
		"CLASSÉ DANS",
		"ABGELEGT IN" },
	{ "ALSO FOUND BY",
		"ほかの呼び名",
		"也能这样找到",
		"다른 이름",
		"TAMBIÉN SE ENCUENTRA POR",
		"AUSSI TROUVÉ PAR",
		"AUCH ZU FINDEN ÜBER" },
	{ "PRESS ENTER TO OPEN",
		"Enter で開く",
		"按 Enter 打开",
		"Enter를 눌러 열기",
		"PULSA INTRO PARA ABRIR",
		"APPUYEZ SUR ENTRÉE POUR OUVRIR",
		"ZUM ÖFFNEN ENTER DRÜCKEN" },
	{ "CLICK ITS LINE TO OPEN",
		"行をクリックして開く",
		"点击它的那一行打开",
		"줄을 클릭해 열기",
		"HAZ CLIC EN SU LÍNEA PARA ABRIR",
		"CLIQUEZ SUR SA LIGNE POUR OUVRIR",
		"ZUM ÖFFNEN ZEILE ANKLICKEN" },
	{ "AN INDEX OF", "すべての", "所有", "모든", "ÍNDICE DE", "INDEX DE", "VERZEICHNIS" },
	{ "EVERY PROGRAM",
		"プログラムの索引",
		"程序的索引",
		"프로그램 색인",
		"TODOS LOS PROGRAMAS",
		"TOUS LES PROGRAMMES",
		"ALLER PROGRAMME" },
	{ "spine|ALL", "全", "所有", "모든", "TODAS", "TOUTES", "ALLE" },
	{ "spine|APPS", "アプリ", "应用", "앱", "APPS", "APPS", "APPS" },
	{ "spine|SEARCH", "検索", "搜索", "검색", "BUSCAR", "CHERCHER", "SUCHEN" },
	{ "spine|INDEX", "索引", "索引", "색인", "ÍNDICE", "INDEX", "INDEX" },
	{ "spine|PROGRAMS", "件", "个", "개", "PROGRAMAS", "PROGRAMMES", "PROGRAMME" },
	/* the memo */
	{ "MEMO", "メモ", "便签", "메모", "NOTAS", "MÉMO", "NOTIZ" },
	{ "write here\xe2\x80\xa6",
		"ここに書く…",
		"写在这里…",
		"여기에 쓰세요…",
		"escribe aquí…",
		"écrivez ici…",
		"hier schreiben…" },
	/* Shell Settings: the form printed on its sheet */
	{ "MORE", "その他", "更多", "더 보기", "MÁS", "PLUS", "MEHR" },
	/* the screen lock */
	{ "PASSWORD", "パスワード", "密码", "암호", "CONTRASEÑA", "MOT DE PASSE", "PASSWORT" },
	{ "Checking\xe2\x80\xa6",
		"確かめています…",
		"正在核对…",
		"확인하는 중…",
		"Comprobando…",
		"Vérification…",
		"Wird geprüft…" },
	{ "That is not it",
		"パスワードが違います",
		"密码不对",
		"암호가 틀렸습니다",
		"No es esa",
		"Ce n’est pas ça",
		"Das ist es nicht" },
	{ "Caps Lock is on",
		"Caps Lock がオンです",
		"大写锁定已打开",
		"Caps Lock이 켜져 있습니다",
		"Bloq Mayús está activado",
		"Verr. Maj est activé",
		"Feststelltaste ist an" },
	{ "Bring the lamp up slowly at login",
		"ログイン時にゆっくり明かりをつける",
		"登录时慢慢点亮台灯",
		"로그인할 때 조명을 천천히 켜기",
		"Encender la lámpara despacio al entrar",
		"Allumer la lampe doucement à la connexion",
		"Lampe bei der Anmeldung langsam aufdrehen" },
	{ "The room starts dark and the lamp comes up as it is made; the clock counts how far. "
		"Off, it is lit at once.",
		"部屋は暗いところから始まり、支度が進むにつれて明かりがつきます。進み具合は時計に"
		"出ます。切にすると最初から明るくなります。",
		"房间从黑暗中开始，随着准备进行台灯渐渐亮起，时钟显示进度。关闭后一开始就是亮的。",
		"방은 어두운 상태로 시작하고 준비가 진행되면서 조명이 켜집니다. 진행 상황은 시계"
		"에 나옵니다. 끄면 처음부터 밝습니다.",
		"La habitación empieza a oscuras y la lámpara se enciende a medida que se "
		"prepara; el reloj cuenta cuánto falta. Desactivado, se ilumina enseguida.",
		"La pièce commence dans le noir et la lampe s’allume à mesure qu’elle se prépare"
		"\xc2\xa0; l’horloge compte où elle en est. Désactivé, tout est éclairé d’emblée.",
		"Der Raum beginnt dunkel, und die Lampe geht an, während er entsteht; die Uhr "
		"zählt mit. Aus: sofort hell." },
	{ "Kept in %s. One program the other way about paper: paper.<program> = yes or no.",
		"%s に保存されます。紙に載せるかどうかを一つだけ逆にするには paper.<プログラム> "
		"= yes か no。",
		"保存在 %s。要让某个程序反过来是否放在纸上：paper.<程序> = yes 或 no。",
		"%s에 저장됩니다. 한 프로그램만 종이 위에 올리는 방식을 반대로 하려면: paper.<프"
		"로그램> = yes 또는 no.",
		"Se guarda en %s. Para un programa al revés en cuanto al papel: paper.<programa> "
		"= yes o no.",
		"Enregistré dans %s. Un programme à l’inverse pour le papier\xc2\xa0: paper.<programme> "
		"= yes ou no.",
		"Gespeichert in %s. Ein Programm beim Papier andersherum: paper.<Programm> = yes "
		"oder no." },
	{ "DAYS AND WEATHER",
		"記念日と天気",
		"纪念日与天气",
		"기념일과 날씨",
		"DÍAS Y TIEMPO",
		"JOURS ET MÉTÉO",
		"TAGE UND WETTER" },
	{ "Your birthday",
		"誕生日",
		"你的生日",
		"생일",
		"Tu cumpleaños",
		"Votre anniversaire",
		"Ihr Geburtstag" },
	{ "The real weather at the front door",
		"玄関に本当の天気を映す",
		"在门口显示真实的天气",
		"현관에 실제 날씨 보이기",
		"El tiempo real en la puerta",
		"La vraie météo à la porte d’entrée",
		"Das echte Wetter an der Haustür" },
	{ "Rain, snow and fog at the front door as they are where you live: the city your time "
		"zone is named after is looked up on open-meteo.com every half hour, and nothing "
		"else is sent. Off, it is always fine.",
		"お住まいの地域の雨・雪・霧を玄関に映します。タイムゾーンの都市の天気を 30 分ごと"
		"に open-meteo.com へ問い合わせます（送るのはその都市の位置だけです）。切にする"
		"と、いつも晴れです。",
		"门口显示你所在地的雨、雪和雾：每半小时向 open-meteo.com 查询你的时区所用城市的天"
		"气，不会发送其他任何信息。关闭后总是晴天。",
		"사는 곳의 비·눈·안개를 현관에 보여 줍니다. 30분마다 시간대 이름의 도시 날씨를 "
		"open-meteo.com에 묻고, 그 밖에는 아무것도 보내지 않습니다. 끄면 늘 맑습니다.",
		"Lluvia, nieve y niebla en la puerta tal como están donde vives: cada media hora "
		"se consulta en open-meteo.com la ciudad que da nombre a tu zona horaria, y no "
		"se envía nada más. Desactivado, siempre hace bueno.",
		"La pluie, la neige et le brouillard à la porte, comme là où vous vivez\xc2\xa0: la "
		"ville qui donne son nom à votre fuseau horaire est consultée sur open-meteo.com "
		"toutes les demi-heures, et rien d’autre n’est envoyé. Désactivé, il fait "
		"toujours beau.",
		"Regen, Schnee und Nebel an der Haustür, wie sie bei Ihnen sind: Die Stadt, nach "
		"der Ihre Zeitzone heißt, wird alle halbe Stunde bei open-meteo.com abgefragt, "
		"sonst wird nichts gesendet. Aus: immer schönes Wetter." },
	{ "Not set", "なし", "未设置", "없음", "Sin fijar", "Non défini", "Nicht gesetzt" },
	{ "month|\xe2\x80\x94", "— 月", "— 月", "—월", "—", "—", "—" },
	{ "day|\xe2\x80\x94", "— 日", "— 日", "—일", "—", "—", "—" },
	{ "day|%d", "%d 日", "%d 日", "%d일", "%d", "%d", "%d." },
	{ "January", "1 月", "1 月", "1월", "Enero", "Janvier", "Januar" },
	{ "February", "2 月", "2 月", "2월", "Febrero", "Février", "Februar" },
	{ "March", "3 月", "3 月", "3월", "Marzo", "Mars", "März" },
	{ "April", "4 月", "4 月", "4월", "Abril", "Avril", "April" },
	{ "May", "5 月", "5 月", "5월", "Mayo", "Mai", "Mai" },
	{ "June", "6 月", "6 月", "6월", "Junio", "Juin", "Juni" },
	{ "July", "7 月", "7 月", "7월", "Julio", "Juillet", "Juli" },
	{ "August", "8 月", "8 月", "8월", "Agosto", "Août", "August" },
	{ "September", "9 月", "9 月", "9월", "Septiembre", "Septembre", "September" },
	{ "October", "10 月", "10 月", "10월", "Octubre", "Octobre", "Oktober" },
	{ "November", "11 月", "11 月", "11월", "Noviembre", "Novembre", "November" },
	{ "December", "12 月", "12 月", "12월", "Diciembre", "Décembre", "Dezember" },
	{ "Automatic", "自動", "自动", "자동", "Automático", "Automatique", "Automatisch" },
	{ "Brisk", "きびきび", "轻快", "빠르게", "Ágil", "Vif", "Zügig" },
	{ "Normal", "ふつう", "普通", "보통", "Normal", "Normal", "Normal" },
	{ "Gentle", "ゆったり", "舒缓", "느긋하게", "Suave", "Doux", "Gemächlich" },
	{ "Off", "切", "关", "끔", "Desactivado", "Désactivé", "Aus" },
	{ "Quiet", "小さめ", "小声", "작게", "Bajo", "Discret", "Leise" },
	{ "WINDOWS", "ウィンドウ", "窗口", "창", "VENTANAS", "FENÊTRES", "FENSTER" },
	{ "Lay every window on paper",
		"すべてのウィンドウを紙に載せる",
		"所有窗口都放在纸上",
		"모든 창을 종이 위에 올리기",
		"Poner todas las ventanas sobre papel",
		"Poser chaque fenêtre sur du papier",
		"Jedes Fenster auf Papier legen" },
	{ "Programs that draw their own frame and buttons, such as Firefox, get the shell's "
		"paper too: a margin round them, the turned-down corner, folding, the shadow.",
		"Firefox のように自分で枠とボタンを描くプログラムも紙に載せます。周りの余白、折れ"
		"た角、たたむこと、影がつきます。",
		"像 Firefox 这样自己画边框和按钮的程序也放在纸上：周围的留白、折起的角、折叠和阴"
		"影。",
		"Firefox처럼 테두리와 단추를 직접 그리는 프로그램도 종이 위에 올립니다. 둘레의 여"
		"백, 접힌 모서리, 접기, 그림자가 생깁니다.",
		"Los programas que dibujan su propio marco y botones, como Firefox, también van "
		"sobre el papel: un margen alrededor, la esquina doblada, el plegado, la sombra.",
		"Les programmes qui dessinent leur propre cadre et leurs boutons, comme Firefox, "
		"ont aussi le papier\xc2\xa0: une marge autour, le coin corné, le pliage, l’ombre.",
		"Programme, die Rahmen und Knöpfe selbst zeichnen, etwa Firefox, bekommen auch "
		"das Papier: einen Rand, die umgeknickte Ecke, das Falten, den Schatten." },
	{ "Screw up a closed window and throw it away",
		"閉じたウィンドウを丸めて捨てる",
		"把关掉的窗口揉成团扔掉",
		"닫은 창을 구겨서 버리기",
		"Arrugar una ventana cerrada y tirarla",
		"Froisser une fenêtre fermée et la jeter",
		"Ein geschlossenes Fenster zerknüllen und wegwerfen" },
	{ "Off, a window that is closed simply goes.",
		"切にすると、閉じたウィンドウはそのまま消えます。",
		"关闭后，关掉的窗口直接消失。",
		"끄면 닫은 창은 그냥 사라집니다.",
		"Desactivado, una ventana cerrada simplemente desaparece.",
		"Désactivé, une fenêtre fermée disparaît simplement.",
		"Aus: ein geschlossenes Fenster verschwindet einfach." },
	{ "Turn a corner up when the pointer rests on it",
		"ポインタを載せると角がめくれる",
		"指针停在角上时把角掀起",
		"포인터를 올리면 모서리가 젖혀짐",
		"Levantar una esquina al dejar el puntero encima",
		"Relever un coin quand le pointeur s’y pose",
		"Ecke anheben, wenn der Zeiger darauf ruht" },
	{ "Off, it turns up only when it is clicked. For windows, letters and binder sheets.",
		"切にすると、クリックしたときだけめくれます。ウィンドウ、手紙、バインダーの紙に効"
		"きます。",
		"关闭后只有点击时才掀起。适用于窗口、信和活页夹的纸。",
		"끄면 클릭했을 때만 젖혀집니다. 창, 편지, 바인더의 종이에 적용됩니다.",
		"Desactivado, solo se levanta al hacer clic. Para ventanas, cartas y hojas de "
		"las carpetas.",
		"Désactivé, il ne se relève qu’au clic. Pour les fenêtres, les lettres et les "
		"feuilles des classeurs.",
		"Aus: nur beim Anklicken. Für Fenster, Briefe und Ordnerblätter." },
	{ "DESK", "机", "书桌", "책상", "ESCRITORIO", "BUREAU", "SCHREIBTISCH" },
	{ "Size of things on the desk",
		"机の上の物の大きさ",
		"书桌上物品的大小",
		"책상 위 물건의 크기",
		"Tamaño de las cosas del escritorio",
		"Taille des objets sur le bureau",
		"Größe der Dinge auf dem Schreibtisch" },
	{ "Automatic follows the screen. Bigger things leave less desk; the room is traced again "
		"for it, which takes a moment.",
		"自動は画面に合わせます。大きくすると机は狭くなります。部屋を描き直すので少し時間"
		"がかかります。",
		"自动会跟随屏幕。东西越大，书桌越窄；房间要重新绘制，需要一点时间。",
		"자동은 화면에 맞춥니다. 크게 하면 책상이 좁아집니다. 방을 다시 그리므로 잠시 걸"
		"립니다.",
		"Automático sigue a la pantalla. Cosas más grandes dejan menos escritorio; la "
		"habitación se vuelve a trazar, lo que tarda un momento.",
		"Automatique suit l’écran. Des objets plus grands laissent moins de bureau\xc2\xa0; la "
		"pièce est retracée, ce qui prend un moment.",
		"Automatisch folgt dem Bildschirm. Größere Dinge lassen weniger Schreibtisch; "
		"der Raum wird neu gezeichnet, das dauert einen Moment." },
	{ "Motion",
		"動きの速さ",
		"动作速度",
		"움직임 속도",
		"Movimiento",
		"Mouvement",
		"Bewegung" },
	{ "Camera bounce",
		"カメラの揺れ",
		"镜头晃动",
		"카메라 흔들림",
		"Rebote de la cámara",
		"Rebond de la caméra",
		"Kamera-Nachschwingen" },
	{ "How far the view swings past what you turn to look at and settles back, and how the "
		"head bobs walking to the front door. Lower it, or turn it off, if it makes you "
		"feel unwell.",
		"見る向きを変えたときに視点が少し行き過ぎて戻る揺れと、玄関まで歩くときの頭の上下"
		"です。酔いやすい場合は「低」か「なし」にしてください。",
		"转向某处时视角稍微越过再回来的晃动，以及走向门口时头部的起伏。如果让你不舒服，请"
		"调低或关闭。",
		"보는 방향을 바꿀 때 시점이 조금 지나쳤다가 돌아오는 흔들림과, 현관까지 걸을 때 "
		"머리의 오르내림입니다. 어지러우면 낮추거나 끄세요.",
		"Cuánto se pasa la vista de lo que miras y vuelve, y cómo se mueve la cabeza al "
		"ir a la puerta. Bájalo o desactívalo si te marea.",
		"À quel point la vue dépasse ce que vous regardez avant de revenir, et comment "
		"la tête oscille en allant à la porte. Baissez-le ou coupez-le s’il vous "
		"incommode.",
		"Wie weit die Ansicht über das Ziel hinausschwingt und zurückkommt, und wie der "
		"Kopf auf dem Weg zur Haustür wippt. Verringern oder ausschalten, wenn Ihnen "
		"übel wird." },
	{ "Low", "低", "低", "낮음", "Bajo", "Bas", "Niedrig" },
	{ "None", "なし", "无", "없음", "Ninguno", "Aucun", "Keins" },
	{ "How long everything that moves takes.",
		"動くものすべてにかかる時間です。",
		"所有动作所需的时间。",
		"움직이는 모든 것에 걸리는 시간입니다.",
		"Cuánto tarda todo lo que se mueve.",
		"Le temps que prend tout ce qui bouge.",
		"Wie lange alles dauert, was sich bewegt." },
	{ "Clock", "時計", "时钟", "시계", "Reloj", "Horloge", "Uhr" },
	{ "24-hour", "24 時間制", "24 小时制", "24시간제", "24 horas", "24 heures", "24 Stunden" },
	{ "12-hour", "12 時間制", "12 小时制", "12시간제", "12 horas", "12 heures", "12 Stunden" },
	{ "The clock on the desk, and the times on letters.",
		"机の時計と、手紙の時刻の表し方です。",
		"书桌上的时钟，以及信上的时间。",
		"책상의 시계와 편지의 시각입니다.",
		"El reloj del escritorio y la hora de las cartas.",
		"L’horloge du bureau et l’heure des lettres.",
		"Die Uhr auf dem Schreibtisch und die Uhrzeit auf Briefen." },
	{ "Language", "言語", "语言", "언어", "Idioma", "Langue", "Sprache" },
	{ "The words on the desk, and the names programs are filed under.",
		"机の上の言葉と、プログラムの名前の言語です。",
		"书桌上的文字，以及程序名称的语言。",
		"책상 위의 말과 프로그램 이름의 언어입니다.",
		"Las palabras del escritorio y los nombres de los programas.",
		"Les mots du bureau et le nom sous lequel les programmes sont classés.",
		"Die Wörter auf dem Schreibtisch und die Namen der Programme." },
	{ "DRAWER AND ALL APPS",
		"引き出しと「すべてのアプリ」",
		"抽屉和「所有应用」",
		"서랍과 「모든 앱」",
		"CAJÓN Y TODAS LAS APPS",
		"TIROIR ET TOUTES LES APPS",
		"SCHUBLADE UND ALLE APPS" },
	{ "Put the drawer back after something in it is used",
		"使ったら引き出しを閉める",
		"用过抽屉里的东西后关上抽屉",
		"쓰고 나면 서랍 닫기",
		"Cerrar el cajón después de usar algo de él",
		"Refermer le tiroir après usage",
		"Schublade nach Gebrauch wieder schließen" },
	{ "Off, it stays out until it is pushed back in.",
		"切にすると、押し戻すまで開いたままです。",
		"关闭后，抽屉一直开着，直到推回去。",
		"끄면 밀어 넣을 때까지 열려 있습니다.",
		"Desactivado, se queda abierto hasta que lo empujes.",
		"Désactivé, il reste ouvert jusqu’à ce qu’on le repousse.",
		"Aus: sie bleibt offen, bis sie zugeschoben wird." },
	{ "Fold windows away into the drawer",
		"たたんだウィンドウを引き出しにしまう",
		"把折起的窗口收进抽屉",
		"접은 창을 서랍에 넣기",
		"Guardar en el cajón las ventanas dobladas",
		"Ranger les fenêtres pliées dans le tiroir",
		"Gefaltete Fenster in die Schublade legen" },
	{ "Off, a folded window is put down along the front of the desk.",
		"切にすると、たたんだウィンドウは机の手前に並べます。",
		"关闭后，折起的窗口排在书桌的前沿。",
		"끄면 접은 창은 책상 앞쪽에 놓입니다.",
		"Desactivado, una ventana doblada se deja en el borde del escritorio.",
		"Désactivé, une fenêtre pliée est posée le long du bord du bureau.",
		"Aus: ein gefaltetes Fenster wird vorn auf den Schreibtisch gelegt." },
	{ "Carry a program's sheet down to the desk",
		"プログラムの紙を机まで運ぶ",
		"把程序的纸带到书桌上",
		"프로그램의 종이를 책상까지 가져오기",
		"Llevar la hoja del programa al escritorio",
		"Porter la feuille du programme jusqu’au bureau",
		"Das Blatt eines Programms auf den Schreibtisch tragen" },
	{ "A program taken out of ALL APPS has its sheet put down where its window will be, and "
		"the window comes onto it.",
		"「すべてのアプリ」から取ったプログラムは、その紙をウィンドウの出る所に置き、ウィ"
		"ンドウはその上に出ます。",
		"从「所有应用」中取出的程序，会先把它的纸放在窗口将出现的位置，窗口再出现在纸上。",
		"「모든 앱」에서 꺼낸 프로그램은 창이 나올 자리에 종이를 내려놓고, 창은 그 위에 "
		"나타납니다.",
		"Un programa sacado de TODAS LAS APPS deja su hoja donde irá su ventana, y la "
		"ventana aparece encima.",
		"Un programme pris dans TOUTES LES APPS a sa feuille posée là où sera sa "
		"fenêtre, et la fenêtre vient dessus.",
		"Ein Programm aus ALLE APPS legt sein Blatt dort ab, wo sein Fenster erscheint, "
		"und das Fenster kommt darauf." },
	{ "SOUND", "音", "声音", "소리", "SONIDO", "SON", "TON" },
	{ "Sounds", "音", "声音", "소리", "Sonidos", "Sons", "Klänge" },
	{ "Paper, the drawer, the post box, keys and switches: what each thing on the desk "
		"sounds like.",
		"紙、引き出し、ポスト、キーやスイッチ。机の上の物の音です。",
		"纸、抽屉、信箱、按键和开关：书桌上每样东西的声音。",
		"종이, 서랍, 우편함, 키와 스위치. 책상 위 물건의 소리입니다.",
		"Papel, cajón, buzón, teclas e interruptores: cómo suena cada cosa del "
		"escritorio.",
		"Le papier, le tiroir, la boîte aux lettres, les touches et les interrupteurs\xc2\xa0: "
		"le son de chaque chose sur le bureau.",
		"Papier, Schublade, Briefkasten, Tasten und Schalter: wie jedes Ding auf dem "
		"Schreibtisch klingt." },
	{ "Hear a letter arrive",
		"手紙が届いたら音を鳴らす",
		"来信时发出声音",
		"편지가 오면 소리 내기",
		"Oír cuando llega una carta",
		"Entendre arriver une lettre",
		"Ankommende Briefe hören" },
	{ "A letter coming through the slot of the post box.",
		"ポストの投入口から手紙が入る音です。",
		"信从信箱投递口落进来的声音。",
		"우편함 투입구로 편지가 들어오는 소리입니다.",
		"Una carta entrando por la ranura del buzón.",
		"Une lettre qui passe par la fente de la boîte.",
		"Ein Brief, der durch den Schlitz des Briefkastens fällt." },
	/* Shell Settings: the screen ("SCREEN" is the console's) */
	{ "Turn the screen off when the desk is left",
		"操作しないときに画面を消す",
		"离开时关闭屏幕",
		"자리를 비우면 화면 끄기",
		"Apagar la pantalla al dejar el escritorio",
		"Éteindre l’écran quand on quitte le bureau",
		"Bildschirm ausschalten, wenn niemand da ist" },
	/* Shell Settings: the keyboard and the pointer */
	{ "KEYBOARD AND POINTER",
		"キーボードとポインタ",
		"键盘和指针",
		"키보드와 포인터",
		"TECLADO Y PUNTERO",
		"CLAVIER ET POINTEUR",
		"TASTATUR UND ZEIGER" },
	{ "Tap the touchpad to click",
		"タッチパッドをタップしてクリック",
		"轻触触摸板即点击",
		"터치패드를 탭하여 클릭",
		"Tocar el panel táctil para hacer clic",
		"Taper sur le pavé tactile pour cliquer",
		"Touchpad antippen zum Klicken" },
	{ "Off, only its buttons click.",
		"切ると、クリックはボタンだけになります。",
		"关闭后只有按键能点击。",
		"끄면 버튼으로만 클릭합니다.",
		"Desactivado, solo hacen clic sus botones.",
		"Désactivé, seuls ses boutons cliquent.",
		"Aus: nur die Tasten klicken." },
	{ "Natural scrolling on the touchpad",
		"タッチパッドのナチュラルスクロール",
		"触摸板自然滚动",
		"터치패드 자연스러운 스크롤",
		"Desplazamiento natural en el panel táctil",
		"Défilement naturel sur le pavé tactile",
		"Natürliches Scrollen auf dem Touchpad" },
	{ "The page goes the way the fingers do, as on a phone.",
		"スマートフォンのように、指を動かした方向にページが動きます。",
		"像手机一样，页面随手指移动的方向走。",
		"휴대폰처럼 손가락이 움직이는 방향으로 페이지가 움직입니다.",
		"La página va hacia donde van los dedos, como en un móvil.",
		"La page suit les doigts, comme sur un téléphone.",
		"Die Seite folgt den Fingern, wie auf einem Handy." },
	{ "Pointer speed",
		"ポインタの速さ",
		"指针速度",
		"포인터 속도",
		"Velocidad del puntero",
		"Vitesse du pointeur",
		"Zeigergeschwindigkeit" },
	{ "For mice and touchpads alike.",
		"マウスにもタッチパッドにも。",
		"鼠标和触摸板都适用。",
		"마우스와 터치패드 모두에 적용됩니다.",
		"Para ratones y paneles táctiles por igual.",
		"Pour les souris comme pour les pavés tactiles.",
		"Für Mäuse und Touchpads gleichermaßen." },
	{ "Slow", "遅い", "慢", "느림", "Lenta", "Lente", "Langsam" },
	{ "Fast", "速い", "快", "빠름", "Rápida", "Rapide", "Schnell" },
	{ "Buttons for the left hand",
		"左手用のボタン",
		"左手按键",
		"왼손잡이용 버튼",
		"Botones para zurdos",
		"Boutons pour gaucher",
		"Tasten für Linkshänder" },
	{ "The main button on the right, the other on the left.",
		"主なボタンを右に、もう一方を左にします。",
		"主键在右，另一个在左。",
		"주 버튼을 오른쪽에, 다른 하나를 왼쪽에 둡니다.",
		"El botón principal a la derecha, el otro a la izquierda.",
		"Le bouton principal à droite, l’autre à gauche.",
		"Die Haupttaste rechts, die andere links." },
	{ "Keyboard layout",
		"キーボードの配列",
		"键盘布局",
		"키보드 배열",
		"Distribución del teclado",
		"Disposition du clavier",
		"Tastaturbelegung" },
	{ "The machine's is the one set for the whole computer. Any other layout xkb knows can "
		"be written into shell.conf.",
		"「このコンピュータの設定」は、コンピュータ全体に設定されている配列です。xkb が知"
		"っているほかの配列も shell.conf に書けます。",
		"「本机设置」是整台电脑设定的布局。xkb 认识的其他布局也可以写进 shell.conf。",
		"「이 컴퓨터의 설정」은 컴퓨터 전체에 설정된 배열입니다. xkb가 아는 다른 배열도 "
		"shell.conf에 쓸 수 있습니다.",
		"La de la máquina es la configurada para todo el ordenador. Cualquier otra que "
		"xkb conozca se puede escribir en shell.conf.",
		"Celle de la machine est celle réglée pour tout l’ordinateur. Toute autre "
		"disposition connue de xkb peut s’écrire dans shell.conf.",
		"Die des Rechners ist die für den ganzen Computer eingestellte. Jede andere "
		"Belegung, die xkb kennt, kann in shell.conf stehen." },
	{ "The machine's",
		"このコンピュータの設定",
		"本机设置",
		"이 컴퓨터의 설정",
		"La de la máquina",
		"Celle de la machine",
		"Die des Rechners" },
	{ "English (US)",
		"英語（US）",
		"英语（美国）",
		"영어(미국)",
		"Inglés (EE. UU.)",
		"Anglais (États-Unis)",
		"Englisch (USA)" },
	{ "English (UK)",
		"英語（UK）",
		"英语（英国）",
		"영어(영국)",
		"Inglés (Reino Unido)",
		"Anglais (Royaume-Uni)",
		"Englisch (UK)" },
	{ "Japanese", "日本語", "日语", "일본어", "Japonés", "Japonais", "Japanisch" },
	{ "German", "ドイツ語", "德语", "독일어", "Alemán", "Allemand", "Deutsch" },
	{ "French", "フランス語", "法语", "프랑스어", "Francés", "Français", "Französisch" },
	{ "Spanish", "スペイン語", "西班牙语", "스페인어", "Español", "Espagnol", "Spanisch" },
	{ "Italian", "イタリア語", "意大利语", "이탈리아어", "Italiano", "Italien", "Italienisch" },
	{ "Portuguese",
		"ポルトガル語",
		"葡萄牙语",
		"포르투갈어",
		"Portugués",
		"Portugais",
		"Portugiesisch" },
	{ "Portuguese (Brazil)",
		"ポルトガル語（ブラジル）",
		"葡萄牙语（巴西）",
		"포르투갈어(브라질)",
		"Portugués (Brasil)",
		"Portugais (Brésil)",
		"Portugiesisch (Brasilien)" },
	{ "Russian", "ロシア語", "俄语", "러시아어", "Ruso", "Russe", "Russisch" },
	{ "Korean", "韓国語", "韩语", "한국어", "Coreano", "Coréen", "Koreanisch" },
	{ "Swedish", "スウェーデン語", "瑞典语", "스웨덴어", "Sueco", "Suédois", "Schwedisch" },
	{ "Norwegian",
		"ノルウェー語",
		"挪威语",
		"노르웨이어",
		"Noruego",
		"Norvégien",
		"Norwegisch" },
	{ "Danish", "デンマーク語", "丹麦语", "덴마크어", "Danés", "Danois", "Dänisch" },
	{ "Finnish", "フィンランド語", "芬兰语", "핀란드어", "Finés", "Finnois", "Finnisch" },
	{ "Second layout",
		"2 つ目の配列",
		"第二种布局",
		"두 번째 배열",
		"Segunda distribución",
		"Deuxième disposition",
		"Zweite Belegung" },
	{ "Changed to and back with the keys below.",
		"下のキーで切り替えます。",
		"用下面的键切换。",
		"아래 키로 전환합니다.",
		"Se cambia con las teclas de abajo.",
		"On passe de l’une à l’autre avec les touches ci-dessous.",
		"Mit den Tasten unten hin- und herschalten." },
	{ "Change layouts with",
		"配列の切り替え",
		"切换布局的键",
		"배열 전환 키",
		"Cambiar con",
		"Changer avec",
		"Umschalten mit" },
	{ "Key repeat",
		"キーリピート",
		"按键重复",
		"키 반복",
		"Repetición de teclas",
		"Répétition des touches",
		"Tastenwiederholung" },
	{ "How soon a key held down starts repeating, and how fast.",
		"押したままのキーが繰り返し始めるまでの時間と、その速さ。",
		"按住键多久开始重复，以及重复的速度。",
		"누르고 있는 키가 반복되기까지의 시간과 그 속도입니다.",
		"Cuánto tarda en repetirse una tecla pulsada, y a qué velocidad.",
		"Au bout de combien de temps une touche maintenue se répète, et à quelle vitesse.",
		"Wann eine gehaltene Taste zu wiederholen beginnt, und wie schnell." },
	{ "After this long without a key or the pointer, the screen goes dark; either brings it "
		"back.",
		"キーにもポインタにも触れないままこの時間がたつと、画面が暗くなります。どちらかに"
		"触れれば戻ります。",
		"这么久不碰键盘和指针，屏幕就会变暗；碰任何一个就会恢复。",
		"키나 포인터를 이만큼 건드리지 않으면 화면이 꺼집니다. 어느 쪽이든 건드리면 돌아"
		"옵니다.",
		"Tras este tiempo sin tocar una tecla ni el puntero, la pantalla se apaga; "
		"cualquiera de los dos la devuelve.",
		"Après ce délai sans touche ni pointeur, l’écran s’éteint\xc2\xa0; l’un ou l’autre le "
		"rallume.",
		"Nach so langer Zeit ohne Taste oder Zeiger wird der Bildschirm dunkel; beides "
		"holt ihn zurück." },
	{ "Never", "しない", "从不", "안 함", "Nunca", "Jamais", "Nie" },
	{ "1 min", "1 分後", "1 分钟后", "1분 후", "1 min", "1 min", "1 Min." },
	{ "2 min", "2 分後", "2 分钟后", "2분 후", "2 min", "2 min", "2 Min." },
	{ "5 min", "5 分後", "5 分钟后", "5분 후", "5 min", "5 min", "5 Min." },
	{ "10 min", "10 分後", "10 分钟后", "10분 후", "10 min", "10 min", "10 Min." },
	{ "15 min", "15 分後", "15 分钟后", "15분 후", "15 min", "15 min", "15 Min." },
	{ "30 min", "30 分後", "30 分钟后", "30분 후", "30 min", "30 min", "30 Min." },
	{ "Lock the desk when the screen goes off",
		"画面が消えたら机をロックする",
		"屏幕关闭时锁定书桌",
		"화면이 꺼지면 책상 잠그기",
		"Bloquear el escritorio al apagarse la pantalla",
		"Verrouiller le bureau quand l’écran s’éteint",
		"Schreibtisch sperren, wenn der Bildschirm ausgeht" },
	{ "The desk is then behind the front door, and your password lets you back in. Off, a "
		"key or the pointer is enough.",
		"机は玄関の向こうになり、戻るにはパスワードが要ります。切にすると、キーかポインタ"
		"に触れるだけで戻ります。",
		"书桌会在大门后面，要用密码才能回来。关闭后，碰一下键盘或指针就行。",
		"책상은 현관 너머가 되고, 돌아오려면 암호가 필요합니다. 끄면 키나 포인터만으로 돌"
		"아옵니다.",
		"El escritorio queda tras la puerta de entrada y tu contraseña te deja volver. "
		"Desactivado, basta una tecla o el puntero.",
		"Le bureau est alors derrière la porte d’entrée, et votre mot de passe vous "
		"laisse revenir. Désactivé, une touche ou le pointeur suffit.",
		"Der Schreibtisch liegt dann hinter der Haustür, und Ihr Passwort lässt Sie "
		"zurück. Aus: eine Taste oder der Zeiger genügt." },
	/* Shell Settings: the rest of the machine, set in programs made for it */
	{ "SYSTEM", "システム", "系统", "시스템", "SISTEMA", "SYSTÈME", "SYSTEM" },
	{ "NETWORK AND DEVICES",
		"ネットワークと機器",
		"网络与设备",
		"네트워크와 기기",
		"RED Y DISPOSITIVOS",
		"RÉSEAU ET APPAREILS",
		"NETZWERK UND GERÄTE" },
	{ "THIS COMPUTER",
		"このコンピュータ",
		"这台电脑",
		"이 컴퓨터",
		"ESTE ORDENADOR",
		"CET ORDINATEUR",
		"DIESER COMPUTER" },
	/* Shell Settings' two tabs */
	{ "Shell", "シェル", "外壳", "셸", "Shell", "Shell", "Shell" },
	{ "Graphics", "グラフィック", "图形", "그래픽", "Gráficos", "Graphismes", "Grafik" },
	{ "How much the desk draws. High is the desk as it is made; Ultra traces the room finer, "
		"and wider on a screen with a GPU to do it; Medium and Low smaller and plainer. "
		"2D Fallback is for a machine without a GPU, or a virtual machine: no shadows, "
		"nothing fading, blurred or moving, and the room traced once, small and plain. "
		"The room is traced again for it, which takes a moment.",
		"机がどれだけ描くか。高がふだんの机です。ウルトラは部屋をより細かく（GPU があれば"
		"大きい画面でより大きく）焼き、中・低は小さく簡素に焼きます。2D フォールバックは "
		"GPU の無い機械や仮想マシン向けで、影・フェード・ぼかし・動きをすべて切り、部屋は"
		"一度だけ小さく簡素に焼きます。切り替えると部屋を焼き直すので、少し時間がかかりま"
		"す。",
		"书桌画多少。高是书桌本来的样子；超高把房间画得更细（有 GPU 时在大屏幕上画得更"
		"大）；中和低画得更小更朴素。2D 后备适用于没有 GPU 的机器或虚拟机：没有阴影、淡入"
		"淡出、模糊和动画，房间只简单地画一次。切换后房间要重画，需要一点时间。",
		"책상이 얼마나 그리는지입니다. 높음이 본래의 책상이고, 울트라는 방을 더 세밀하게"
		"(GPU가 있으면 큰 화면에서 더 크게) 그리며, 중간과 낮음은 작고 간소하게 그립니다"
		". 2D 대체 모드는 GPU가 없는 컴퓨터나 가상 머신용으로, 그림자·페이드·흐림·움직임"
		"을 모두 끄고 방은 한 번만 작고 간소하게 그립니다. 바꾸면 방을 다시 그리므로 잠시"
		" 걸립니다.",
		"Cuánto dibuja el escritorio. Alto es el escritorio tal como es; Ultra traza la "
		"habitación más fina, y más ancha en una pantalla con GPU; Medio y Bajo, más "
		"pequeña y sencilla. 2D básico es para una máquina sin GPU o una máquina "
		"virtual: sin sombras, sin fundidos, desenfoques ni movimiento, y la habitación "
		"trazada una vez, pequeña y sencilla. La habitación se vuelve a trazar, lo que "
		"tarda un momento.",
		"Ce que dessine le bureau. Élevé est le bureau tel qu’il est fait\xc2\xa0; Ultra trace "
		"la pièce plus finement, et plus large sur un écran avec un GPU\xc2\xa0; Moyen et Bas "
		"plus petit et plus simple. 2D de secours est pour une machine sans GPU, ou une "
		"machine virtuelle\xc2\xa0: ni ombres, ni fondus, ni flou, ni mouvement, et la pièce "
		"tracée une fois, petite et simple. La pièce est retracée, ce qui prend un "
		"moment.",
		"Wie viel der Schreibtisch zeichnet. Hoch ist der Schreibtisch, wie er gemacht "
		"ist; Ultra zeichnet den Raum feiner, auf einem Bildschirm mit GPU auch breiter; "
		"Mittel und Niedrig kleiner und schlichter. 2D-Notbetrieb ist für Rechner ohne "
		"GPU oder virtuelle Maschinen: keine Schatten, kein Überblenden, keine Unschärfe "
		"und keine Bewegung, und der Raum wird einmal klein und schlicht gezeichnet. Der "
		"Raum wird dafür neu gezeichnet, das dauert einen Moment." },
	{ "Ultra", "ウルトラ", "超高", "울트라", "Ultra", "Ultra", "Ultra" },
	{ "High", "高", "高", "높음", "Alto", "Élevé", "Hoch" },
	{ "Medium", "中", "中", "중간", "Medio", "Moyen", "Mittel" },
	{ "2D Fallback",
		"2D フォールバック",
		"2D 后备",
		"2D 대체 모드",
		"2D básico",
		"2D de secours",
		"2D-Notbetrieb" },
	{ "System", "システム", "系统", "시스템", "Sistema", "Système", "System" },
	/* Shell Settings: what is written on each part's sticky note */
	{ "Windows", "ウィンドウ", "窗口", "창", "Ventanas", "Fenêtres", "Fenster" },
	{ "Desk", "机", "书桌", "책상", "Escritorio", "Bureau", "Schreibtisch" },
	{ "Screen", "画面", "屏幕", "화면", "Pantalla", "Écran", "Bildschirm" },
	{ "Keyboard,\nmouse",
		"キーボード\nマウス",
		"键盘\n鼠标",
		"키보드\n마우스",
		"Teclado,\nratón",
		"Clavier,\nsouris",
		"Tastatur,\nMaus" },
	{ "Days,\nweather",
		"記念日\n天気",
		"纪念日\n天气",
		"기념일\n날씨",
		"Días,\ntiempo",
		"Jours,\nmétéo",
		"Tage,\nWetter" },
	{ "Drawer", "引き出し", "抽屉", "서랍", "Cajón", "Tiroir", "Schublade" },
	{ "Sound", "音", "声音", "소리", "Sonido", "Son", "Ton" },
	{ "Network,\ndevices",
		"ネットワーク\n機器",
		"网络\n设备",
		"네트워크\n기기",
		"Red,\ndispositivos",
		"Réseau,\nappareils",
		"Netzwerk,\nGeräte" },
	{ "Look,\nlanguage",
		"見た目\n言語",
		"外观\n语言",
		"모양\n언어",
		"Aspecto,\nidioma",
		"Apparence,\nlangue",
		"Aussehen,\nSprache" },
	{ "Computer", "コンピュータ", "电脑", "컴퓨터", "Ordenador", "Ordinateur", "Computer" },
	{ "LOOK AND LANGUAGE",
		"見た目と言語",
		"外观与语言",
		"모양과 언어",
		"ASPECTO E IDIOMA",
		"APPARENCE ET LANGUE",
		"AUSSEHEN UND SPRACHE" },
	{ "Colour profiles",
		"色のプロファイル",
		"颜色配置文件",
		"색 프로필",
		"Perfiles de color",
		"Profils de couleur",
		"Farbprofile" },
	{ "How each screen and printer shows colour.",
		"画面とプリンタごとの色の出し方。",
		"每个屏幕和打印机显示颜色的方式。",
		"화면과 프린터마다 색을 나타내는 방식입니다.",
		"Cómo muestra el color cada pantalla e impresora.",
		"Comment chaque écran et imprimante rend la couleur.",
		"Wie jeder Bildschirm und Drucker Farbe zeigt." },
	{ "Disk usage",
		"ディスクの使用量",
		"磁盘用量",
		"디스크 사용량",
		"Uso del disco",
		"Utilisation du disque",
		"Speicherbelegung" },
	{ "What is taking up the room on a disk, folder by folder.",
		"ディスクの容量を何が使っているかを、フォルダごとに。",
		"逐个文件夹查看什么占用了磁盘空间。",
		"디스크 공간을 무엇이 차지하는지 폴더별로 봅니다.",
		"Qué ocupa el espacio de un disco, carpeta por carpeta.",
		"Ce qui occupe la place sur un disque, dossier par dossier.",
		"Was auf einem Datenträger Platz belegt, Ordner für Ordner." },
	{ "Input method",
		"入力メソッド",
		"输入法",
		"입력기",
		"Método de entrada",
		"Méthode de saisie",
		"Eingabemethode" },
	{ "Typing Japanese, Chinese, Korean and the like: which converters, and the keys that "
		"turn them on.",
		"日本語・中国語・韓国語などの入力：使う変換エンジンと、切り替えのキー。",
		"输入日文、中文、韩文等：用哪些输入法，以及打开它们的键。",
		"일본어·중국어·한국어 등의 입력: 어떤 입력기를 쓰는지와 켜는 키입니다.",
		"Escribir japonés, chino, coreano y similares: qué conversores y las teclas que "
		"los activan.",
		"Saisir le japonais, le chinois, le coréen et autres\xc2\xa0: quels convertisseurs, et "
		"les touches qui les activent.",
		"Japanisch, Chinesisch, Koreanisch und Ähnliches schreiben: welche Umwandler und "
		"die Tasten, die sie einschalten." },
	{ "Date and time",
		"日付と時刻",
		"日期和时间",
		"날짜와 시간",
		"Fecha y hora",
		"Date et heure",
		"Datum und Uhrzeit" },
	{ "The time zone, and setting the clock by the network.",
		"タイムゾーンと、ネットワークで時計を合わせるかどうか。",
		"时区，以及是否通过网络对时。",
		"시간대와, 네트워크로 시계를 맞출지 여부입니다.",
		"La zona horaria, y poner el reloj en hora por la red.",
		"Le fuseau horaire, et la mise à l’heure par le réseau.",
		"Die Zeitzone und das Stellen der Uhr über das Netzwerk." },
	{ "Your name and picture",
		"名前と写真",
		"名字和照片",
		"이름과 사진",
		"Tu nombre y tu foto",
		"Votre nom et votre photo",
		"Ihr Name und Bild" },
	{ "As the sign on the front door shows them, and the login screen.",
		"玄関の札とログイン画面に出るもの。",
		"显示在大门的门牌和登录界面上。",
		"현관의 문패와 로그인 화면에 나오는 것입니다.",
		"Como los muestran el letrero de la puerta y la pantalla de inicio de sesión.",
		"Tels que les montrent la plaque de la porte et l’écran de connexion.",
		"Wie das Schild an der Haustür und der Anmeldebildschirm sie zeigen." },
	{ "Users", "ユーザー", "用户", "사용자", "Usuarios", "Utilisateurs", "Benutzer" },
	{ "Who else can log in to this computer.",
		"このコンピュータにログインできる人。",
		"还有谁可以登录这台电脑。",
		"이 컴퓨터에 로그인할 수 있는 사람입니다.",
		"Quién más puede entrar en este ordenador.",
		"Qui d’autre peut se connecter à cet ordinateur.",
		"Wer sich sonst an diesem Computer anmelden kann." },
	{ "Passwords and keys",
		"パスワードと鍵",
		"密码和密钥",
		"암호와 키",
		"Contraseñas y claves",
		"Mots de passe et clés",
		"Passwörter und Schlüssel" },
	{ "The passwords programs keep for you, and your SSH and PGP keys.",
		"プログラムが覚えているパスワードと、SSH・PGP の鍵。",
		"程序替你保存的密码，以及你的 SSH 和 PGP 密钥。",
		"프로그램이 기억하는 암호와 SSH·PGP 키입니다.",
		"Las contraseñas que guardan los programas, y tus claves SSH y PGP.",
		"Les mots de passe que gardent les programmes, et vos clés SSH et PGP.",
		"Die Passwörter, die Programme für Sie aufbewahren, und Ihre SSH- und "
		"PGP-Schlüssel." },
	{ "Adding and removing programs",
		"プログラムの追加と削除",
		"添加和删除程序",
		"프로그램 추가와 제거",
		"Añadir y quitar programas",
		"Ajouter et supprimer des programmes",
		"Programme hinzufügen und entfernen" },
	{ "Finding programs to put on the shelves, and updating the ones there.",
		"本棚に置くプログラムを探す、置いてあるものを新しくする。",
		"寻找要放上书架的程序，并更新已有的程序。",
		"책장에 둘 프로그램을 찾고, 있는 것을 업데이트합니다.",
		"Buscar programas para las estanterías y actualizar los que hay.",
		"Trouver des programmes à mettre sur les étagères, et mettre à jour ceux qui y "
		"sont.",
		"Programme für die Regale finden und die vorhandenen aktualisieren." },
	{ "Backups",
		"バックアップ",
		"备份",
		"백업",
		"Copias de seguridad",
		"Sauvegardes",
		"Sicherungen" },
	{ "Copies of your files, kept on another disk now and then.",
		"ファイルの写しを、ときどき別のディスクに取っておく。",
		"不时把文件的副本保存到另一个磁盘。",
		"파일의 사본을 가끔 다른 디스크에 보관합니다.",
		"Copias de tus archivos, guardadas de vez en cuando en otro disco.",
		"Des copies de vos fichiers, gardées de temps en temps sur un autre disque.",
		"Kopien Ihrer Dateien, ab und zu auf einem anderen Datenträger." },
	{ "What the computer is doing",
		"コンピュータがしていること",
		"电脑正在做什么",
		"컴퓨터가 하고 있는 일",
		"Qué está haciendo el ordenador",
		"Ce que fait l’ordinateur",
		"Was der Computer gerade tut" },
	{ "The programs running, and what each is using.",
		"動いているプログラムと、それぞれが使っているもの。",
		"正在运行的程序，以及各自占用的资源。",
		"실행 중인 프로그램과 각각이 쓰는 자원입니다.",
		"Los programas en marcha y lo que usa cada uno.",
		"Les programmes en cours, et ce que chacun utilise.",
		"Die laufenden Programme und was jedes davon verbraucht." },
	{ "About this computer",
		"このコンピュータについて",
		"关于这台电脑",
		"이 컴퓨터에 대하여",
		"Acerca de este ordenador",
		"À propos de cet ordinateur",
		"Über diesen Computer" },
	{ "What the computer is made of, part by part.",
		"コンピュータの中身を、部品ごとに。",
		"电脑由什么组成，逐个部件查看。",
		"컴퓨터가 무엇으로 이루어졌는지 부품별로 봅니다.",
		"De qué está hecho el ordenador, pieza a pieza.",
		"De quoi est fait l’ordinateur, pièce par pièce.",
		"Woraus der Computer besteht, Teil für Teil." },
	{ "Volume, brightness, the wireless and the lock are on the Control Panel. The rest of "
		"the machine is set in programs made for it, each of which opens as a window on "
		"the desk.",
		"音量・明るさ・無線・ロックはコントロールパネルにあります。このマシンのそのほかの"
		"設定はそれぞれ専用のプログラムで行い、どれも机の上にウィンドウとして開きます。",
		"音量、亮度、无线和锁定都在控制面板上。机器的其他设置在各自专用的程序里进行，它们"
		"都会作为窗口在书桌上打开。",
		"음량, 밝기, 무선, 잠금은 제어판에 있습니다. 컴퓨터의 나머지 설정은 각각의 전용 "
		"프로그램에서 하며, 모두 책상 위에 창으로 열립니다.",
		"El volumen, el brillo, la conexión inalámbrica y el bloqueo están en el Panel "
		"de control. El resto de la máquina se ajusta en programas hechos para ello, que "
		"se abren como ventanas en el escritorio.",
		"Le volume, la luminosité, le sans-fil et le verrouillage sont sur le panneau de "
		"commandes. Le reste de la machine se règle dans des programmes faits pour cela, "
		"qui s’ouvrent chacun comme une fenêtre sur le bureau.",
		"Lautstärke, Helligkeit, Funk und Sperre sind am Bedienfeld. Der Rest des "
		"Rechners wird in eigenen Programmen eingestellt, die sich jeweils als Fenster "
		"auf dem Schreibtisch öffnen." },
	{ "Network connections",
		"ネットワーク接続",
		"网络连接",
		"네트워크 연결",
		"Conexiones de red",
		"Connexions réseau",
		"Netzwerkverbindungen" },
	{ "Wired, Wi-Fi and VPN connections, and their passwords.",
		"有線・Wi-Fi・VPN の接続と、そのパスワードです。",
		"有线、Wi-Fi 和 VPN 连接，以及它们的密码。",
		"유선·Wi-Fi·VPN 연결과 그 암호입니다.",
		"Conexiones por cable, Wi-Fi y VPN, y sus contraseñas.",
		"Connexions filaires, Wi-Fi et VPN, et leurs mots de passe.",
		"Kabel-, WLAN- und VPN-Verbindungen und ihre Passwörter." },
	{ "Bluetooth devices",
		"Bluetooth 機器",
		"蓝牙设备",
		"블루투스 기기",
		"Dispositivos Bluetooth",
		"Appareils Bluetooth",
		"Bluetooth-Geräte" },
	{ "Pairing headphones, keyboards and phones. The radio itself is switched on the Control "
		"Panel.",
		"ヘッドホン・キーボード・電話とのペアリングです。無線そのものの入切はコントロール"
		"パネルで。",
		"配对耳机、键盘和手机。无线电本身在控制面板上开关。",
		"헤드폰·키보드·전화와 페어링합니다. 무선 자체는 제어판에서 켜고 끕니다.",
		"Emparejar auriculares, teclados y móviles. La radio en sí se enciende en el "
		"Panel de control.",
		"Appairer casques, claviers et téléphones. La radio elle-même s’allume sur le "
		"panneau de commandes.",
		"Kopfhörer, Tastaturen und Telefone koppeln. Den Funk selbst schaltet man am "
		"Bedienfeld." },
	{ "Sound devices",
		"サウンド機器",
		"声音设备",
		"사운드 기기",
		"Dispositivos de sonido",
		"Appareils audio",
		"Audiogeräte" },
	{ "Which speakers and microphone, and how loud each program is. The volume of everything "
		"is on the Control Panel.",
		"使うスピーカーとマイク、プログラムごとの音量です。全体の音量はコントロールパネル"
		"で。",
		"用哪个扬声器和麦克风，以及每个程序的音量。总音量在控制面板上。",
		"어떤 스피커와 마이크를 쓸지, 프로그램별 음량입니다. 전체 음량은 제어판에 있습니"
		"다.",
		"Qué altavoces y micrófono, y el volumen de cada programa. El volumen general "
		"está en el Panel de control.",
		"Quels haut-parleurs et micro, et le volume de chaque programme. Le volume "
		"général est sur le panneau de commandes.",
		"Welche Lautsprecher und welches Mikrofon, und wie laut jedes Programm ist. Die "
		"Gesamtlautstärke ist am Bedienfeld." },
	{ "Displays",
		"ディスプレイ",
		"显示器",
		"디스플레이",
		"Pantallas",
		"Écrans",
		"Bildschirme" },
	{ "Where each screen stands, its resolution and its refresh rate.",
		"画面の並び、解像度、リフレッシュレートです。",
		"每个屏幕的位置、分辨率和刷新率。",
		"화면의 배치, 해상도, 주사율입니다.",
		"Dónde está cada pantalla, su resolución y su frecuencia.",
		"La place de chaque écran, sa résolution et sa fréquence.",
		"Wo jeder Bildschirm steht, seine Auflösung und Bildrate." },
	{ "Printers", "プリンター", "打印机", "프린터", "Impresoras", "Imprimantes", "Drucker" },
	{ "Adding a printer, and what is waiting to be printed.",
		"プリンターの追加と、印刷を待っている文書です。",
		"添加打印机，以及等待打印的文档。",
		"프린터 추가와 인쇄를 기다리는 문서입니다.",
		"Añadir una impresora, y lo que espera para imprimirse.",
		"Ajouter une imprimante, et ce qui attend d’être imprimé.",
		"Einen Drucker hinzufügen und was auf den Druck wartet." },
	{ "Look of other programs",
		"ほかのプログラムの外観",
		"其他程序的外观",
		"다른 프로그램의 모양",
		"Aspecto de otros programas",
		"Apparence des autres programmes",
		"Aussehen anderer Programme" },
	{ "The theme, icons and fonts of other programs' windows. The desk keeps its own look.",
		"ほかのプログラムのウィンドウのテーマ・アイコン・フォントです。机の見た目は変わり"
		"ません。",
		"其他程序窗口的主题、图标和字体。书桌保持自己的样子。",
		"다른 프로그램 창의 테마·아이콘·글꼴입니다. 책상의 모양은 바뀌지 않습니다.",
		"El tema, los iconos y las fuentes de las ventanas de otros programas. El "
		"escritorio conserva su aspecto.",
		"Le thème, les icônes et les polices des fenêtres des autres programmes. Le "
		"bureau garde son apparence.",
		"Design, Symbole und Schriften der Fenster anderer Programme. Der Schreibtisch "
		"behält sein Aussehen." },
	{ "Default programs",
		"既定のプログラム",
		"默认程序",
		"기본 프로그램",
		"Programas predeterminados",
		"Programmes par défaut",
		"Standardprogramme" },
	{ "Which program opens web links, mail and each kind of file. The drawer's web browser, "
		"file manager and text editor are the ones chosen here.",
		"Web のリンク、メール、ファイルの種類ごとに、どのプログラムで開くかです。引き出し"
		"の Web ブラウザ・ファイルマネージャ・テキストエディタも、ここで選んだものになり"
		"ます。",
		"用哪个程序打开网页链接、邮件和各类文件。抽屉里的网页浏览器、文件管理器和文本编辑"
		"器也是这里选的那些。",
		"웹 링크, 메일, 파일 종류마다 어떤 프로그램으로 열지입니다. 서랍의 웹 브라우저·파"
		"일 관리자·텍스트 편집기도 여기서 고른 것이 됩니다.",
		"Qué programa abre los enlaces web, el correo y cada tipo de archivo. El "
		"navegador, el gestor de archivos y el editor de texto del cajón son los que se "
		"eligen aquí.",
		"Quel programme ouvre les liens web, le courrier et chaque type de fichier. Le "
		"navigateur, le gestionnaire de fichiers et l’éditeur de texte du tiroir sont "
		"ceux choisis ici.",
		"Welches Programm Weblinks, E-Mail und jede Dateiart öffnet. Browser, "
		"Dateimanager und Texteditor der Schublade sind die hier gewählten." },
	{ "Disks", "ディスク", "磁盘", "디스크", "Discos", "Disques", "Datenträger" },
	{ "Formatting, partitioning and checking disks and memory sticks.",
		"ディスクや USB メモリの初期化・パーティション分け・検査です。",
		"格式化、分区和检查磁盘与 U 盘。",
		"디스크와 USB 메모리의 포맷·파티션·검사입니다.",
		"Formatear, particionar y comprobar discos y memorias USB.",
		"Formater, partitionner et vérifier disques et clés USB.",
		"Datenträger und USB-Sticks formatieren, partitionieren und prüfen." },
	{ "DEsk Experience Index",
		"DEsk エクスペリエンス インデックス",
		"DEsk 体验指数",
		"DEsk 체험 지수",
		"Índice de experiencia de DEsk",
		"Indice d’expérience DEsk",
		"DEsk-Leistungsindex" },
	{ "How this machine does at what the desk asks of it: the processor, the memory, the "
		"graphics and the disk.",
		"このマシンが机の求めることをどれだけこなせるかを、プロセッサ・メモリ・グラフィッ"
		"クス・ディスクごとに測ります。",
		"这台机器能多好地完成书桌的要求：处理器、内存、图形和磁盘。",
		"이 컴퓨터가 책상이 요구하는 일을 얼마나 해내는지를 프로세서·메모리·그래픽·디스크"
		"별로 잽니다.",
		"Qué tal hace esta máquina lo que le pide el escritorio: el procesador, la "
		"memoria, los gráficos y el disco.",
		"Comment cette machine s’en sort avec ce que lui demande le bureau\xc2\xa0: le "
		"processeur, la mémoire, les graphismes et le disque.",
		"Wie dieser Rechner schafft, was der Schreibtisch verlangt: Prozessor, Speicher, "
		"Grafik und Datenträger." },
	{ "Open \xe2\x80\xba", "開く ›", "打开 ›", "열기 ›", "Abrir ›", "Ouvrir ›", "Öffnen ›" },
	{ "Opening\xe2\x80\xa6",
		"開いています…",
		"正在打开…",
		"여는 중…",
		"Abriendo…",
		"Ouverture…",
		"Wird geöffnet…" },
	{ "not installed \xe2\x80\x94 e.g. %s",
		"インストールされていません — 例：%s",
		"未安装 — 例如 %s",
		"설치되지 않음 — 예: %s",
		"no instalado — p. ej. %s",
		"non installé — p. ex. %s",
		"nicht installiert — z. B. %s" },
	{ "not installed",
		"インストールされていません",
		"未安装",
		"설치되지 않음",
		"no instalado",
		"non installé",
		"nicht installiert" },
	{ "DEsk cannot do this yet",
		"DEsk はまだ対応していません",
		"DEsk 暂时还做不到",
		"DEsk는 아직 할 수 없습니다",
		"DEsk aún no puede hacerlo",
		"DEsk ne sait pas encore le faire",
		"Das kann DEsk noch nicht" },
	{ "Shell Settings",
		"シェルの設定",
		"外壳设置",
		"셸 설정",
		"Ajustes del shell",
		"Réglages du shell",
		"Shell-Einstellungen" },
	{ "APPLICATIONS",
		"アプリケーション",
		"应用程序",
		"응용 프로그램",
		"APLICACIONES",
		"APPLICATIONS",
		"ANWENDUNGEN" },
	{ "Standard programs",
		"標準のプログラム",
		"标准程序",
		"표준 프로그램",
		"Programas estándar",
		"Programmes standard",
		"Standardprogramme" },
	{ "label|%s (%s)", "%s（%s）", "%s（%s）", "%s (%s)", "%s (%s)", "%s (%s)", "%s (%s)" },
	/* the screen drawn on the CPU (wm.c cpu_letter, settings.c) */
	{ "Running on the CPU",
		"CPU で動いています",
		"正在用 CPU 运行",
		"CPU로 동작 중",
		"Funcionando con la CPU",
		"Fonctionne sur le processeur",
		"Läuft auf dem Prozessor" },
	{ "The screen is being drawn by the processor, not a graphics card, so the desk draws in "
		"2D Fallback. Check the graphics drivers -- or, in a virtual machine, its "
		"settings (3D acceleration).",
		"現在 CPU で動いています。ドライバー等を確認してみてください。仮想環境の場合は設"
		"定（3D アクセラレーション）を確認してみてください。それまでは 2D フォールバック"
		"で描きます。",
		"当前由 CPU 而不是显卡绘制屏幕，所以书桌以 2D 后备模式绘制。请检查显卡驱动；如果"
		"是虚拟机，请检查它的设置（3D 加速）。",
		"지금은 그래픽 카드가 아니라 CPU가 화면을 그리고 있어 책상은 2D 대체 모드로 그립"
		"니다. 그래픽 드라이버를 확인하세요. 가상 머신이라면 설정(3D 가속)을 확인하세요.",
		"La pantalla la dibuja el procesador, no una tarjeta gráfica, así que el "
		"escritorio dibuja en 2D básico. Revisa los controladores gráficos o, en una "
		"máquina virtual, su configuración (aceleración 3D).",
		"L’écran est dessiné par le processeur, pas par une carte graphique, donc le "
		"bureau dessine en 2D de secours. Vérifiez les pilotes graphiques ou, dans une "
		"machine virtuelle, ses réglages (accélération 3D).",
		"Der Bildschirm wird vom Prozessor statt von einer Grafikkarte gezeichnet, daher "
		"zeichnet der Schreibtisch im 2D-Notbetrieb. Prüfen Sie die Grafiktreiber oder, "
		"in einer virtuellen Maschine, deren Einstellungen (3D-Beschleunigung)." },
	{ "The screen is being drawn by the processor, not a graphics card. Check the graphics "
		"drivers -- or, in a virtual machine, its settings (3D acceleration).",
		"現在 CPU で動いています。ドライバー等を確認してみてください。仮想環境の場合は設"
		"定（3D アクセラレーション）を確認してみてください。",
		"当前由 CPU 而不是显卡绘制屏幕。请检查显卡驱动；如果是虚拟机，请检查它的设置（3D "
		"加速）。",
		"지금은 그래픽 카드가 아니라 CPU가 화면을 그리고 있습니다. 그래픽 드라이버를 확인"
		"하세요. 가상 머신이라면 설정(3D 가속)을 확인하세요.",
		"La pantalla la dibuja el procesador, no una tarjeta gráfica. Revisa los "
		"controladores gráficos o, en una máquina virtual, su configuración (aceleración "
		"3D).",
		"L’écran est dessiné par le processeur, pas par une carte graphique. Vérifiez "
		"les pilotes graphiques ou, dans une machine virtuelle, ses réglages "
		"(accélération 3D).",
		"Der Bildschirm wird vom Prozessor statt von einer Grafikkarte gezeichnet. "
		"Prüfen Sie die Grafiktreiber oder, in einer virtuellen Maschine, deren "
		"Einstellungen (3D-Beschleunigung)." },
	{ "Running on the CPU now: check the graphics drivers, or in a virtual machine its "
		"settings. Until then the desk draws in 2D Fallback.",
		"現在 CPU で動いています。ドライバー等を確認してみてください。仮想環境の場合は設"
		"定を確認してみてください。それまでは 2D フォールバックで描きます。",
		"当前正在用 CPU 运行：请检查显卡驱动；如果是虚拟机，请检查它的设置。在此之前书桌"
		"以 2D 后备模式绘制。",
		"지금 CPU로 동작 중입니다. 그래픽 드라이버를 확인하세요. 가상 머신이라면 설정을 "
		"확인하세요. 그때까지 책상은 2D 대체 모드로 그립니다.",
		"Ahora funciona con la CPU: revisa los controladores gráficos o, en una máquina "
		"virtual, su configuración. Mientras tanto el escritorio dibuja en 2D básico.",
		"Fonctionne sur le processeur\xc2\xa0: vérifiez les pilotes graphiques, ou dans une "
		"machine virtuelle ses réglages. D’ici là, le bureau dessine en 2D de secours.",
		"Läuft jetzt auf dem Prozessor: Grafiktreiber prüfen oder, in einer virtuellen "
		"Maschine, deren Einstellungen. Bis dahin zeichnet der Schreibtisch im "
		"2D-Notbetrieb." },
	/* the letter on a first start (draw.c desk_draw_first_letter) */
	{ "Setting out the desk",
		"机を用意しています",
		"正在布置书桌",
		"책상을 준비하고 있습니다",
		"Preparando el escritorio",
		"Le bureau se prépare",
		"Der Schreibtisch wird eingerichtet" },
	{ "The first start can take a while: a minute or two, and longer on some machines. The "
		"desk works meanwhile; the bookcase, the Control Panel and the post box open as "
		"each is ready.",
		"初回起動時は時間がかかるかもしれません。環境によっては 1 分から 2 分以上かかるこ"
		"とも有ります。その間も机は使えます。本棚・Control Panel・ポストは、用意ができた"
		"ものから開けます。",
		"第一次启动可能需要一些时间：一两分钟，有些机器会更久。在此期间书桌可以照常使用；"
		"书架、控制面板和信箱准备好一个就能打开一个。",
		"처음 시작할 때는 시간이 걸릴 수 있습니다. 1~2분, 환경에 따라서는 그 이상 걸리기"
		"도 합니다. 그동안에도 책상은 쓸 수 있습니다. 책장·제어판·우편함은 준비된 것부터 "
		"열립니다.",
		"El primer arranque puede tardar un poco: uno o dos minutos, y más en algunas "
		"máquinas. Mientras tanto el escritorio funciona; la estantería, el Panel de "
		"control y el buzón se abren cuando cada uno está listo.",
		"Le premier démarrage peut prendre un moment\xc2\xa0: une minute ou deux, davantage sur "
		"certaines machines. Le bureau fonctionne entre-temps\xc2\xa0; la bibliothèque, le "
		"panneau de commandes et la boîte aux lettres s’ouvrent dès que chacun est prêt.",
		"Der erste Start kann etwas dauern: ein, zwei Minuten, auf manchen Rechnern "
		"länger. Der Schreibtisch funktioniert schon; Bücherregal, Bedienfeld und "
		"Briefkasten öffnen sich, sobald sie fertig sind." },
	/* the binders on the shelf, by what is filed in them */
	{ "shelf|Development",
		"開発",
		"开发",
		"개발",
		"Desarrollo",
		"Développement",
		"Entwicklung" },
	{ "shelf|Internet",
		"インターネット",
		"互联网",
		"인터넷",
		"Internet",
		"Internet",
		"Internet" },
	{ "shelf|Office", "文書", "办公", "문서", "Oficina", "Bureautique", "Büro" },
	{ "shelf|Graphics", "画像", "图像", "그래픽", "Gráficos", "Graphisme", "Grafik" },
	{ "shelf|Media", "音と映像", "影音", "미디어", "Multimedia", "Multimédia", "Medien" },
	{ "shelf|Games", "遊び", "游戏", "게임", "Juegos", "Jeux", "Spiele" },
	{ "shelf|Utilities", "道具", "工具", "도구", "Utilidades", "Utilitaires", "Werkzeuge" },
	{ "shelf|System", "システム", "系统", "시스템", "Sistema", "Système", "System" },
};

#define NWORDS (sizeof WORDS / sizeof WORDS[0])

static enum desk_lang g_lang = DESK_LANG_EN;

/* an open-addressed table of the English, built once */
#define SLOTS 1024
static short g_slot[SLOTS];
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static unsigned hash(const char *s)
{
	unsigned h = 2166136261u;
	for (; *s; s++) {
		h = (h ^ (unsigned char)*s) * 16777619u;
	}
	return h;
}

static void build(void)
{
	_Static_assert(NWORDS < SLOTS / 2, "the word table wants more slots");
	for (int i = 0; i < SLOTS; i++) {
		g_slot[i] = -1;
	}
	for (size_t i = 0; i < NWORDS; i++) {
		unsigned k = hash(WORDS[i][DESK_LANG_EN]) % SLOTS;
		while (g_slot[k] >= 0) {
			k = (k + 1) % SLOTS;
		}
		g_slot[k] = (short)i;
	}
}

const char *desk_tr(const char *key)
{
	if (!key) {
		return "";
	}
	const char *bar = strchr(key, '|');
	const char *english = bar ? bar + 1 : key;
	if (g_lang == DESK_LANG_EN) {
		return english;
	}
	pthread_once(&g_once, build);
	unsigned k = hash(key) % SLOTS;
	while (g_slot[k] >= 0) {
		if (!strcmp(WORDS[g_slot[k]][DESK_LANG_EN], key)) {
			const char *t = WORDS[g_slot[k]][g_lang];
			return t && *t ? t : english;
		}
		k = (k + 1) % SLOTS;
	}
	return english;
}

/* each language's code, as shell.conf and a locale begin with it, and the
 * word it is also known by */
static const char *const CODES[DESK_LANG_COUNT][2] = {
	{ "en", "english" }, { "ja", "japanese" }, { "zh", "chinese" }, { "ko", "korean" },
	{ "es", "spanish" }, { "fr", "french" }, { "de", "german" },
};

/* "ja", "ja_JP.UTF-8", "japanese": the language; -1, none of them */
static int lang_of(const char *s)
{
	for (int l = 0; s && l < DESK_LANG_COUNT; l++) {
		size_t n = strlen(CODES[l][0]);
		if (!strncmp(s, CODES[l][0], n) && (!s[n] || s[n] == '_' || s[n] == '.'
			|| s[n] == '@' || s[n] == '-')) {
			return l;
		}
		if (!strcmp(s, CODES[l][1])) {
			return l;
		}
	}
	return -1;
}

enum desk_lang desk_lang_parse(const char *s)
{
	int l = lang_of(s);
	return l >= 0 ? (enum desk_lang)l : desk_lang_from_locale();
}

enum desk_lang desk_lang_from_locale(void)
{
	const char *names[] = { "LC_ALL", "LC_MESSAGES", "LANG" };
	for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
		const char *v = getenv(names[i]);
		if (v && *v) {
			int l = lang_of(v);
			return l >= 0 ? (enum desk_lang)l : DESK_LANG_EN;
		}
	}
	return DESK_LANG_EN;
}

void desk_font_set_wide_default(bool wide);
void desk_font_set_region(enum desk_lang lang);

void desk_lang_set(enum desk_lang l)
{
	g_lang = l >= 0 && l < DESK_LANG_COUNT ? l : DESK_LANG_EN;
	/* the Chinese, Japanese and Korean faces drawn for what is written in
	 * them, each as it is printed there; and text set before it is known
	 * what it says is in this language */
	desk_font_set_region(g_lang);
	desk_font_set_wide_default(desk_lang_wide(g_lang));
}

enum desk_lang desk_lang_get(void)
{
	return g_lang;
}

bool desk_lang_wide(enum desk_lang l)
{
	return l == DESK_LANG_JA || l == DESK_LANG_ZH || l == DESK_LANG_KO;
}

const char *desk_lang_code(enum desk_lang l)
{
	return CODES[l >= 0 && l < DESK_LANG_COUNT ? l : DESK_LANG_EN][0];
}

/* ---- the months and the days of the week ----
 *
 * As a calendar sold there prints them: capitals where the language has
 * them, and in Chinese, Japanese and Korean the month a number and the
 * day its own character (i18n.h desk_month_name, desk_day_name). */
static const char *const MONTH_WHOLE[DESK_LANG_COUNT][12] = {
	{ /* English */
		"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST",
		"SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER",
	},
	{ /* Japanese */
		"1月", "2月", "3月", "4月", "5月", "6月", "7月", "8月", "9月", "10月", "11月",
		"12月",
	},
	{ /* Chinese */
		"1月", "2月", "3月", "4月", "5月", "6月", "7月", "8月", "9月", "10月", "11月",
		"12月",
	},
	{ /* Korean */
		"1월", "2월", "3월", "4월", "5월", "6월", "7월", "8월", "9월", "10월", "11월",
		"12월",
	},
	{ /* Spanish */
		"ENERO", "FEBRERO", "MARZO", "ABRIL", "MAYO", "JUNIO", "JULIO", "AGOSTO",
		"SEPTIEMBRE", "OCTUBRE", "NOVIEMBRE", "DICIEMBRE",
	},
	{ /* French */
		"JANVIER", "FÉVRIER", "MARS", "AVRIL", "MAI", "JUIN", "JUILLET", "AOÛT",
		"SEPTEMBRE", "OCTOBRE", "NOVEMBRE", "DÉCEMBRE",
	},
	{ /* German */
		"JANUAR", "FEBRUAR", "MÄRZ", "APRIL", "MAI", "JUNI", "JULI", "AUGUST", "SEPTEMBER",
		"OKTOBER", "NOVEMBER", "DEZEMBER",
	},
};

static const char *const MONTH_SHORT[DESK_LANG_COUNT][12] = {
	{ /* English */
		"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
	},
	{ /* Japanese */
		"1月", "2月", "3月", "4月", "5月", "6月", "7月", "8月", "9月", "10月", "11月",
		"12月",
	},
	{ /* Chinese */
		"1月", "2月", "3月", "4月", "5月", "6月", "7月", "8月", "9月", "10月", "11月",
		"12月",
	},
	{ /* Korean */
		"1월", "2월", "3월", "4월", "5월", "6월", "7월", "8월", "9월", "10월", "11월",
		"12월",
	},
	{ /* Spanish */
		"ENE", "FEB", "MAR", "ABR", "MAY", "JUN", "JUL", "AGO", "SEP", "OCT", "NOV", "DIC",
	},
	{ /* French */
		"JANV", "FÉVR", "MARS", "AVR", "MAI", "JUIN", "JUIL", "AOÛT", "SEPT", "OCT", "NOV",
		"DÉC",
	},
	{ /* German */
		"JAN", "FEB", "MÄR", "APR", "MAI", "JUN", "JUL", "AUG", "SEP", "OKT", "NOV", "DEZ",
	},
};

/* Sunday first */
static const char *const DAY_WHOLE[DESK_LANG_COUNT][7] = {
	{ /* English */
		"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY",
	},
	{ /* Japanese */
		"日曜日", "月曜日", "火曜日", "水曜日", "木曜日", "金曜日", "土曜日",
	},
	{ /* Chinese */
		"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六",
	},
	{ /* Korean */
		"일요일", "월요일", "화요일", "수요일", "목요일", "금요일", "토요일",
	},
	{ /* Spanish */
		"DOMINGO", "LUNES", "MARTES", "MIÉRCOLES", "JUEVES", "VIERNES", "SÁBADO",
	},
	{ /* French */
		"DIMANCHE", "LUNDI", "MARDI", "MERCREDI", "JEUDI", "VENDREDI", "SAMEDI",
	},
	{ /* German */
		"SONNTAG", "MONTAG", "DIENSTAG", "MITTWOCH", "DONNERSTAG", "FREITAG", "SAMSTAG",
	},
};

static const char *const DAY_SHORT[DESK_LANG_COUNT][7] = {
	{ /* English */
		"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
	},
	{ /* Japanese */
		"日", "月", "火", "水", "木", "金", "土",
	},
	{ /* Chinese */
		"周日", "周一", "周二", "周三", "周四", "周五", "周六",
	},
	{ /* Korean */
		"일", "월", "화", "수", "목", "금", "토",
	},
	{ /* Spanish */
		"DOM", "LUN", "MAR", "MIÉ", "JUE", "VIE", "SÁB",
	},
	{ /* French */
		"DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM",
	},
	{ /* German */
		"SO", "MO", "DI", "MI", "DO", "FR", "SA",
	},
};

static const char *const DAY_LETTER[DESK_LANG_COUNT][7] = {
	{ /* English */
		"S", "M", "T", "W", "T", "F", "S",
	},
	{ /* Japanese */
		"日", "月", "火", "水", "木", "金", "土",
	},
	{ /* Chinese */
		"日", "一", "二", "三", "四", "五", "六",
	},
	{ /* Korean */
		"일", "월", "화", "수", "목", "금", "토",
	},
	{ /* Spanish */
		"D", "L", "M", "X", "J", "V", "S",
	},
	{ /* French */
		"D", "L", "M", "M", "J", "V", "S",
	},
	{ /* German */
		"S", "M", "D", "M", "D", "F", "S",
	},
};

/* (a language with no row of its own yet has the English) */
static const char *name_in(const char *const *lang, const char *const *english, int i)
{
	return lang[i] ? lang[i] : english[i];
}

const char *desk_month_name(int m, enum desk_name form)
{
	m = ((m % 12) + 12) % 12;
	const char *const (*t)[12] = form == DESK_NAME_WHOLE ? MONTH_WHOLE : MONTH_SHORT;
	return name_in(t[g_lang], t[DESK_LANG_EN], m);
}

const char *desk_day_name(int wd, enum desk_name form)
{
	wd = ((wd % 7) + 7) % 7;
	const char *const (*t)[7] = form == DESK_NAME_WHOLE ? DAY_WHOLE
		: form == DESK_NAME_SHORT ? DAY_SHORT : DAY_LETTER;
	return name_in(t[g_lang], t[DESK_LANG_EN], wd);
}
