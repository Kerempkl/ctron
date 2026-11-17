# AUDIT.md — ctron hata sınıfları denetim listesi

ctron gibi bir uygulamanın (TUI + sysfs yazma kapısı + fork/exec zinciri +
poll döngüsü + tek işçi thread) başına gelebilecek hata sınıflarının tam
listesi. Kategoriler A–L; kalemler tek tek denetlenecek.

## Nasıl kullanılır

- Her kalem tek oturumda denetlenir: kod okuması + mümkünse dinamik kanıt.
- **Durum işaretleri:** `[ ]` açık · `[x]` kanıtla kapalı (tarih + kanıt yazar)
  · `[~]` kısmen kapalı (kalan netleşmeli).
- Kanıt kuralı: iddia ölçümle, çıkışla veya birebir kod alıntısıyla durur;
  "görünüşe göre" kabul edilmez. NEXT.md/CHANGELOG'daki durum kodla
  doğrulanmadan bitmiş sayılmaz (P4 dersi).
- Denetim okuma-yazmaz: probe'lar build/ altına, müdahale yalnız kullanıcı
  onayıyla. Her turun başında git log + NEXT.md + HANDOFF üçlüsünden durum
  tazelenir (paralel oturum dersi).
- Sabit kurallara referans: AGENTS.md'deki 5 "hard-won rule" (LC_NUMERIC,
  poll-yazmaz, dürüst okuma, geri-okuma doğrulaması, q-quit).

---

## A — Bellek güvenliği (buffer/taşma/tanımsız)

- [x] **A1. Heap sızıntısı** — tüm malloc/fopen/popen noktalarında her yolda
  free/fclose/pclose; realloc yok; poll döngüsünde heap tahsisi yok; UI
  heap'e hiç dokunmuyor. *Kanıt 11-05: statik 24/24 nokta + yalnız-LSan
  derlemesi altında unit/--doctor/--status/tam TUI smoke sıfır rapor
  (yöntem hafızada: ASan glibc-2.41 artifact'ine dikkat — yalnız
  -fsanitize=leak kullan).*
- [x] **A2. Stack overflow, `off += snprintf` kalıbı** — dönüş değeri
  out[] ötesine taşır, size_t sarar. `profile_set_note`'ta kanıtlanmıştı;
  `note_append` clamp + test ile kapandı (v0.8.1). *Ağaç-geneli yoklama
  11-05: kalan iki birikim noktası korunan kalıpta — main.c net-satırı
  her yazımdan önce `len >= sizeof line` kırıyor, `pw_app`
  (panel_workspace.c:428) yazmadan önce yer ön-kontrolü yapıyor
  (truncate hiç oluşmuyor, off n'i aşamaz). Saf örnek kalmadı.*
- [x] **A3. `sscanf` genişliksiz `%s`** — TUR3 (1d53638) parse clamp'leriyle
  denetlendi. *Yeni eklenen her `sscanf` için kural: genişlik şart
  (`%47s` gibi); sondaki `.` yutan `%lf` tuzağı ve baş-boşluklu satır
  dersi (TS_WIFI_LINK) hatırla.*
- [x] **A4. Sabit tampon taşmaları** — yollara 512/520, satırlara 256–8192
  tamponlar (`editor_daeboard.c` 8192'lik satır). *Yoklama 11-05 bulgusuz:*
  `strcpy/sprintf` sıfır; `strcat` yalnız mk[8]'e sabit "▲▼" (en fazla
  7 bayt); `strncat` ×10 hepsi doğru `sizeof - strlen - 1` kalıbında;
  `memcpy/memmove` ×27'nin tamamı sınırlı — uzunluk kaynağı clamp'li
  (modes.c MODE_STEPS_MAX-1, main.c:785 key[64], hw.c:1915 name[24],
  daeboard.c replyn/line/name clamp'leri) veya ön-kontrollü (hw.c
  `len < sizeof line` ×3, hw.c:1003 glob alt-akışı `len < sizeof`'a
  takılır, util.c:458 `len >= sizeof(buf)` reddi, kde/hypr `i + 1 < n`
  döngüleri, panel_telem_full dt_sep `off + 3 < sizeof s`,
  panel_workspace rule[128] rc ≤ 40); dizi-sınır eşleşmeleri: s_busy/
  busy ikisi de HW_CPU_MAX + `id < max_cpu`, editor_daeboard adım
  sayısı her artışta DBE_STEPS kapılı (227/599/1108), fan.c strchr
  aralığı.
- [x] **A5. Dizisiz-sonlandırma / truncation sessizliği** — `fgets` truncede
  satırı yarım alır (uzun modes.ini satırı = yarı anahtar). *Denetim
  11-05: 11 fgets noktası sınıflandı, BULGU 1 (set_note kalıcı bozulma
  + sessiz kesme) + BULGU 2 (yarı değerin geçerli kabulü) — İKİSİ DE
  11-17 FIX LAND: `ut_fgets_full()` (util.c) tam-satır-veya-atla
  sözleşmesi; modes/settings/.ctr import/meta parse satırı atlar+loglar;
  set_note yuvarlak-geçemeyecek içerikte rewrite'I TAMAMEN reddeder
  (dosya byte-özdeş kalır, -1 + log; sözleşme değişimi: oversized
  artık 0-dönmüyor). Testler: fgets bataryası (7 durum), parser-atlama
  ×2, note ret/pozitif/temizle. Kalan güvenli noktalar: csv başlığı
  (kolon sayısı tutmaz → temiz ret), os-release/cpuinfo (yalnız
  gösterim), editor_daeboard (8 KB), ut_read_file/_all sınır-correct.*
- [x] **A6. Tanımsız okuma** — struct alanlarının memset'siz kalması,
  hata yolunda atanmamış `err` okunması (TUR3'te biri kapandı).
  *Denetim 11-05: dinamik = unit + --doctor/--status + tam TUI smoke
  UBSan altında SIFIR runtime error (valgrind makinede yok; MSan
  uygulanamaz — instrumentlanmamış notcurses); statik = clang
  --analyze 24 TU'da sıfır core.uninitialized (yalnız 2 ölü-depo:
  main.c:164, panel_telem_full.c:671 — kozmetik); merkez yapılar
  temiz (hw_init memset + sentinel'ler, g_ui memset, g_prefs statik
  init + clamp'li load). BULGU 3 (db_reload hata yolunda err
  yazmıyor, üç arayan `err[0]` guard'ıyla tanımsız okuyordu) —
  11-17 FIX LAND: err girişte temizleniyor (cmd_run konvansiyonu) +
  test_daeboard'a guard'lı sözleşme testi. Diğer err sözleşmeleri
  (cmd_run, profile_import, mode_apply) tüm -1 yollarında yazıyor.*
- [x] **A7. Tamsayı taşması/sarma** — size_t çıkmaları (`len - off`),
  mW↔W dönüşümleri, pwm/Hz matematiği, doğrudan `atoi`.
  *Denetim 11-17: 41 atoi noktası sınıflandı — settings.c hepsi
  ut_clamp_i/boolean bağlam; cmds.c hepsi atoi-SONRASI aralık guardı
  (hz 30-2000, freq 100-10000, battery 20-100, nv-boost 5-25,
  nv-temp 75-87, gpu-clock 200-4096); UI tinput'ları fan_set_point/
  add_point/nudge → clamp_point'e düşer (pure model, testli);
  profile/control/hw kalanları boolean/görüntüleme/sysfs. atoi'nin
  teorik UB'si (representable dışı) glibc'de tanımlı (int)strtol —
  guard'lar wrap'lenmiş değeri de yakalar. İşaretsiz çıkarma taraması:
  tek birikim noktası hw.c:641 ve korunmuş (k >= n-w kırışı);
  çarpmalar long long cast'li (statvfs, tv×1000, 1LL<<30). UBSan
  (signed-integer-overflow dahil) A6 bataryasında zaten sıfır.*
  *- **BULGU 4 (fix land 11-17):** hw_parse_gpu_csv'de double→int
  cast'lere üst sınır yoktu — 1e300 değerinde (int)cast GERÇEK UB
  (C11 6.3.1.4: representation dışı float→int). Artık saçma büyüklük
  alanı dürüstçe -1 kalıyor (test: "absurd values stay -1").*
  *- Mini-sertleştirme: settings cpu_mhz_limit'e üst guard (≤10000,
  freq guard ile aynı tavan) — alt guard vardı, üst yoktu.*
- [x] **A8. Format-string zehirlenmesi** — log/printf ilk argümanı daima
  literal olmalı; config/profile/mode isimleri `%s` olarak geçmeli.
  *Denetim 11-17 bulgusuz: ut_log çağrıları tamamen literal (yalnız
  tanımı eşleşiyor); errf aynı; printf/fputs taramasındaki değişkenli
  görünenler ya yorum ya fputs(buffer) (format yorumlamaz);
  vsnprintf geçişleri (errf, ut_log) literal-arayan zinciriyle;
  fmt-parametreli yardımcı `val_or_dash` arayanları "%d°C" gibi
  literal; editor_daeboard "%.*s" ve profile_capture_line literal.*
- [x] **A9. Veri kaynaklı dizi indeksi** — fan eğrisi 8 noktası, per-core
  indeksleri, SECTIONS kayıt defteri indeksi, dbe_key alanları.
  *Denetim 11-17 bulgusuz, 10 indeks ailesi tek tek: fan_from_csv
  FAN_POINTS sınırı + clamp_point; fan set/add/del/nudge → clamp_point
  (testli); "freq core <id>" → hw_cpu_present guard; cpu_ids doldurma
  `id < HW_CPU_MAX` FİLTRELİ (hw.c 1584-92, yorum da var — sib_buf[id]/
  l3_buf[id] ham kernel-id indekslemesi bu yüzden güvenli); hw
  per-core okuma `t1 < n` + parse `id < max_cpu`; corefreq editör
  cf_sel wrap + `< hw->cpu_n`; telem grid sanitize + kenar-kilitli
  navigasyon (wrap YOK, bilinçli); dbe keyi/step her mutasyonda
  guardlı; modes arr `n < max` append-öncesi; hw_cpu_list_parse
  `n < max` her dalda; sibling tmp[8] parse sınırı + `m>=2 → over`
  bail.*

## B — Durum ve eşzamanlılık

- [x] **B1. Giriş dağıtıcısının busy kapısını atlaması** — fare apply_busy'yi
  atlıyordu; TUR3'te kapandı. *Kalıcı kural: yeni giriş yolu (tuş, fare,
  daeboard tetiklemesi) eklenince apply_busy kontrolü şart.*
- [x] **B2. İşçi thread ↔ poll döngüsü yarışı** — ctron'un tek thread'i
  (apply worker) yazma-yalnız disiplininde. *Denetim 11-17 (TSan:
  notcurses instrumentlanmadığından salt kod kanıtı). Doğru kurgu:
  spawn (düz alanlar + release-store + pthread_create kenarı),
  finalize (busy-drop gözlemi), busy'de tuş+fare kilidi, quit join'le
  bekler, quit_pending erteleme, ut_log/ut_progress iki taraf mutex,
  telem halkalarına worker dokunmaz, busy'de modes immutable.*
  *- **BULGU 5 (FIX LAND 11-17):** ana döngü busy iken bile panelleri
  HER karede hw'dan çiziyordu — worker aynı anda hw'ya yazıyor
  ("reads paused" yalnız maybe_refresh'i kapsıyordu); formal veri
  yarışı, pratikte tek kare yırtık okuma. Fix: donmuş-kopya kapsülü —
  spawn'da (thread doğmadan, UI thread'i) `s_hw_frozen = *hw` +
  `g_ui.hw → donmuş`, worker gerçek hw'ya `apply_hw`'dan yazar,
  finalize/quit pointer'ı geri koyar (quit yolu term-sinyali busy
  ortasında yakalarsa diye join sonrası restore). Çizim protokolu
  değişmedi: paneller aynen çizer, progress satırı yaşamaya devam
  eder, değerler busy süresince donuk (zaten refresh pause
  ediliyordu). Doğrulama: 0 uyarı + unit + tam tuitest (apply
  akışları dahil).*
- [x] **B3. Çifte/yinelenen apply** — *Denetim 11-17: davranış =
  DÜŞÜR + tek-sefer uyarı (kuyruk YOK, bilinçli). Çift kapı:
  dispatch_key busy'de yalnız q/Q geçirir (yorum: "a running apply
  owns the machine's write path"), ui_spawn_apply kendi de
  early-return + tek-sefer "already running" logu. Enter basılı
  tutma batch'lenir ama busy'de dispatch'e ulaşamaz.*
- [x] **B4. Thread ömrü kapanışta** — *Denetim 11-17: kapanış sırası
  kanıtlandı — döngü !busy'de çıkar (quit_pending busy bitene dek
  ertelenir; term sinyali busy ortasında yakalanırsa döngü yine
  join'e düşer) → pthread_join (worker ASLA öldürülmez, ~9 s üst
  sınır) → pointer restore (term-mid-busy kenarı) → settings_save →
  notcurses_stop. Worker yazma-yalnız disiplini (nc/g_ui'ye
  dokunmaz; ut_log/progress mutex'li) olduğundan stop-sonrası
  kullanım imkânsız: join stop'tan ÖNCE.*
- [x] **B5. Telemetri halkaları veri yarışı** — *Denetim 11-17:
  envanter: telem_poll tek çağrı noktası maybe_refresh (ui.c:621,
  busy-gated → UI thread'i), telem_reset yalnız 'r' tuş işleyicisi
  (UI thread'i), worker zinciri (profile_import/ctrl_fan_write/
  pw_apply_writes/mode_apply) telem_*'e hiç dokunmaz; csv logger ayrı
  süreç (paylaşılan bellek yok). Tek-yazıcı = UI thread'i; okuyan
  çizim aynı thread.*
- [x] **B6. Bayat durum gösterimi** — asusd.ron async flush; okuma katmanı
  daemon-first (10-01 düzeltmeleri). *Yeni okuma eklenince kural: mtime/
  daemon-öncelik denetimi.*
- [x] **B7. pending-staging semantiği** — *"değmedi" temsili envanteri
  11-17: üç editör üç şema ve hepsi sağlam — POWER: pw_touched
  (PW_T_* bitleri, açık bayrak) + PW_W_* yazma-kapı maskesi (10-30
  birliği); FAN: hw->fan_staged bool (yazıldı+doğrulandı → false,
  10-03); CORE LIMITS: cf_staged[id] 0-sentinel — invariant: tüm
  stage'leme yolları cf_clamp'ten geçer (gerçek makinelerde tavan
  ≥ yüzlerce MHz; patolojik cpu_mhz_min=0'da bile 0 = "stage'lenmedi"
  okunur → hayalet yazma doğamaz, güvenli yön). Okuma tarafı her
  yerde `!= 0 && != live` çift koşulu.*

## C — Süreç/IO disiplini

- [x] **C1. fd sızıntısı** — A1 ile aynı kanıt (pipe çiftleri fork hata
  yolunda dahi kapanıyor).
- [x] **C2. SIGPIPE yarışı** — ut_write_via_argv ignore-penceresi +
  exec_run/argv_run drain (141 sahte-FAILED dersi). *Yeni exec yardımcısı
  eklenince drain kuralı tekrarlanmalı.*
- [x] **C3. pidfile yarışı** — O_EXCL + atomik kendini-temizleyen claim
  (ctron-toggle.sh, eae980d).
- [x] **C4. Zombie/saat-dolan çocuklar** — *Denetim 11-17: süreç envanteri
  tam — system() YOK; fork ×2 (ut_write_via_argv, argv_run) ikisi de
  "timeout" önekli + waitpid'li; popen ×4 (exec_run "timeout 3 %s",
  ut_have_cmd/nvidia_smi_bin `command -v` builtin'i — anında çıkar,
  timeout gerekmez, gerekçe kodda yorumlu) + pclose'lu. Tüm display
  backend'leri ve daeboard süreç işi ut_exec_argv üzerinden.
  HARDENING LAND: TERM tek basamaktı (D-durumu çocuğu 3 sn sonra da
  yaşayabilir) → dört noktaya da `-k1` KILL eskalasyonu (exec_run
  dizgisi, ut_write_via_argv av, argv_run full[], control.c cpufreq
  toplu tee — av tavanı +6→+7, argv_run kapısı argc+2→argc+3, tek-arg
  "-k1" biçimi full[26] matematiğini korur). --status canlı rc=0.
  C8 belge notu da SECURITY.md §2.4'e girdi.*
- [x] **C5. Asılı çocuk** — *Denetim 11-17: daeboard seqpacket
  SO_RCVTIMEO bütçeli (left_ms, 11aae58); kscreen-doctor/hyprctl
  yalnız ut_exec_argv* → timeout -k1 3 tavanı; asusctl/busctl
  sabit-dizgi ut_exec → aynı tavan. Tüm dış çağrılar üst sınır
  taşıyor; soket tek istisna ve o da zaman aşımlı.*
- [x] **C6. Kabuk enjeksiyonu** — system() yok; popen yalnız sabit
  string'lerle (`ut_have_cmd`, nvidia_smi_bin); kullanıcı/config kökenli
  string argv vektörüne bile girmeden doğrulanıyor (nvidia_smi_bin
  '\''/boşluk reddi). *Yeni popen yasağı: kalıcı.*
- [x] **C7. /tmp ve öngörülebilir yol** — *Denetim 11-17: ctron runtime
  HİÇ geçici dosya yazmıyor — ut_log ring-only (dosya yok), tüm
  kalıcılık config dizininde (~/.config/ctron, mkdir_p 0755), testler
  mkdtemp kullanıyor. Tek /tmp teması toggle pidfile: O_EXCL+NOFOLLOW
  0600 + lstat sahiplik kapısı (10-26'da kapandı, C3 kapsamı).*
- [x] **C8. Ortam değişkeni enjeksiyonu** — *Denetim 11-17:
  CTRON_CONFIG bilinçli kullanıcı özelliği; config kökenli komut
  çalıştırma zinciri = modes.ini/.ctr anahtarları → cmd_run (kendi
  komut tablosu, değer-aralık guardları) → argv exec (shell yok —
  C6). Güven modeli SECURITY.md §2.4'te ARTIK AÇIKÇA yazılı: config
  dizini güvenilen kullanıcı-mülkü alan; yazabilen zaten o kullanıcı
  olarak komut çalıştırabilir. A5 fix'i (ut_fgets_full) ve set_note
  ret sözleşmesi de §2.4'e işlendi (bayat "rewritten truncated"
  ifadesi düzeltildi).*

## D — Ayrıştırma ve birimler

- [x] **D1. Yerel-ayar duyarlı sayı ayrıştırma** — LC_NUMERIC "C" kuralı
  (tr_TR strtod dersi, AGENTS.md kural 1). *Yeni strtod/atof eklenince
  TUI init sırası tekrar doğrulanmalı.*
- [x] **D2. Bozuk/düşman config** — *Denetim 11-17, batarya land etti
  (check_hostile_config, test_core): settings.ini — negatif/aralık-üstü/
  çöp değer clamp, çift anahtar son-kazanır, '='-siz/boş-anahtar satır
  atlanır, CRLF temiz; modes.ini — section'sız ve ']'-siz steps düşer,
  çift section doldur-bulunan kuralıyla son steps kazanır, boş ad/
  boş steps zarif; .ctr meta — 100-karakter not alan-a-truncate,
  çöp satırlar atlanır, fan-curve uçleri (10 nokta / saçma) zarif;
  fan_from_csv — sayı-olmayan/boş → -1, sayı-uymazlığı min alır,
  aralık-dışı noktalar clamp; mode_apply — bilinmeyen anahtar / çöp
  hz / aralık-dışı hz guard'ları YAZMADAN önce reddeder (hw=0 ile
  koşulur, donanım dokunulmaz). Bataryanın tamamlanması = asılı/
  çökme yok kanıtı. İlk koşuda tüm beklentiler tuttu.*
- [ ] **D3. Kernel/sysfs biçim kayması** — anahtar adı değişimleri
  (clocks_event_reasons alt-tire dersi), hwmon index kayması
  (index*/head-1 = L1d tuzağı — label doğrulaması şart), hex/dec
  karışımı. Ölçüt: her okuma label/örnekleme-doğrulamalı.
- [ ] **D4. Birim ölçek hataları** — mW/W, mA/A, kHz/MHz, %/oran.
  gpu_metrics gfx "watt" faciası dersi: **yuvarlak tavan sıfır-varyans
  = sayaç/aktivite, ölçer değil** — watt iddiasından önce birim kanıtı.
  Ölçüt: her yeni seri için birim kanıtı belgelendi mi?
- [x] **D5. Dürüst okuma** — bayat önbellek `--` (kural 3); BAT1 temp yok,
  cycle_count 0 gizli. *Yeni sensör eklenince kural tekrarlanır.*
- [x] **D6. Yaz-sonrası geri-okuma** — kscreen-doctor/Hyprland exit-0
  durmadan-yapmaz; integer refresh `@60` (kural 4). *Yeni display
  backend'inde (Arcioth) kural tekrarlanır.*
- [x] **D7. `asusd.ron` ayrıştırıcısı** — fallback tam-dosya okuma
  (5bbaa46) sonrası format: alan eksik/tip değişirse? *Denetim 11-17:
  mevcut test (check_asusd_ron_parse) silahlı/kapalı/anahtarsız/
  bilinmeyen-profil/NULL durumlarını zaten kapsıyordu; düşman eki
  land etti — silahlı ama profil-anahtarı YOK → -2 bilinmeyen (asla
  sessiz "kapalı" okunmaz), boş metin ve ron-olmayan çöp → kapalı,
  bool-as-sayı tip kayması → güvenli taraf (kapalı). Kesik profil adı
  mevcut "Whatever" vakasıyla aynı yola düşer (-2). Zarif düşüş.*

## E — Yazma kapısı disiplini (ctron'a özgü can-simgesi)

- [x] **E1. Poll döngüsünden yazma yasağı** — (kural 2) 09-29/10-01 read-only
  denetimleriyle kanıtlandı; *her turda: `ut_priv_write/ut_write_via_argv/
  ut_write_file` çağrı grafiği yalnız user-action yollarından geçmeli.*
- [x] **E2. asusd ezişi** — güç olayında profil zorlaması; management
  satırları + snapshot/restore + explicit-staging-önceliği (10-01).
  "AC/battery ters" false alarm'ı da kapandı.
- [ ] **E3. Yazma zinciri önceliği** — asusctl → sysfs → sudo -n her kontrol
  için gerçekten bu sırada mı; sudo yokken temiz FAILED (asla prompt/
  asılı — fan_sysfs_dead mandalı + fan_write_verdict üçlü ayrımı, 7011b56).
  Ölçüt: zincir dışına çıkan çağrı noktası kalmadı mı (grep)?
- [ ] **E4. Stale-PPT etkileşimi** — SMU limitleri uçucu (reboot/profil
  temizler); write-clamp reboot-sürekli. Ölçüt: gösterilen durum ile
  gerçek durum arasındaki fark dürüst etiketli mi (●/önizleme
  semantiği)? NEXT.md 19 kapanış kaydına karşı kod doğrulaması.
- [ ] **E5. FAN 'w' senkron EC yazması** — bilinçli kural ama worker
  aktifken FAN yazımı çakışıyor mu? Ölçüt: çakışma penceresi
  belgelenmiş/kapılı.
- [ ] **E6. GPU clock kilidi doğrulaması** — örnekleme-doğrulaması
  (ctrl_gpu_clock_ok) yalnız yükle çalışır; boşta "başarılı" sanılabilir
  mi? Ölçüt: doğrulama penceresi ve başarısızlık wording'i dürüst.

## F — TUI'ye özgü

- [x] **F1. ESC/tuş karışıklığı** — q-only quit (kural 5, Kitty CSI dersi).
- [x] **F2. ui_putln son-hücre** — genişlik-1 çağrı çizilmez dersi
  (10-30). *Yeni çizim kodunda kural hatırlanmalı.*
- [ ] **F3. Boyutlandırma/resize** — küçük terminal (min boyut altı),
  resize anında panel yerleşimi (ui_layout tek nokta), scroll viewport
  sınırları (pw_top), fullscreen overlay geçişinde boyut değişimi.
  Ölçüt: aşırı boyutlarla (çok küçük/çok geniş) görsel duman testi.
- [ ] **F4. UTF-8 genişlik/yerel metin** — toupper tr_TR 'i'→'WRiTE' dersi;
  çok baytlı karakter hücre sınırlarını bölüyor mu; tablo hizası.
  Ölçüt: Türkçe etiket + değişmez-etiket ASCII kuralı (büyük-harf
  etiketler) korunuyor mu?
- [ ] **F5. Girdi uçları** — tuş tekrarı (%8.7 ölçümlü sıkışma noktası),
  bracketed paste, mouse wheel panoklarında sınır, fullscreen 't' içinde
  global kısayol çakışması. Ölçüt: her yeni panel için kısayol
  çakışma tablosu.
- [ ] **F6. Terminal yetenek varsayımları** — truecolor yoksa, unicode
  glif yoksa (packed fallback var), terminal sorgusuna cevap vermeyen
  pty'de init asılmaz (tui_screenshot.py'nin varoluş nedeni).
  Ölçüt: notcurses init hatası temiz mesajla çıkar.

## G — Uzun koşu kaynak büyümesi

- [x] **G1. Poll başına tahsis** — yok (A1 kanıtı).
- [ ] **G2. Halka dışı sınırsız geçmiş** — since-reset istatistikler,
  per-core NOW tablosu, csv bellek tarafı: her birikimli yapının üst
  sınırı var mı? Ölçüt: sabit-boyut kanıtı.
- [ ] **G3. Log büyümesi** — ut_log döngüsü: dosya büyüklüğü sınırsız mı,
  rotasyon var mı? Karar: bilinçli bırakılıyorsa belgele.
- [ ] **G4. Uzun oturum ölçümü** — saatlerce açık TUI'da RSS eğrisi düz mi?
  Ölçüt: 1 saatlik /proc stat örnekleme (perf-audit yöntemi).

## H — CLI/argüman/çoklu-örnek

- [ ] **H1. Argüman ayrıştırma uçları** — `--hz 0`, `--ppt` eksik/bozuk
  değer, negatif, `--config-dir` kuyruksuz, bilinmeyen bayrak sessiz
  headless'e mi düşüyor? Ölçüt: her CLI komutu için kötü-girdi tablosu
  (hata mesajı + rc).
- [x] **H2. rc sözleşmesi** — FAILED CLI'da nonzero (fan 7011b56).
  *Yeni komut eklenince kural.*
- [x] **H3. Borulanmış çıktıda TUI açılmama** — (no-args=kural) piped
  tespiti. *Yeni giriş yollarında tekrar doğrulanır.*
- [ ] **H4. Çoklu örnek çatışması** — iki TUI (toggle claim'i var), TUI+CLI
  eşzamanlılık, iki csv logger aynı dosyaya. Ölçüt: çakışma davranışı
  belgeli (son-yazan kazanır mı, uyarı var mı).

## I — Taşınabilirlik/ortam

- [x] **I1. PATH/secure_path** — nvidia-smi mutlak yol çözümü (sudo reset),
  ryzenadj bare-name fallback, merged-usr /usr/sbin→bin tek-dosya dersi;
  NixOS read-only denetimi kırıcı yok (10-31).
- [ ] **I2. Kalan NixOS/Arcioth safa** — flake `nix build` ilk doğrulama,
  sudoers extraRules, ryzenadj secure_path testi, FA507NVR canlı dörtlüsü,
  Hyprland mouse. (Saha maddeleri; NEXT.md FAZ 2 ile senkron.)
- [ ] **I3. Kernel sürüm kayması** — perf PMU 6.14+ opsiyonel (paranoid=2
  EACCES), gpu_metrics offset'leri ASIC-bağımlı. Ölçüt: yok-sayma
  yolları --doctor'da görünür.
- [x] **I4. İzin düşüşü** — sudo'suz makinede temiz FAILED, sabit-durum
  mandalları (fan_sysfs_dead); RAPL 0400 root sessiz gizlenme.
- [ ] **I5. Opsiyonel bağımlılık yokluğu** — asusctl/daeboard/nvidia-smi/
  notcurses sürüm farkı yokluğunda özellikler zarifçe gizleniyor mu?
  Ölçüt: her opsiyonel özellik için yok-luk testi (--doctor çıktısıyla).

## J — Kalıcılık/veri bütünlüğü

- [ ] **J1. Yarım yazım bozulması** — .ctr/modes.ini/settings.ini `fopen(w)`
  ile yerinde yeniden yazılıyor: yazım ortasında güç kesilirse dosya
  bozulur. Ölçüt/karar: temp+rename (atomik değiştirme) gerekiyor mu —
  kod değişikliği önerisiyle bitir.
- [ ] **J2. Eşzamanlı yazım** — iki oturum aynı settings.ini'ye quit'te
  yazarsa (paralel oturum dersi). Ölçüt: last-writer-wins kabulü
  belgeli ya da kilit.
- [x] **J3. Format ileri-uyumluluk** — csv başlık kolon koruması
  (22 kolon uyarısı + yeni dosya zorlaması).

## K — Alan mantığı

- [x] **K1. Fan eğrisi değişmezleri** — 8 nokta, artan temp, pwm 0-255;
  pure model unit-testli. *Editor→apply yolunda yeniden doğrulama:
  editor'den gelen tablo da aynı değişmezden mi geçiyor?*
- [x] **K2. df-yüzde tavani** — df Use% tavan yuvarlar; df-exact yüzde
  (03a085f dersi).
- [ ] **K3. Sıfıra-bölme/boş-mengeç** — util%, busiest, min/max: boş
  halka/ilk poll "--" semantiği (16. tur kuralı) her yeni tabloda.
- [ ] **K4. Zaman hesapları** — since-reset, strftime, localtime_r
  hatası yolu; monotonik saat varsayımı. Ölçüt: sistem saati geriye
  alınırsa çılgın değer yok.

## L — Hata raporlama dürüstlüğü (ctron'un imzası)

- [x] **L1. Yazma hükmü wording'i** — fan_write_verdict üç durum
  (asusctl-ok / hata / yok) dürüst metin; Dragon Range geri-okuma
  yokluğu dürüst log.
- [ ] **L2. Yutulan dönüş değeri** — `-1` dönen çağrıların rc'si yok
  sayılıyor mu? Ölçüt: `grep -nE '^\s*(ut_|ctrl_|profile_|modes_|settings_|display_).*\(' src/` içinde sonucu kullanmayan çağrılar
  tek tek gerekçelenir ya da hata yolu eklenir.
- [ ] **L3. "Başarılı" sanılan kısmi başarı** — çok-adımlı apply'da (ppt
  üç limit, fan cpu+gpu) biri başarısız olursa raporlama parçalı mı,
  bütünsel mi? Ölçüt: her adımın sonucu tamamlama satırında ayrı
  görünür (tek-tamamlama-satırı kuralıyla çatışmaz: satır içinde
  parçalı durum).

---

## Denetim oturumu disiplini

1. Oturum = tek kalem; başta git log + NEXT.md + HANDOFF tazele.
2. Önce mevcut kanıt ara (bu dosya + hafıza + CHANGELOG) — kapanmış
   şeyi yeniden icat etme.
3. Dinamik kanıt > statik okuma > varsayım. Ölçüm yöntemini çıktıya yaz.
4. Bulgu varsa: önce bildir, düzeltmeyi kullanıcı onayıyla yap
   (okuma-yazmaz varsayılan); fix + test + gerekirse HELP satırı aynı
   commit'te.
5. Kalem kapanınca bu dosyada [x]'e çek + kanıt satırını yaz; HANDOFF +
   CHANGELOG'a kayıt düş.
