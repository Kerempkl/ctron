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

1. **hwmon/power yolu önbelleği (en çok kazandıran).** `hw_refresh_fast`
   her 250 ms'de `glob()` ile k10temp/fan/batarya/AC yollarını yeniden
   arıyor (`hw.c`, `hw_hwmon_path`/`power_supply_find` çağrıları).
   Yollar `hw_init`'te bir kez bulunup struct'a saklanmalı; poll yalnızca
   değer okumalı. nvidia-smi 2 sn cache'te — dokunma.
2. **pty test harness'ını repoya al.** `scripts/tui_smoke.py` +
   `make tuitest`. Harness şart: terminal sorgularına cevap veren
   responder (CPR `\x1b[1;1R`, DA1, kitty `?u`, sync `2026`, OSC 4/10/11
   renkler) — respondersız pty'de `notcurses_init` asılır (test kusuru,
   programın değil). Cevapları yalnız ilk ~1 sn gönder; sonra sessizlik
   (geken cevaplar ESC'ye ayrışıp pencere açabiliyor). Akış testleri:
   `s`/ESC pencere, `3`+`p` POWER, `jjl` satır değişimi, `q` exit 0.
3. **[BİTTİ 09-23]** POWER görünümüne CPU frekans satırı eklendi:
   "CPU clock limit" satırı (h/l/Enter ±100 MHz, per-CPU `scaling_max_freq`
   yazımı; değer canlı okunur, bilinmiyorsa `--`). Kalan yarım:
   `--tctl`-vari gösterge (sıcaklık/tavan mesafe göstergesi).
4. **Listelere kaydırma:** PROFILES/CLI SHORTCUTS listeleri taşınca
   kırpılıyor; pencere içi scroll (seçim pencere dışına çıkınca kaydır).
5. **Fan yazma doğrulaması:** fan curve hwmon'u güvenilir okunuyor —
   `ctrl_fan_write` sonrası geri oku, log'da doğrula. Ayrıca uzun
   çağrılar sırasında footer'a "applying..." (UI donması hissi).
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
