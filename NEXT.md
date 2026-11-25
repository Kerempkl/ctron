# ctron — Açık İşler ve Yol Haritası

> Bu dosya **yalnızca açık işleri**, sürüm politikasını ve teknik dersleri
> taşır. Okuma sırası: `AGENTS.md` → `HANDOFF.md` → `NEXT.md` / `REVIEW.md`.
> Bitmiş işlerin kaydı `CHANGELOG.md` ve `docs/history/` altındadır.

## Sürüm politikası (agent yönetiminde)

Şema: **semver**.
- **PATCH** = yalnız hata düzeltme / regresyon.
- **MINOR** = yeni özellik veya UX (geriye uyumlu).
- **MAJOR** = kırıcı değişiklik (config formatı, CLI bayrağı kalkması).
- Kök `VERSION` dosyası sürümün **tek yazıldığı yer**; Makefile onu derleyiciye
  verir (`-DCTRON_VERSION`, `CFLAGS` dışında). Okuyamayan kopyalar — `README.md`
  sürüm rozeti, `flake.nix` `version`, `contrib/PKGBUILD` `pkgver` — ve
  `CHANGELOG.md` başlığı `make check-version` ile aynı release commit'inde
  doğrulanır (`vX.Y.Z` annotated tag o commit'i gösterir; push kullanıcı
  tarafındadır).
- Güncel sürüm: **`v0.14.0`** (geçmiş kesimler: `CHANGELOG.md`).

## Planlanan sürümler (`0.x` evrimi — `v1` hedefi şimdilik yok)

Ayrıntılı teknik gerekçeler ve kod referansları için bkz. [REVIEW.md](REVIEW.md)
(`v0.10.0` P0 doğruluk/hata kapanışı, `v0.11.0` CLI/JSON/sıfır-shell,
`v0.12.0` Konsept C TUI + birleşik kısayol yenilemesi, `v0.13.0` iki panelli
Snapshot Editor, AURA okuma/doğrulama, Fan Eğrisi altyapısı + Kayıtlı Fan
Eğrileri, dürüst PPT/revert + `Boot snapshot` ve `v0.14.0` paketleme tamamlandı):

- **`v0.14.0` (Paketleme, `udev` Ayrıcalık Azaltma & CLI/Masaüstü Artefaktları) — KESİLDİ:**
  `Makefile` (`PREFIX`/`DESTDIR`, `LDFLAGS`, `STRIP=0`, `uninstall`, kök
  `VERSION`, `make check-version`), `man/ctron.1`, bash/zsh/fish tamamlamaları
  (`make comptest`), `contrib/` (`99-ctron.rules` + `ctron.tmpfiles.conf`,
  `ctron-ppt`, `sudoers.ctron.example`, `ctron.desktop`, `PKGBUILD`),
  `CONTRIBUTING.md` + donanım raporu şablonu. Kalan saha kalemleri aşağıda
  ("Açık saha" 8–10).
- **`v0.15.0` (Offline Test Kalkanı, CI/CD & Modüler Temizlik):**
  `CTRON_SYSFS_ROOT` sahte `/sys`+`/proc` testleri +
  `scripts/tui_smoke.py --offline`, `.github/workflows/ci.yml`
  (`gcc`+`clang`, `HARDEN=1`, UBSan/LSan, `clang --analyze`, `shellcheck`,
  `make check-version`, `make check-help`, `make comptest`, offline smoke), `src/hw.c` ve UI
  modül bölünmesi + gizli `static` önbellek temizliği.

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
6. **Aday mikro sensör ekleri (karar kullanıcıda, kod değişikliği yok):**
   - PD port-başına sözleşme detayı (V/A + `USB_TYPES` + online; şimdi yalnız
     toplam mW var).
   - `TIME_TO_EMPTY_NOW` / `TIME_TO_FULL_NOW` — pil bölümüne kalan süre satırı.
   - NVIDIA `power.max_limit` / `min_limit` — POWER GPU satırına tavan bağlamı.
7. **Aday snapshot alanları (karar kullanıcıda, kod değişikliği yok):**
   - `nv-boost` / `nv-temp` komut tablosunda var, POWER'dan bilinçli
     çıkarıldılar (tek "GPU clock limit" satırı) — snapshot'a girsin mi?
   - `fan = stock|silent|cool|full` hazır ayarları (eğri satırları zaten
     aynı bilgiyi taşıyor).
   - `battery-oneshot` bir eylem, durum değil — snapshot'a girmez.
8. **Ayrıcalık azaltma dosyalarının gerçek açılış doğrulaması (root gerekir):**
   `contrib/99-ctron.rules` + `ctron.tmpfiles.conf` köksüz doğrulandı
   (`udevadm verify`, canlı ağaca karşı `--dry-run`, sahte kökte gerçek
   `systemd-tmpfiles` ve kuralın yaptığı `--prefix=` çağrıları, `find`
   ifadesinin 34 fan dosyasına tam isabeti) — ama udev'in `RUN`'ı
   çalıştırması ile bir saniyelik zamanlayıcının tetiklenmesi gerçek bir
   açılışta görülmedi. Kurulumdan sonra reboot ve `SECURITY.md` 4.6'daki
   `ls -l` satırları.
9. **`contrib/PKGBUILD` ile tag kaynağından derleme:** yerel `git+file://`
   kaynağıyla `makepkg` uçtan uca geçti (0 uyarı, `check()` birim testleri,
   paket içerikleri kaynakla bayt bayt aynı); `git+https://…#tag=v0.14.0`
   kaynağı yalnız tag push edildikten sonra denenebilir. `namcap` kurulu
   değil — paket lint edilmedi.
10. **Nix tarafı (Arcioth):** `flake.nix` bu turda yalnız yorumla değişti;
     `version` satırı artık `make check-version`'da doğrulanıyor (VERSION
     dosyasını doğrudan okuyan biçim, `nix build` doğrulanmadan denenmedi).
     Aday: tamamlamaların flake'e bağlanması (`installShellFiles`) ve 4.6'nın
     Nix karşılığı (`systemd.tmpfiles.rules` + `services.udev.extraRules`).
11. **GitHub:** `hardware` etiketi (donanım raporu şablonu onu uyguluyor)
    oluşturulmalı; Release sayfaları açılmadı (tag'ler origin'de, `gh` kurulu
    değil).

## Kodu doğrulanmış açık bulgular (2026-11-26)

Bunlar 11-23 ikinci taramasından ya da v0.14 turunda koda karşı doğrulandı;
hepsi **açık**:

- **`--dry-run` yıkıcı alt komutlarda yok sayılıyor:** `snapshot delete`,
  `fancurve delete`, `mode add` ve `mode delete` `dry_run`'a bakmadan uygular
  (`src/main.c`: `cmd_profile`, `cmd_fancurve`, `cmd_mode`). Yani
  `ctron snapshot delete x --dry-run` gerçekten siler. Tamamlamalar bu yüzden
  `--dry-run`'ı yalnız `apply`'da öneriyor. Öneri: `--dry-run` iken bu alt
  komutlar "would delete …" yazsın ve dokunmasın.
- **`--ppt on` kayıtlı değer yoksa sessizce hiçbir şey yapmıyor**
  (`ctrl_ppt_restore`: kayıt yoksa olay günlüğüne yazıp `0` döner, CLI'ya
  hiçbir şey basılmaz).
- **`--tui` terminalsiz çıkış kodu 1**, argsız terminalsiz başlatma ise 2
  (sözleşme "2 = kullanım hatası" diyor).
- **Canlı resize takip edilmiyor** (`NCOPTION_NO_WINCH_SIGHANDLER`, SIGWINCH
  işleyici yok → `NCKEY_RESIZE` dalı ölü) ve **stdin kapanınca ana döngü
  boş dönebiliyor** (`src/ui/ui.c`, `key == -1 → continue`; 11-23'te ölçüldü,
  kod aynı) — `REVIEW.md` 2.4 / v0.15.0 kapsamı.

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
- udev `GROUP=`/`MODE=` yalnız `/dev` düğümüne uygulanır, sysfs öznitelikleri
  `root:root` kalır (systemd 262'de ölçüldü: `/dev/rtc0` root:clock 0660 ↔
  `/sys/class/rtc/rtc0/*` root:root). Sysfs izni = `RUN` ile `chgrp`/`chmod`
  ya da tmpfiles `z`. FA608PP'te `systemd-tmpfiles-setup` 2.2 sn'de biter,
  `asus_wmi` 5.1 sn'de başlar → ASUS düğümleri udev olayını ister; sürücü
  bazı dosyaları "add" olayından *sonra* oluşturur (asus-armoury
  öznitelikleri, ASUS pil eklentisinin `charge_control_end_threshold`'u) →
  gecikmeli ikinci geçiş (`systemd-run --on-active`, `RUN`'ı bloklamaz).
- Tamamlamalar `ctron` çalıştırmaz: alt komutlar `hw_init` yapar (nvidia-smi
  sorgusu, dGPU'yu uyandırabilir) ve ilk çalıştırmada dosya oluşturur
  (`modes_load` → `modes_write_defaults`, `profile_list` → `ut_mkdir_p`).
  İsimler doğrudan yapılandırma dizininden okunur.
- Tamamlama sözdizimi tuzakları: fish'te açıklama ilk boşluğa kadar sürer
  (`-a ad -d 'açıklama'` her adaya ayrı satır); zsh `_arguments`'te numaralı
  konum spec'i baştaki `:` olmadan yazılır, tekrar edebilen seçenek `*`,
  `--seçenek=değer` biçimi için `=`, "tanınıp önerilmeyen" seçenek için `!`
  öneki; zsh tamamlaması yalnız pty + ZLE widget'ıyla sınanabilir
  (`scripts/completion_test.py`).
- `make CFLAGS=` komut satırı `+=`'ı siler → bayrakları ORTAM olarak ver;
  temizleyici derleme: `CFLAGS="-O1 -g -fsanitize=undefined,leak …"
  LDFLAGS="-fsanitize=undefined,leak" make test`.
- `--doctor` çıktısı yapılandırma yolunu basar (kullanıcı adı dahil) —
  donanım raporu şablonu bunu hatırlatıyor.
- Sandbox'ta zsh: `$a` kelime bölmez (`${=a}`), `===` ayracı "no matches
  found" verir; sınama düzeneklerini bash betiği olarak yaz.

## Hızlı komutlar

```bash
make && make test && ./build/ctron --doctor && ./build/ctron --status
make tuitest                     # scripts/tui_smoke.py (pty responder)
make comptest                    # bash/zsh/fish tamamlamaları
make check-version               # sürüm kopyaları eşleşiyor mu
make check-help                  # README bayrak bloğu == ctron --help
make install                     # ~/.local/bin/ctron (+ctron-toggle), man, tamamlamalar
./build/ctron                    # TUI (argsız); `,` = tercihler, Esc = geri/iptal
sudo ~/.local/bin/ctron --freq 5386   # frekans tavanını geri aç (gerekirse)
```
