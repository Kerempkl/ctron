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
- Güncel sürüm: **`v0.12.0`** (geçmiş kesimler: `CHANGELOG.md`).

## Planlanan sürümler (`0.x` evrimi — `v1` hedefi şimdilik yok)

Ayrıntılı teknik gerekçeler ve kod referansları için bkz. [REVIEW.md](REVIEW.md)
(`v0.10.0` P0 doğruluk/hata kapanışı, `v0.11.0` CLI/JSON/sıfır-shell ve
`v0.12.0` Konsept C TUI + birleşik kısayol yenilemesi tamamlandı):

- **`v0.13.0` (Paketleme, `udev` Ayrıcalık Azaltma & Masaüstü/CLI Artefaktları):**
  `Makefile` (`PREFIX`/`DESTDIR`, tek `VERSION`, `make check-version`) +
  `flake.nix` uyumu, `contrib/` (`99-ctron.rules` + `ctron.tmpfiles.conf`,
  `ctron-ppt`, `sudoers.ctron.example`, `ctron.desktop`, `PKGBUILD`),
  `man/ctron.1`, shell completions (`bash`, `zsh`, `fish`), `CONTRIBUTING.md`.
- **`v0.14.0` (Offline Test Kalkanı, CI/CD & Modüler Temizlik):**
  `CTRON_SYSFS_ROOT` sahte `/sys`+`/proc` testleri +
  `scripts/tui_smoke.py --offline`, `.github/workflows/ci.yml`
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
5. **FA507NVR aura doğrulaması (Arcioth):** TUF aura arayüzü asusctl 6.x
   şemasıyla okunuyor (`/xyz/ljones/aura/tuf`, D-Bus; yedek
   `/etc/asusd/aura_tuf.ron` — NixOS'ta yol aynı mı?). `aura_support.ron` FA507
   için 5 efekt listeliyor: `ctron --doctor` → `aura effects`, çok-efektli
   klavyede `--aura breathe red` geri okuması ve daeboard açıkken aura'nın
   `--` kalması canlı görülmedi.
5. **Aday mikro sensör ekleri (karar kullanıcıda, kod değişikliği yok):**
   - PD port-başına sözleşme detayı (V/A + `USB_TYPES` + online; şimdi yalnız
     toplam mW var).
   - `TIME_TO_EMPTY_NOW` / `TIME_TO_FULL_NOW` — pil bölümüne kalan süre satırı.
   - NVIDIA `power.max_limit` / `min_limit` — POWER GPU satırına tavan bağlamı.
6. **Aday snapshot alanları (karar kullanıcıda, kod değişikliği yok):**
   - `nv-boost` / `nv-temp` komut tablosunda var, POWER'dan bilinçli
     çıkarıldılar (tek "GPU clock limit" satırı) — snapshot'a girsin mi?
   - `fan = stock|silent|cool|full` hazır ayarları (eğri satırları zaten
     aynı bilgiyi taşıyor).
   - `battery-oneshot` bir eylem, durum değil — snapshot'a girmez.

## Bilinçli park (tek liste — `README.md` Roadmap ile eş güncellenir)

- Waybar renk senkronu (v1 özelliği, v2'de bilinçli yok; `--json` çözecek)
- MUX / dGPU anahtarı (`gpu_mux_mode`, `dgpu_disable` — reboot akışı gerekir)
- `throttle_thermal_policy` (platform profilleriyle çakışır)
- `wlr-randr` / `xrandr` display backend'leri
- UI dil tablosu (EN/TR)
- Polkit helper (`SECURITY.md` Phase 3 — koşul: çok-kullanıcılı kurulum ihtiyacı)
- Per-key klavye rengi (bu klavye tek bölgeli)
- Snapshot'ta static DIŞI AURA efektlerini yeniden oynatma (asusd mod
  numaralarını belgelemiyor; yalnız `static = 0` kanıtlı — bugün yalnız renk
  ve static yazılıyor; FA608PP'de zaten tek efekt var)
- AURA güç durumları (açılış/uyku/kapanış LED animasyonları —
  `asusctl aura power-tuf`)
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
- `asusctl aura effect <desteklenmeyen efekt>` `NotSupported` basıp **0 ile
  çıkar** (6.5.0, FA608PP): aura yazması yalnız geri okumayla "ok" olur.
- AURA mod indeksi→ad eşlemesi yalnız `0 = static` için kanıtlı (dosya adı +
  D-Bus aynı anda); diğer indeksleri `AURA_EFFECTS` sırasıyla eşleme.
- Snapshot ve POWER çekirdek ızgaraları ayrı havuz kullanır
  (`prof_cf_staged` / `cf_staged`); ortak havuz ray'de sahte `Power ●` /
  `N pending` üretti.
- Smoke: terminal yalnız değişen hücreleri yeniler — `wait_for` yeni DEĞERİ
  aramalı, yerinde duran metni değil; `.ctr` kontrolünde `"profile ="`
  alt dizgisi `ac-profile =`'e takılır → `"\nprofile = "` (satır bazlı).

## Hızlı komutlar

```bash
make && make test && ./build/ctron --doctor && ./build/ctron --status
make tuitest                     # scripts/tui_smoke.py (pty responder)
make install                     # ~/.local/bin/ctron
./build/ctron                    # TUI (argsız); ESC = settings overlay
sudo ~/.local/bin/ctron --freq 5386   # frekans tavanını geri aç (gerekirse)
```
