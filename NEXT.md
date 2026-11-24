# ctron — Açık İşler ve Yol Haritası

> Bu dosya **yalnızca açık işleri**, sürüm politikasını ve teknik dersleri
> taşır. Okuma sırası: `AGENTS.md` → `HANDOFF.md` → `NEXT.md` / `REVIEW.md`.
> Bitmiş işlerin kaydı `CHANGELOG.md` ve `docs/history/` altındadır.

## Sürüm politikası (agent yönetiminde)

Şema: **semver**.
- **PATCH** = yalnız hata düzeltme / regresyon.
- **MINOR** = yeni özellik veya UX (geriye uyumlu).
- **MAJOR** = kırıcı değişiklik (config formatı, CLI bayrağı kalkması).
- `src/main.c` `VERSION`, `README.md` sürüm rozeti ve `flake.nix` `version`
  aynı release commit'inde birlikte güncellenir (`vX.Y.Z` annotated tag o
  commit'i gösterir; push kullanıcı tarafındadır).
- Güncel sürüm: **`v0.11.0`** (geçmiş kesimler: `CHANGELOG.md`).

## Planlanan sürümler (`0.x` evrimi — `v1` hedefi şimdilik yok)

Ayrıntılı teknik gerekçeler ve kod referansları için bkz. [REVIEW.md](REVIEW.md)
(`v0.10.0` P0 doğruluk/hata kapanışı ve `v0.11.0` CLI/JSON/sıfır-shell paketi
tamamlandı):

- **`v0.12.0` (Yeni TUI Tasarımı & Kısayol Mimarisi):**
  - **Görsel & yerleşim yenilemesi ("AI slop" görünümünden çıkış):**
    - Zorla boyanan opak neon/cyberpunk kutular (`s_themes`) yerine terminalin
      doğal arka planına (`default bg` / şeffaflık) saygılı, sade ve yüksek
      kontrastlı tipografik tema sistemi.
    - 4 kutulu sıkışık panel + sahte `[ Buton ]` kalabalığı ve `CONTROLS` ↔
      `POWER`/`FAN`/`LIGHT` tekrarının kaldırılması; net sekme/görünüm tabanlı
      düzen (üstte tek satır sekme + özet durum, ortada temiz çalışma alanı,
      altta tek birleşik kısayol + durum/log çubuğu; panel içi tekrar eden
      `j/k · h/l · ...` metin şeritlerinin temizlenmesi).
    - Tutarlı yazma/etkileşim dili (bir panelde anında yazıp diğerinde stage
      eden kafa karıştırıcı ikili modelin sadeleştirilmesi).
  - **Tuş atamaları & kısayol düzenlemesi:**
    - `1..4` odak / `5..8` sekme bölünmüşlüğünün kaldırılması; doğrudan `1..N`
      ve `Tab` / `Shift+Tab` (`[` / `]`) ile ana görünümler arasında düz geçiş.
    - `ESC` tuşunun **yalnızca geri / iptal / kapat** yapması (`ESC` ile
      Ayarlar açma davranışının kaldırılması; Ayarlar için `,` veya kendi sekmesi).
    - Çakışan tek-harf kısayollarının (`s` save/settings/stats, `t` telem/type,
      `c` note/cpu/cores, `Enter` vs `w`) ekrandan ekrana anlam değiştirmeyen
      tek bir kısayol tablosuna (`keymap`) oturtulması.
- **`v0.13.0` (Paketleme, `udev` Ayrıcalık Azaltma & Masaüstü/CLI Artefaktları):**
  `Makefile` (`PREFIX`/`DESTDIR`, tek `VERSION`, `make check-version`) +
  `flake.nix` uyumu, `contrib/` (`99-ctron.rules` + `ctron.tmpfiles.conf`,
  `ctron-ppt`, `sudoers.ctron.example`, `ctron.desktop`, `PKGBUILD`),
  `man/ctron.1`, shell completions (`bash`, `zsh`, `fish`), `CONTRIBUTING.md`.
- **`v0.14.0` (Offline Test Kalkanı, CI/CD & Modüler Temizlik):**
  Yeni TUI/kısayol şeması oturduktan sonra `CTRON_SYSFS_ROOT` sahte `/sys`+`/proc`
  testleri + `scripts/tui_smoke.py --offline`, `.github/workflows/ci.yml`
  (`gcc`+`clang`, `HARDEN=1`, UBSan/LSan, `clang --analyze`, `shellcheck`,
  offline smoke), `src/hw.c` ve UI modül bölünmesi + gizli `static` önbellek
  temizliği.

## Açık saha ve aday maddeler

1. **`flake.nix` doğrulaması (FA507NVR / Arcioth):** Bu makinede nix yok →
   `nix build`, `nix develop` içinde `make`, ilk koşuda `flake.lock` üretilip
   commit edilmeli (`AUDIT.md` I2).
2. **FA507NVR canlı testleri (Arcioth):**
   - `pwmN_enable=1` yazımı (custom mod canlı), asılı daeboard soketi
     (`SO_RCVTIMEO` canlı), `--gpu-clock` canlı, çok monitör canlı kanıtı
     (HDMI-A-1 + panel).
   - `security.sudo.extraRules` + `ryzenadj` `secure_path` doğrulaması.
   - **Rembrandt-R (7435HS) sensör-fark kontrolü:** (a) `ryzenadj -i` canlı PPT
     okuma denemesi (Dragon Range'de NO-GO, Rembrandt'ta çalışabilir);
     (b) k10temp tek-CCD (`Tccd1` var, `Tccd2` yok — label-doğrulamalı okuma
     sorunsuz küçülmeli); (c) `/sys/class/hwmon` içinde `amd_energy` varlığı.
3. **FA507NVR / Hyprland'de TUI mouse:** Bilinen açık (v1 edge-tab geçmişi;
   `README.md` Display backends → Hyprland handoff state).
4. **FA608PP kalan saha testleri:** Post-OCCT kernel clamp → reboot → yeniden
   test; fırsat çıkarsa harici monitör takılıyken KDE çok-ekran listesi.
5. **Aday mikro sensör ekleri (karar kullanıcıda, kod değişikliği yok):**
   - PD port-başına sözleşme detayı (V/A + `USB_TYPES` + online; şimdi yalnız
     toplam mW var).
   - `TIME_TO_EMPTY_NOW` / `TIME_TO_FULL_NOW` — pil bölümüne kalan süre satırı.
   - NVIDIA `power.max_limit` / `min_limit` — POWER GPU satırına tavan bağlamı.

## Bilinçli park (tek liste — `README.md` Roadmap ile eş güncellenir)

- Waybar renk senkronu (v1 özelliği, v2'de bilinçli yok; `--json` çözecek)
- MUX / dGPU anahtarı (`gpu_mux_mode`, `dgpu_disable` — reboot akışı gerekir)
- `throttle_thermal_policy` (platform profilleriyle çakışır)
- `wlr-randr` / `xrandr` display backend'leri
- UI dil tablosu (EN/TR)
- Polkit helper (`SECURITY.md` Phase 3 — koşul: çok-kullanıcılı kurulum ihtiyacı)
- Per-key klavye rengi (bu klavye tek bölgeli)
- `daeboard` daemon-taraflı işler (soket efekt komutları, NixOS unit, AUR) —
  daemon repo'sunda yaşar, ctron'da değil

## Teknik dersler (tekrar etme)

- TUI'de `setlocale(LC_ALL, "")` sonrası **`setlocale(LC_NUMERIC, "C")`**
  (tr_TR'de `strtod("59.87")` → `59`). `toupper()` yerine ASCII dönüşümü.
- `kscreen-doctor`: refresh **tam sayı** (`@60`), exit 0'a güvenme — geri-oku.
  Hyprland 0.55+: `keyword monitor` no-op, `hl.monitor` eval kullan.
- `amd-pstate`'te her CPU'nun politikası ayrı → cpufreq yazımı `cpu0..cpuN`.
- `NCKEY_ESC == 0x1b`; onu stray-CSI filtresine sokma. Quit yalnız `q`/`Q`.
- `Makefile`'da `.o` hedefleri header'lara bağımlı olmalı (struct kayması).
- PPT sysfs okuma bu makinede stale (`0`/`5`) → `--` bas, asla uydurma.
- `gpu_metrics` u32@28 watt değil gfx **aktivitesi** (yüzde-yüzde-bir) —
  Dragon Range'de root'suz CPU/iGPU watt ayrımı yok.
- `sudo -n` parolasız değilse temiz hata verir, asla parola sormaz/asılmaz.
- PTY smoke testlerinde terminal yanıtlayıcısını ~1 s sonra sustur (geç yanıt
  ESC olarak ayrışır).

## Hızlı komutlar

```bash
make && make test && ./build/ctron --doctor && ./build/ctron --status
make tuitest                     # scripts/tui_smoke.py (pty responder)
make install                     # ~/.local/bin/ctron
./build/ctron                    # TUI (argsız); ESC = settings overlay
sudo ~/.local/bin/ctron --freq 5386   # frekans tavanını geri aç (gerekirse)
```
