# ctron — SONRAKİ OTURUM İÇİN NOT (2026-09-23)

> Yeni bir ZCode/AI konuşmasında projeye devam ederken **ilk olarak bu
> dosyayı**, ardından `AGENTS.md` ve `HANDOFF.md`'yi oku. Bu dosya,
> 2026-09-19 → 09-23 arası yoğun oturumların özeti + sıradaki işlerdir.

## Durum (2026-09-23)

- **Tek kaynak: `~/Projeler/ctron`** (GitHub: kerempkl/ctron, main).
  `~/Projeler/glm deneme` altındaki `ctron`, `ctron deneme`, `ctron 2.1`
  emekli kopyalardır — oradan buraya **asla** dosya taşıma.
- Çalışma ağacı temiz; son commit `4db3e96` (Arcioth, PLANS.md).
  Kullanıcı "Session ..." adıyla düzenli commit atıyor — devam etsin.
- Binary: `~/.local/bin/ctron` (v1 yedeği: `ctron.v1.bak`).
- Makineler: **FA608PP** (Kerempkl, CachyOS/KDE Wayland) + **FA507NVR**
  (Arcioth, NixOS/Hyprland 0.55). Hyprland backend Arcioth'un sahası.

## Bu oturumlarda bitenler (kısaca)

- v2 tam yeniden yazım: CLI-first + argsız tam ekran TUI (notcurses),
  sol kolon PROFILES/CONTROLS, sağ WORKSPACE + LIVE telemetri.
- Yazma zinciri: asusctl → doğrudan sysfs → `sudo -n` (parolasız yoksa
  temiz hata). Poll döngüsünde asla yazma yok.
- Display backend'leri: **kde** (tam çalışır) + **hyprland** (Arcioth:
  array JSON + `hyprctl eval hl.monitor`).
- Modlar (modes.ini, `--mode`/TUI Settings), .ctr profiller (serbest isim).
- **ESC = btop tarzı yüzen Settings penceresi** (çift çerçeve + gölge,
  dışına tıklayınca kapanır) → LAYOUT bölümü: swap_left, telem_top,
  left_pct, split_pct, telem_h (anında uygulanır, settings.ini'ye kalır).
- POWER'a **PPT limits** satırı + `--ppt off|on` (off = tavanlar, on =
  hatırlananlar; profil-çevirme numarasının temiz hali).
- Kritik bug düzeltmeleri: tr_TR LC_NUMERIC (59/164 hayalet Hz),
  kscreen-doctor int-refresh + rc=0 yalanı → read-back doğrulama,
  per-CPU cpufreq yazımı (cpu0-only 31 çekirdeği 2.4'te kilitliyordu),
  lone-ESC yutan filtre, Makefile header bağımlılıkları, ghost-text
  (pencere içi fill), `ut_path_join`/`ui_btn_row`/`dash_if` temizliği.

## SIRADAKİ İŞLER (öncelik sırasıyla)

1. **[BİTTİ 10-02]** asusd takeover çakışma uyarısı: takeover şu anki
   güç kaynağında ayaktaysa ve dayattığı profil canlı profilden
   farklıysa POWER'daki profile satırında `⚠asusd` işareti, yükselen
   kenarda bir kez log, apply toast'una "asusd will revert" notu ve
   `--status`'ta asusd note satırı (`hw_asusd_enforced`, saf +
   birim-testli). FA608PP'de dormant (takeover bilinçli kapalı).
2. **[BİTTİ 10-02]** CORE LIMITS editörü fiziksel çekirdek + CCD görünümü:
   SMT çiftleri `c0·16` hücresi, çok-CCD CPU'da CCD1/CCD2 (L3 alanı)
   başlıkları; header seçiliyken h/l/t/o/a tüm CCD'ye, `a` = CCD, `A` =
   tümü. Topoloji tamamen sysfs'ten (thread_siblings_list + L3
   shared_cpu_list), SMT'siz/L3'siz/>2-thread makinelerde dürüst geri
   çekilme (eski per-thread grid). Saf gruplama birim-testli; tuitest
   gerçek makinede CCD başlıklarını doğruluyor.
2. **[BİTTİ 10-02]** cpufreq toplu yazma + per-core indeksleme gerçek cpu
   id listesi (`hw->cpu_ids`): `cpufreq_write_all` artık "ilk eksik yolda
   dur" yerine kernel'in present listesini (`0-15,32-47` gibi aralıklı
   olabilir) iterasyon ediyor; CORE LIMITS editörü, `freq core N M`,
   profil export'u dahil tüm per-core erişim gerçek id ile indeksleniyor.
   Bitişik makinelerde davranış birebir aynı. (NOT: FA608PP'de şu an
   parolasız `sudo -n tee` yok — yetkili yazmalar temiz hata veriyor,
   sudoers düzeltilince geri gelir.)
2. **[BİTTİ 10-01]** hwmon/power yolu önbelleği: yollar `hw_state_t.paths`
   içinde keşfedilip saklanıyor (fan eğrisi, k10temp, fan RPM, batarya,
   AC); 250 ms poll artık yalnızca değer okuyor (~18 keşif open'ı/poll
   kalktı). Cache'li yoldan okuma başarısız olursa giriş düşürülür,
   sonraki poll yeniden probe eder (suspend/resume yeniden numaralandırma
   için). Yazma katmanı fan tabanını aynı cache'ten alıyor
   (`hw_path_fan_curve`). Dürüst not: CPU zamanında ölçülebilir fark yok
   (glob'lar bu makinede ucuz) — kazanç syscall sayısı ve okuma/yazma
   yolu tutarlılığı. nvidia-smi 2 sn cache dokunulmadı.
2. **[BİTTİ 09-27]** pty test harness repoda: `scripts/tui_smoke.py` +
   `make tuitest`. Responder yalnız ilk ~1 sn CPR/DA1/kitty `?u`/sync
   2026/OSC 4-10-11 sorgularını yanıtlar (sonra sessiz — geç cevaplar
   ESC'ye ayrışıyor). Akışlar: aç/çık exit 0, ESC settings overlay,
   POWER net-sıfır stage+apply (log satırı, donanıma yazmaz), fan
   kontrol satırı tıklamaları (ID çakışması regresyonu) + Write
   tıklaması gerçek eğri yazması. Üç koşu üst üste yeşil.
3. **[BİTTİ 09-23]** POWER görünümüne CPU frekans satırı eklendi:
   "CPU clock limit" satırı (h/l/Enter ±100 MHz, per-CPU `scaling_max_freq`
   yazımı; değer canlı okunur, bilinmiyorsa `--`). Kalan yarım:
   `--tctl`-vari gösterge (sıcaklık/tavan mesafe göstergesi).
4. **[BİTTİ 09-28]** Listelere kaydırma: PROFILES ve CLI SHORTCUTS
   (modes.ini) listelerinde seçim pencere dışına çıkarsa liste seçimi
   takip edecek şekilde kayar (draw sırasında clamp; ▲/▼ ipuçları
   taşan tarafı gösterir).
5. **[BİTTİ 09-27]** Fan yazma doğrulaması + "applying..." göstergesi:
   `ctrl_fan_write` sonrası 8 nokta + enable geri okunup log'da
   doğrulanıyor (`· verified 8/8 + 8/8 pts` / `VERIFY FAILED`);
   padding mantığı `fan_point()`'a çekildi, yazma ve doğrulama aynı
   değeri kullanıyor. Uzun çağrılar (fan yazma, preset, profil, mode,
   power apply) öncesi footer'a anında `applying...` basılıyor
   (`ui_flash()`: telemetry log satırı üzerine + senkron render).
6. **flake.nix geri ekle** (v1'de vardı, v2'ye gelmedi; Arcioth NixOS'ta
   derleyemiyor şu an).
7. **Mini telemetri grafikleri** (btop tarzı blok karakterlerle son ~60
   örnek) — Arcioth'un PLANS.md "Fullscreen dashboard telemetry" planıyla
   birleştirilebilir (bkz. `PLANS.md`).
8. **Park edilenler:** Waybar senkronu, MUX/dGPU anahtarı,
   throttle_thermal_policy, polkit/udev yazma yolu, UI dil tablosu (EN/TR).

## Arcioth'un yeni gündemi (PLANS.md, 2026-09-23)

- `PLANS.md` içinde üç plan: **Fullscreen dashboard telemetry**,
  **daeboard client** (klavye ışık daemon'u; root, `--binds`, AUR
  kurulum), **daeboard control**. Bunlar "later plans" — ctron çekirdeği
  değil ekleri; çakışmasın diye sıradaki işlere önce 1-6 girsin.

## Denetimden kalan (P4 — 2026-10-03 tam kod denetiminden, öncelik sırasıyla)

1. **[BİTTİ 10-06]** util: `ut_write_file` hata yolundaki fd sızıntısı
   kapandı (fwrite/fflush kısa devresi fclose'u atlıyordu — fclose artık
   her koşulda çağrılıyor) ve `ut_priv_write` sudo fallback'i exec'ten
   ÖNCE değer/yoldaki `'`'ı temiz -1 ile reddediyor. /dev/full +
   /proc/self/fd sayımıyla mutasyon-kanıt test (`check_util_write`).
2. **[BİTTİ 10-08, 11aae58]** daeboard: `exchange()` artık 2 sn
   SO_RCVTIMEO + CLOCK_MONOTONIC tavanlı (fire satırları da bütçeyi
   paylaşıyor); `db_action_in` kelime-sınırlı eşleşiyor (`ctronx=`
   eşleşmez) ve '='-siz satır aramayı iptal etmez (`goto next_line`).
   test_daeboard'da regresyon testli. Kalıntı (park): connect/send
   zamanlamasız — tam-backlog bloğu egzotik, yalnızca recv kapped.
3. **[BİTTİ 10-10]** control dürüstlüğü: pwm1/2_enable dönüşü 11aae58'de
   kontrol+geri-okunur hâle geldi; hwmon yokken "fan curves written"
   yalanı BUGÜN kapandı — saf `fan_write_verdict` (control.h) kararı
   ASUSCTL/FAILED'a ayırıyor, asusctl yoksa/hata verdiyse staging
   korunur ve rc -1 döner (CLI nonzero çıkar). Birim testli
   (`check_fan_write_verdict`, 11 durum).
4. **ui kozmetik — büyük ölçüde bitti:** `ui_row` byte-padding'i
   `ut_cell_join` ile hücre-duyarlı (birim testli, `check_cell_join`);
   topbar çipleri 10-02'de gerçek genişlikten sağa-hizalı. KALAN:
   draw_light swatch sabiti `x+40` dar terminalde çerçeveyi ezebilir.
5. **[BİTTİ]** repo: `__pycache__/` .gitignore'da, track edilen kalmadı.
6. **display (parked)**: kde/hypr mod listesi yalnızca ilk monitörü
   görüyor (HDMI-A-1 kalemi — zaten açık).

## Teknik dersler (tekrar etme!)

- TUI'de `setlocale(LC_ALL,"")` sonrası **`setlocale(LC_NUMERIC,"C")`**
  (tr_TR'de strtod "59.87"→59).
- kscreen-doctor: refresh **tam sayı** (`@60`), exit 0'a güvenme —
  read-back doğrula. Hyprland 0.55: `keyword monitor` no-op, `hl.monitor`
  eval kullan.
- amd-pstate'te her CPU'nun politikası ayrı → cpufreq yazımı cpu0..cpuN.
- `NCKEY_ESC == 0x1b`; onu "stray CSI" filtresine sokma.
- Makefile'da `.o` hedefleri header'lara bağımlı (yoksa struct kayması
  sessiz veri bozulması — yaşandı).
- PPT sysfs okuma bu makinede stale (`5`) → `--` bas, asla uydurma.
- `sudo -n` parolasız değilse yazma temiz hata verir, asla prompt/sızma.
- Test ortamı notu: TUI pty testlerinde responder'ı 1 sn sonra sustur.

## Hızlı komutlar

```bash
cd ~/Projeler/ctron
make && make test && ./build/ctron --doctor && ./build/ctron --status
make install                      # ~/.local/bin/ctron
./build/ctron                     # TUI (argsız); ESC = settings penceresi
sudo ~/.local/bin/ctron --freq 5386   # frekans tavanını geri aç (root'suz path!)
```
