# ctron — YOL HARİTASI (2026-10-25)

> Bu dosya artık **yalnızca açık işleri** taşır (tek kaynak). Okuma
> sırası: `AGENTS.md` → `HANDOFF.md` → burası. Bitmiş işlerin kaydı
> `CHANGELOG.md` + `HANDOFF.md`'dedir; 09-19 → 10-23 döneminin eski
> todo arşivi bu dosyanın `5570b48` halidir (git geçmişinde durur).

## Sürüm politikası (agent yönetiminde — 10-25 kararı)

Kullanıcı sürüm numaralandırmasını agent'a devretti. Şema: semver.

- **PATCH** = yalnız hata düzeltme / regresyon.
- **MINOR** = yeni özellik veya UX (geriye uyumlu).
- **MAJOR** = kırıcı değişiklik (config formatı, CLI bayrağı kalkması).
- Etiket `vX.Y.Z` **annotated** olarak atılır ve VERSION bump'la **aynı
  release commit'ini** işaret eder. Tag'i push etmek kullanıcı tarafında
  kalır (her push gibi).
- README'deki `version` badge'i de aynı release commit'inde güncellenir
  (10-25 README yeniden tasarımıyla geldi).
- İlk kesim **v0.3.0** — 0.2.1'den (09-27) bu yana birikim MINOR
  seviyesinde: fan presets modali, M4 toggle + tek-örnek guard,
  PPT/ryzenadj yazma yolu, GPU clock lock, daeboard control + editör,
  apply worker. *(10-25'te atıldı; sıradaki kesim bir sonraki MINOR veya
  PATCH birikiminde.)*

## FAZ 1 — Bakım & dürüstlük (kısa vade)

1. **[BİTTİ 10-25]** v0.3.0 sürüm kesimi (yukarıdaki politika).
2. **[BİTTİ 10-26]** Provenance düzeltmesi (10-18 bulgusunun doc/kod
   yansıması):
   `src/fan.c`'teki S_SNAPSHOT provenance yorumu ve presets modalındaki
   kaynak etiketi "NOT verified factory" temelli söylemden
   "asusd default = generic fallback; bu anlık görüntü EC-seed'li fabrika
   referansı" söylemine (kanıt: 10-18 asusd probe — asusd'ın
   yeniden ürettiği default'lar generic; eldeki ron EC-seed'li en iyi
   referans). HARDWARE.md'nin EN + TR bölümlerine
   `asusctl fan-curve --default` footgun uyarısı (generic kurar, fabrika
   değil).
3. **flake.nix** — v1'de vardı, v2'ye gelmedi. Taşı; Arcioth bugün
   `nix-shell -p gcc pkg-config notcurses gnumake --run make` ile elle
   derliyor, flake bunu kalıcı hale getirir. Doğrulama FA507NVR
   tarafında (Arcioth).

## FAZ 2 — Saha & doğrulama borcu (makineli, paralel yürür)

4. **FA507NVR canlı testleri (Arcioth):** `pwmN_enable=1` yazımı
   (custom mod canlı), asılı daeboard socket'i (SO_RCVTIMEO yolu canlı),
   `--gpu-clock` canlı. (HANDOFF 10-08 Grok oturumunun "not done live"
   listesi — bugüne dek hiçbir todo dosyasında değildi.)
5. **FA608PP saha testleri:** CCD kolonlarını elle doğrulama; post-OCCT
   kernel clamp → reboot → yeniden test.
6. **Çok monitör:** kde backend yalnız ilk `Output:`/`Modes:` bloğunu
   okuyor (`display_kde.c` `kde_parse_output_name`/`kde_parse_modes`);
   hypr backend availableModes'i bellekteki ilk diziden alıyor, odaklı
   monitörden değil (`display_hypr.c` `hypr_modes_hz`). Doğrulama
   HDMI-A-1 takılıyken (FA507NVR). README "Hyprland handoff notes"
   checklist'iyle aynı madde.
7. **Hyprland'de TUI mouse** (v1 edge-tab geçmişi; README handoff).

## FAZ 3 — Güvenlik yapısal (SECURITY.md Faz 1-2 sırasıyla)

8. **[BİTTİ 10-26]** `ut_priv_write`: `/bin/sh -c` → fork + execve argv — shell
   enjeksiyon sınıfını tamamen kapatır; mevcut tırnak-red testleri
   korunur, yenileri argv vektörünü kanıtlar. (SECURITY.md Faz 1.1.)
9. **[BİTTİ 10-26]** `ut_exec_argv` + migrasyon — nvidia-smi / kscreen-doctor /
   hyprctl çağrıları string-komuttan argv vektörüne. (Faz 2.1.)
10. **[BİTTİ 10-26]** Pidfile/claim sertleştirme denetimi — O_CREAT|O_EXCL|O_NOFOLLOW
    + sahiplik doğrulaması, özellikle `/tmp` fallback'inde; zaten
    sağlamsa testle kanıtla, değilse düzelt. (Faz 1.3.)
11. **Sudoers sertleştirme dokümanı** — ctron-ppt wrapper betiği +
    yol-kapsamlı kurallar (kurulum tarafı; kod değişikliği değil).
    (Faz 1.2.)

## Kapsam kararı bekliyor

- **Mini telemetri grafikleri** — iki alternatif: (a) pure örnek-ring +
  sparkline render çekirdeği önce (birim-testli; poll döngüsü yalnız
  okur), LIVE şeridinde 1-2 satır; `--dash` tam ekranı sonra aynı
  çekirdekle. (b) PLANS.md "Fullscreen dashboard telemetry" planı
  aynen, `--dash` tek seferde (grafikler + detay bloğu + ASCII art).
  Kullanıcı 10-25'te "şimdilik park" dedi; açılınca (a)/(b) kararı
  verilecek. PLANS.md'deki plan referans olarak duruyor. Kodda bugün
  graph/ring/history yok — telemetri salt anlık-değer metni
  (`panel_telemetry.c`).

## Bilinçli park (tek liste — README Roadmap ile eş güncellenir)

- Waybar renk senkronu (v1 özelliği, v2'de bilinçli yok)
- MUX / dGPU anahtarı (`gpu_mux_mode`, `dgpu_disable`) — reboot akışı
  gerekir
- `throttle_thermal_policy` — platform profilleriyle çakışır, iki efendi
  istemiyoruz
- wlr-randr / xrandr display backend'leri
- UI dil tablosu (EN/TR)
- polkit helper (SECURITY.md Faz 3) — koşul: çok-kullanıcılı kurulum
  ihtiyacı
- per-key kbd color (bu klavye tek bölgeli)
- daeboard daemon-taraflı işler (socket efekt komutları, NixOS unit,
  AUR paketi) — daemon repo'sunda yaşar, ctron'da değil

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
make tuitest                     # scripts/tui_smoke.py (pty responder)
make install                     # ~/.local/bin/ctron
./build/ctron                    # TUI (argsız); ESC = settings penceresi
sudo ~/.local/bin/ctron --freq 5386   # frekans tavanını geri aç (root'suz path!)
```
