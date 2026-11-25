# ctron — Profesyonelleşme Eleştirisi ve Yol Haritası

*Tarih: 2026-11-20 · Doğrulandı ve güncellendi: 2026-11-23 · İncelenen taban: `v0.9.0` (`8e3d4b2`) · Tüm kaynak (`src/`, `tests/`, `scripts/`, `Makefile`, `flake.nix`), dokümanlar ve canlı salt-okunur `--status`/sysfs çıktıları tek tek kontrol edilerek hazırlanmıştır.*

---

## 0. Genel Değerlendirme

`ctron`, bugünkü haliyle sıradan bir hobi projesi değil; mühendislik temeli şaşırtıcı derecede sağlam bir sistem aracı:

- **Korunması gereken güçlü yönler:** Tek C11 ikilisi ve tek bağımlılık (`notcurses`), sıfır uyarı derleme politikası, poll döngüsünden yazma yasağı, "bilinmiyorsa `--`, asla uydurma" dürüstlük kültürü, yaz-sonrası geri-okuma doğrulaması, atomik config yazımı (`ut_atomic_*`), tam-satır okuyucu (`ut_fgets_full`), `LC_NUMERIC="C"` disiplini, worker sırasında donmuş-kopya (`s_hw_frozen`) çizimi ve gerçek pty smoke testleri (`scripts/tui_smoke.py`).

**Peki "çok iyi kişisel araç" ile "profesyonel açık kaynak Linux projesi" (`btop`, `htop`, `nvtop`, `asusctl` ligi) arasındaki fark nerede?**

Dört eksende:
1. **Kodda hâlâ yaşayan gerçek hatalar ve dürüstlük çatlakları** (denetimden kaçmış 11 somut bulgu — aşağıda satır satır kanıtlı).
2. **Mimari ve kod hijyeni borcu** (2000 satırlık god-file'lar, dağılmış `static` önbellekler, çizim döngüsünde disk/soket/heap I/O'su, kalan `popen`/shell noktaları, iki farklı kod stili).
3. **Ürün & UX eksikleri** (makinece okunabilir `--json` yokluğu, TUI'de log geçmişi okuyamama, klavyeyle ulaşılamayan SETTINGS mod listesi, imleçsiz metin girişi, eksik snapshot kapsamı).
4. **Dağıtım, CI/CD ve depo vitrini** (`PREFIX`/`DESTDIR` olmayan `Makefile`, CI yok, `man` sayfası / shell completion / hazır `contrib/` kuralları yok; depo doküman sadeleştirmesi 2026-11-23'te tamamlandı).

Aşağıdaki her madde doğrudan koddan doğrulanmıştır; `ctron`'un çekirdek kimliğine (tek C ikilisi, `notcurses`-only, sayısal telemetri, daemon'sız yapı) aykırı hiçbir öneri içermez.

---

## 1. Kesin Yapılması Gerekenler — Kodda Yakalanan Gerçek Hatalar (P0) — **[TAMAMLANDI: `v0.10.0`, 2026-11-23]**

*1.1–1.11'in tamamı (`8a70b77`, `9d5caf9`, `b349b6c`, `b1eabe3`) + 2.2'nin `cpufreq_write_all` direct-first adımı kapatıldı, `AUDIT.md` A1/C1/D5/E3/H4/K4 tekrar `[x]`'e çekildi. Aşağıdaki kayıtlar teknik referans olarak korunmaktadır:*

### 1.1. Per-core frekans limitleri `.ctr` snapshot'a yazılıyor ama geri yüklenirken sessizce çöpe gidiyor
- **Kanıt:** `src/profile.c:197` dışa aktarımda `=` olmadan `fprintf(f, "freq core %d %d\n", id, ...)` yazıyor. Ancak `src/profile.c:245` (`profile_import`) içinde `char *eq = strchr(s, '='); if (!eq) continue;` var — `=` içermeyen her satır koşulsuz atlanıyor.
- **İkinci katman:** `src/profile.c:232` `freq_replay[4][64]` en fazla 4 satır tutabiliyor; 16–32 iş parçacıklı makinede per-core limitler re-assert aşamasında kesilir.
- **Dikkat (regresyon tuzağı):** `profile_export` `freq = core %d %d\n` biçimine çevrilirken [`profile_meta_parse`](src/profile.c#L493-L522) da birlikte güncellenmelidir. Mevcut kod satır 493'te yalnız `strncasecmp(s, "freq core", 9)` atlıyor; `freq = core 0 2400` gelirse `key == "freq"` dalına düşüp `m->freq = atoi("core 0 2400")` → `0` yazar ve snapshot `freq` diff'ini bozar.
- **Çözüm:** Export `freq = core %d %d\n` yazmalı (import eski `freq core ...` biçimini de geriye uyumlu tanıyabilir), `profile_meta_parse` içinde `key == "freq"` iken `core`/`ccd` değerli satırlar atlanmalı, `freq_replay` tavanı `HW_CPU_MAX + 1` olmalı ve `test_core.c`'ye per-core export→import + `profile_meta_parse` round-trip testi eklenmeli.

### 1.2. Wi-Fi `dBm` gösteriminde ters koşul + `-1` sentinel çakışması
- **Kanıt:** `src/hw.c:776,1060` bilinmeyen durumu `hw->wifi_level_dbm = -1` ile kodluyor; geçerli dBm değerleri de negatif (`-57 dBm`, `src/hw.c:796`).
  - `src/ui/panel_telem_full.c:1022` (`detail_net`): `if (hw->wifi_level_dbm < 0)` dalı dBm'yi **yazmıyor**, `else` dalı yazıyor → detay sayfasında dBm hiçbir zaman görünmüyor.
  - `src/ui/panel_telem_full.c:669` (`sec_net`) ve `src/main.c:162` (`--status`): `if (hw->wifi_level_dbm < 0)` kontrolü, değer bilinmiyorken (`-1`) de doğru dönüp `(-1 dBm)` basma riski taşıyor.
- **Çözüm:** Bilinmeyen sentinel'i `0` (geçerli Wi-Fi dBm değeri asla `>= 0` değildir; `hw_init` `memset` ile de doğal uyumlu) yap, üç gösterim noktasını tek kurala (`< 0`) bağla, `detail_net` koşulunu düzelt ve `tests/test_core.c:1230,1234` + `src/hw.h:38,215` beklentilerini güncelle.

### 1.3. Çok satırlı sysfs/procfs dosyalarının tek satır (`ut_read_file`) okunması
- **Kanıt:** `ut_read_file()` (`src/util.c:24-39`) yalnızca ilk satırı okur.
  - `src/hw.c:1158`: `pp_dpm_sclk` dosyası `ut_read_file()` ile okunup `hw_parse_pp_dpm()`'ye veriliyor. Dosya çok satırlıdır (`0: 400Mhz\n1: 600Mhz *\n2: 2200Mhz\n`). **Canlı kanıt (2026-11-23, FA608PP):** `./build/ctron --status` çıktısında `iGPU busy : 58 %` basılıp `· sclk DPM 2/3 @ 600 MHz` satırı tamamen düşüyor; çünkü aktif `*` işareti 2. satırda.
  - Ek not: `src/hw.c:607` `sscanf(line, "%d: %dMHz", ...)` büyük `H` bekliyor, gerçek amdgpu sysfs çıktısı küçük `h` (`400Mhz`) basıyor — `sscanf` iki `%d`'yi doldurduğu için şans eseri `2` dönüyor; format `Mhz`/`MHz` bağımsız temizlenmeli.
  - `src/hw.c:1107`: `/proc/pressure/{cpu,memory,io}` de `ut_read_file()` ile okunuyor; `hw_parse_psi()` (`src/hw.c:868`) ve birim testi (`tests/test_core.c:1259`) çok satırlı (`full` üstte, `some` altta) varsayar — şu an Linux'un `some`'ı ilk satıra koyması sayesinde tesadüfen çalışıyor.
- **Çözüm:** Her iki yerde `ut_read_file_all()` kullan; `hw_parse_pp_dpm` birim eşleşmesini büyük/küçük harf bağımsız yap.

### 1.4. "Dürüst Okuma" (Kural 3) çatlakları: durmuş fan (`0 RPM`) ve ham `-1`/`0` çıktıları
- **Kanıt:**
  - `src/hw.c:1864-1865`: `hw->rpm_cpu = r1 > 0 ? r1 : -1; hw->rpm_gpu = r2 > 0 ? r2 : -1;` — Boşta duran fan (`0 RPM`) `-1` (`--`) olarak siliniyor. `HARDWARE.md` tam tersini (*"0 is honest — the EC stops the GPU fan at idle"*) söylüyor. `>= 0` olmalı (`telem_push`, `fmt_val`, `dash_if` zaten `0`'ı geçerli kabul ediyor).
  - `src/main.c:258` (`--watch`): `hw->cpu_temp` ve `hw->gpu_temp` için `dash_if` kullanılmamış → uyuyan dGPU'da `GPU -1°C` basıyor.
  - `src/ui/panel_telem_full.c:556,567,571,601-609,784-789,846-850,904-909`: `rpm_cpu`/`rpm_gpu`/`gpu_util`/`gpu_vram_used_mb`/`swap_*`/`gpu_mhz_max` ve kısmi `ppt_*` değerleri doğrudan `%d` ile yazdırılıyor → uyuyan/N/A dGPU'da `util -1%` / `-1/8192 MB`, durmuş/yok fanda `fan -1/-1 rpm`, NVIDIA yokken `clock max 0 MHz`, meminfo yokken `swap -1/-1 MB`, kısmi PPT'de `-1` görünüyor.
  - `src/hw.c:1490-1493` + `src/main.c:210-211`: okunamayan `nv_boost`/`nv_temp` `hw_init`'te `0` bırakılıyor ama `cmd_status` `dash_if` (`< 0`) kullanıyor → `--` yerine `0 W / 0 °C` basar.
- **Çözüm:** `rpm` okuması `>= 0`; `nv_boost`/`nv_temp` bilinmeyen sentinel'i `-1` (veya gösterimlerde `> 0`); tüm `--watch`, `--status` ve telemetri info satırları bilinmeyen kontrolünden geçerek `--` basmalı.

### 1.5. SETTINGS → CLI SHORTCUTS (`modes.ini`) klavyeyle kullanılamıyor
- **Kanıt:** `src/ui/panel_settings.c:173` başlıkta `"CLI SHORTCUTS (modes.ini) — Enter apply · n new · e edit · d del"` yazıyor. Ancak `panel_settings_key()` (`src/ui/panel_settings.c:304-348`) içinde:
  - `j`/`k` yalnızca üstteki 9 `SET_ROW_COUNT` ayarında dönüyor; `g_ui.set_mode_sel`'i değiştiren hiçbir tuş yok.
  - `Enter` üstteki ayar satırını değiştiriyor (`row_apply`); klavyeden `mode_apply_idx()` çağıran hiçbir yol yok.
  - Ayrıca MODE EDITOR'da (`src/ui/panel_settings.c:300`) yön tuşuna basmak editörü kapatıyor (`md_field = -1`), `g_ui.md_field == 2` (`satır 290`) ve `ACT_SET_MODE_NEW/EDIT/DEL` (`satır 35-37`) erişilemeyen ölü kod.
- **Çözüm:** SETTINGS içinde `Tab` (veya tek birleşik liste yürüyüşü: 9 ayar + modlar) ile klavye odağını `CLI SHORTCUTS` listesine taşı; `Enter` seçili modu uygulasın; MODE EDITOR'da yön tuşları editörü düşürmesin (bölüm 3.4 ile imleç hareket ettirsin); ölü kodları temizle.

### 1.6. `ctron --tui` singleton kilidini atlıyor + `/tmp` pidfile'ında UID yok
- **Kanıt:**
  - `src/main.c:691-698`: `--tui` / `-t` bayrağı (ayrıca `--setup` `satır 726` gibi `--help`'te belgesiz) `ut_singleton_enter("ctron")` çağırmıyor — ikinci TUI penceresine izin veriyor.
  - `src/util.c:461`: `XDG_RUNTIME_DIR` boşsa `/tmp/ctron-tui.pid` kullanıyor (UID yok). Çok kullanıcılı sistemde kullanıcı A'nın pidfile'ı kullanıcı B ile çakışır.
- **Çözüm:** `--tui` de singleton kapısından geçsin (ve `--tui`/`--setup` `--help`'te ya belgelensin ya sadeleştirilsin); `/tmp` fallback yolu `ctron-tui-<uid>.pid` olsun (`SECURITY.md §2.3` ile eş güncellensin).

### 1.7. Önbellek zamanlayıcılarında duvar saati (`time(NULL)`) kullanımı
- **Kanıt:** `src/hw.c:418` (`gpu_query_cached` 2 s önbelleği), `src/ui/panel_workspace.c:1106` (`draw_light` 1 s `db_up` önbelleği) ve `src/ui/editor_daeboard.c:767` (`db_up` önbelleği) `time(NULL)` kullanıyor. NTP saat geri adımı bu önbellekleri dondurur (`AUDIT.md` K4 bu nedenle `[~]` olarak güncellendi).
- **Çözüm:** Tek `ut_mono_ms()` yardımcısına (`src/util.c`) geçir (7+ noktadaki kopya `CLOCK_MONOTONIC` ms hesaplarını da tek yere indirir — bkz. 2.3).

### 1.8. `editor_daeboard.c`: `cap_fd` sızıntısı, sabit klavye adı, eksik `mkdir_p` ve klavye/ölü-kod hataları
- **Kanıt:**
  - `src/ui/editor_daeboard.c:39,101`: `open_keyboard()` ile açılan `/dev/input/event*` dosya tanımlayıcısı (`cap_fd`) hiçbir zaman `close()` edilmiyor (overlay ESC ile ya da `src/ui/ui.c:509`'da dışarı tıklanarak kapanınca, veya `keypick` bitince açık kalıyor).
  - `src/ui/editor_daeboard.c:84`: `"AT Translated Set 2 keyboard"` adı sabit kodlu (farklı/harici klavyelerde çalışmaz).
  - `src/ui/editor_daeboard.c:262`: `save_file()`, `ut_atomic_begin()` öncesi config dizinini `ut_mkdir_p()` ile oluşturmuyor (temiz kurulumda kayıt başarısız olur).
  - `src/ui/editor_daeboard.c:195`: `fgets()` kullanıyor (`ut_fgets_full()` kuralının dışında kalmış).
  - `src/ui/editor_daeboard.c:1026-1033` vs `1074,1080`: `NCKEY_UP`/`NCKEY_DOWN` yukarıda koşulsuz `move_step()` çağırıp `return` ettiği için `focus == 0` (tuş listesi) dalındaki ok tuşları ölü kod; `1121-1124` (`'x'/'X'`) de `1038` yüzünden ölü kod; `1061-1071` `Tab` tuşu `focus == 2` (`ctron` komut satırı) odağına hiç geçemiyor.
- **Çözüm:** Ortak `editor_daeboard_close()` ekle (`cap_fd` kapatma: `keypick` bitişi, ESC, dış-tık `ui.c:509`, TUI çıkışı); `save_file` önce `ut_mkdir_p` çağırsın; `load_file` `ut_fgets_full` kullansın; `EV_KEY` yetenekli klavye keşfi ve tuş-odak/ok-tuşu/Tab akışını düzelt.

### 1.9. Çizim döngüsünde (her karede) gereksiz disk, heap ve bloklayıcı soket I/O'su
- **Kanıt:**
  - `src/ui/panel_profiles.c:113-117`: `ensure_meta()` seçili snapshot'ı önbelleğe alıyor ama hemen altındaki `profile_summary()` dosyayı diskten **her karede** (saniyede 4 kez) baştan açıp ayrıştırıyor.
  - `src/ui/editor_fan.c:647`: `editor_fan_presets_draw()` modal açıkken **her karede** `fan_preset_get()` çağırıyor (8 KB `/etc/asusd/fan_curves.ron` veya 16 sysfs düğümü) ve içindeki `fan_ron_parse()` (`src/fan.c:354`) her karede `malloc`/`free` yapıyor (`AUDIT.md` A1/G1 varsayımını modal açıkken bozar).
  - `src/ui/panel_workspace.c:1109,1112` ve `src/ui/editor_daeboard.c:772`: `db_up()` (2 s timeout'lu bloklayıcı `AF_UNIX` çağrısı) doğrudan çizim fonksiyonundan saniyede bir çağrılıyor (ayrıca `db_install_note()` her karede `access("/etc/NIXOS")` yapıyor); takılan bir daemon TUI'yi saniyede bir 2 saniye dondurur.
- **Çözüm:** `profile_summary` önbellekteki `profile_meta_t`'den biçimlendirsin (`profile_meta_summary`); fan presets önizlemesi yalnızca seçim değiştiğinde / modal açıldığında okunsun; `db_up()` poll/açılış zamanına taşınsın veya kısa non-blocking/100 ms probe olsun; `is_nixos` bir kez önbelleklensin.

### 1.10. Diğer küçük doğruluk pürüzleri
- **`hw_init` başlangıç sentinel'leri (`src/hw.c:1406-1423`):** `load5/15`, `psi_*`, `bat_health`, `bat_v_mv`, `bat_a_ma`, `igpu_busy`, `igpu_dpm*`, `igpu_vddgfx_mv`, `igpu_vddnb_mv`, `cpu_temp_ccd*`, `ram_thresh_max/crit`, `nvme_sensor_max[]`, `wifi_link_pct`, `fs_avail_mb`, `net_rx_tot/tx_tot` alanları `memset` ile `0` kalıyor (`-1` olmalı); ilk `hw_refresh_fast` öncesi okunursa yanıltıcı `0` verir.
- **Başarısız CLI çağrısında `settings_save` (`src/main.c:865`):** `ctron --hz bogus` gibi hatalı bir komut bile çıkışta `settings_save(&hw)` çalıştırıp `~/.config/ctron/` oluşturuyor/yazıyor. Yalnız `rc == 0` ve kalıcı durum değiştiyse kaydedilmeli.
- **Snapshot save/rename sonrası seçim kayması (`src/ui/panel_profiles.c:158,173`):** Liste araması sanitize edilmemiş girdiyle yapılıyor; boşluklu isim (`"my game"`) `"my-game"` diye kaydedilince seçim üstüne atlamıyor.
- **Per-core frekans doğrulama toleransı (`src/control.c:293`):** All-cores (`satır 230`) `±2 MHz` tolerans tanırken per-core `back == mhz` tam eşitlik istiyor; kHz yuvarlamasında sahte `VERIFY FAILED` üretebilir.

### 1.11. Ayarlar → `"no sudo"` (`PREF_NO_SUDO`) tercihi 4 ayrıcalıklı yolda atlanıyor
- **Kanıt:** `ut_sudo_fallback_allowed()` (`src/settings.c:13-16`) yalnızca `ut_priv_write()` (`src/util.c:229`) içinde kontrol ediliyor. Aşağıdaki 4 yol `write_pref == PREF_NO_SUDO` seçili olsa bile doğrudan `sudo -n` çağırıyor:
  - `src/control.c:103` (`cpufreq_write_all` — EPP ve all-cores freq)
  - `src/control.c:486-503` (`ppt_write` — `ctron-ppt` / `ryzenadj`)
  - `src/control.c:813,838` (`ctrl_gpu_clock_lock` / `ctrl_gpu_clock_reset`)
  - `src/daeboard.c:215,221,261,267` (`db_start` / `db_install`)
- **Çözüm:** Her dört yolda `sudo -n` çağrısı öncesi `ut_sudo_fallback_allowed()` kontrolü yap ve kapalıysa dürüst log (`disabled by write_pref=no sudo`) ile temiz hata dön (`cpufreq_write_all` önce doğrudan `ut_write_file()` denemeli — bkz. 2.2).

---

## 2. Mimari, Güvenlik ve Kod Kalitesi (P1)

Kodun uzun ömürlü, başkalarının katkı verebileceği ve güvenle denetlenebileceği bir seviyeye çıkması için:

### 2.1. "Sıfır `/bin/sh` ve sıfır `popen`" hedefini tamamla — **[TAMAMLANDI: `v0.11.0`, `143d2f7`]**
- **Mevcut durum:** Ayrıcalıklı yazmalar zaten shell'siz (`ut_write_via_argv` / `ut_exec_argv`), ama `src/control.c` (**13** `ut_exec` noktası) ve `src/hw.c` (**4** nokta: `ut_exec` ×1, `ut_exec_raw` ×3) hâlâ `asusctl` ve `busctl` için `popen("timeout -k1 3 %s ...")` kullanıyor; `ut_have_cmd()` (`src/util.c:248`) ve `nvidia_smi_bin()` (`src/control.c:785`) ise `popen("command -v ...")` kullanıyor.
- **Yapılacak:**
  - `PATH` üzerinde `access(candidate, X_OK)` gezen ~25 satırlık saf C `ut_find_in_path()` yaz → hem `ut_have_cmd` hem `nvidia_smi_bin` sıfır fork/shell ile mikrosaniyede çalışır.
  - Kalan tüm `asusctl` / `busctl` çağrılarını `ut_exec_argv*`'ye geçir, `ut_exec` / `ut_exec_raw` fonksiyonlarını tamamen kaldır.
  - **Kazanım:** İkilide tek bir `/bin/sh` veya `popen` çağrısı kalmaz — `SECURITY.md` ve güvenlik denetçileri için çok net bir garanti.

### 2.2. `udev` + `tmpfiles.d` (`contrib/`) ile `sudo` yüzeyini %90 küçült + FAN yazımını hızlandır
- **Mevcut durum:** `ut_priv_write()` doğrudan `ut_write_file()` → `sudo -n tee` sırasını izliyor, ancak `cpufreq_write_all()` (`src/control.c:78-118`) **önce `sudo -n tee` deniyor** (doğrudan yazım yalnız sudo başarısız olursa çalışıyor). Ayrıca sysfs düğümleri root sahipli olduğundan kullanıcı geniş `sudoers` kuralları yazmak zorunda kalıyor ve bir fan eğrisi yazımı `34` ayrı `sudo -n tee` fork'u (~3–4 saniye donma!) üretiyor.
- **Yapılacak (kod + contrib birlikte):**
  1. **Kod:** `cpufreq_write_all()` önce tüm `cpu_ids` üzerinde doğrudan `ut_write_file()` denesin; yalnız izin reddedilirse (ve `ut_sudo_fallback_allowed()` ise) toplu `sudo -n tee`'ye düşsün.
  2. **`contrib/99-ctron.rules` (`udev`):** Gerçek udev cihazları için gruba (`ctron` veya `wheel`) yazma izni: `hwmon` (`asus_custom_fan_curve`), `power_supply` (`charge_control_end_threshold`), `leds` (`asus::kbd_backlight`), `platform/asus-nb-wmi` (`panel_od`).
  3. **`contrib/ctron.tmpfiles.conf` (`systemd-tmpfiles.d`):** Saf udev olayı üretmeyen veya sürücü yükleme yarışına giren düğümler için `z` satırları: `/sys/firmware/acpi/platform_profile`, `/sys/devices/system/cpu/cpufreq/boost`, `/sys/devices/system/cpu/cpu*/cpufreq/{scaling_max_freq,energy_performance_preference}` (ek olarak yeni kernel 6.13+ `/sys/class/platform-profile/` sınıfı opsiyonel değerlendirilebilir).
  - **Kazanım:** Yazmaların %90'ı root olmadan doğrudan `fopen`/`fwrite` ile **<5 ms**'de biter (34× `sudo` maliyeti yok olur!); `sudoers` yalnızca `ctron-ppt` ve `nvidia-smi -lgc/-rgc` için gerekir. `SECURITY.md` Faz 3 Polkit'e gerek kalmadan neredeyse aynı ayrıcalık ayrımını sıfır daemon'la sağlar.

### 2.3. Büyük dosyaları böl, modül içi gizli `static` durumları temizle
- **Dosya boyutları:** `src/hw.c` (2086 satır), `src/ui/panel_workspace.c` (1439), `src/ui/panel_telem_full.c` (1419), `src/ui/editor_daeboard.c` (1272), `src/control.c` (1254), `src/ui/ui.c` (1203). Bu boyutlar yeni katkı vereni korkutur ve değişiklik etkisini izlemeyi zorlaştırır.
  - `src/hw.c` → `hw_parse.c` (saf, testli parser'lar), `hw_sensors.c` (fast poll / extras), `hw_asusd.c` (D-Bus/RON), `hw.c` (init/live/topoloji).
  - `src/ui/panel_workspace.c` → `panel_power.c`, `panel_light.c`, `panel_help.c`, `panel_workspace.c` (yalnız sekme yönlendirici).
- **Gizli `static` önbellekler:** `src/hw.c` içinde yol önbelleğinin yarısı `hw->paths`'te (sıfırlanabilir), yarısı fonksiyon-içi `static`'lerde (`s_dev`, `s_ccd`, `gpu_runtime_status.path`, `gpu_query_cached` statikleri, `s_busy`/`s_total`, `s_sweep_last_full_ms`). Hepsi `hw_state_t` (veya `hw_cache_t`) içine alınmalı; suspend/resume veya yeni `hw_init()` çağrısında temiz sıfırlanmalı.
- **Kod tekrarları & ölü/bayat artıklar:**
  - `ut_mono_ms()` → **7+ noktadaki** kopya `CLOCK_MONOTONIC` ms hesabını tek yere indir (`hw.c:1572`, `ui.c:594`, `panel_workspace.c:83`, `panel_telem_full.c:1097`, `editor_daeboard.c:1177`, `panel_telemetry.c:61`, `main.c:348`, `daeboard.c:71-78`).
  - `utf_cells()` (`panel_telem_full.c:244`) → zaten var olan `ut_cells()` (`util.c:599`) kullanılmalı.
  - `ut_log_get(int idx, char *out, size_t n)` → fonksiyon-içi `static char out[]` yerine çağıranın tamponuna kopyalasın (re-entrant API).
  - Ölü/bayat artıklar: `ui_internal.h:344` `APPLY_MODESET` (kullanılmıyor); `src/telem.c:165` `telem_spark()` (grafikler v0.9.0'da kalktığından beri TUI'de çağrılmıyor, yalnız testte); `panel_settings.c:35-37` `ACT_SET_MODE_NEW/EDIT/DEL`; `panel_telem_full.c:237` (silinmiş wash-chart yorumu), `:521` (mini-chart yorumu), `:881` (kaldırılmış `gpu_metrics gfx mW` yorumu); `panel_workspace.c:291,711` tek satıra yapışmış deyimler.
- **Tek kod biçimi:** `daeboard.c` ve `editor_daeboard.c` tab girintili, kalan tüm `src/` 4 boşluklu. Kök dizine `.clang-format` ve `.editorconfig` ekleyip tüm ağacı tek stile hizala.

### 2.4. Test mimarisini CI-uyumlu ve kapsamlı hale getir
- **Mevcut boşluklar:** `tests/test_core.c` (2014 satır) saf parser ve dosya formatlarını harika test ediyor, ama:
  - `cmds.c` komut yönlendiricisi, `control.c`'nin sysfs yazma/doğrulama zinciri ve UI'ın saf mantığı (`pw_write_mask`, `focus_move`, `ui_layout`, `pw_view_clamp`) birim testlerinde yok.
  - `scripts/tui_smoke.py` bazı akışlarda (`flow_mode_drift`, `flow_fan_buttons`) gerçek donanıma yazıyor — CI konteynerinde veya başka bir geliştiricinin makinesinde koşturulamaz. Ayrıca `flow_telem_full` (`scripts/tui_smoke.py:585-731`) `finally` bloğunda `CTRON_CONFIG`'i geri yüklemiyor ve `tmpcfg`'yi silmiyor (ortam sızıntısı).
- **Yapılacak:**
  - `CTRON_SYSFS_ROOT` (test-only ortam değişkeni veya derleme bayrağı) ile sahte `/sys` + `/proc` ağacı üzerinde `hw_init` / `hw_refresh_fast` / `ctrl_*` birim testleri.
  - `tui_smoke.py`'ye `--offline` / `--readonly` bayrağı + `flow_telem_full` `finally` temizliği (CI'da sıfır donanım bağımlılığıyla tüm TUI navigasyon/overlay/resize akışlarını koşabilsin).
  - `tests/test_core.c`'yi konu bazlı küçük test dosyalarına (`test_fan.c`, `test_hw_parse.c`, `test_config.c`, `test_cmds.c`) veya okunaklı bölümlere ayır.

---

## 3. Ürün ve UX Profesyonelliği (P1–P2)

*3.1, 3.2, 3.3, 3.4 ve 3.5 (`e77622f` + `2241130`) **`v0.11.0`** içinde tamamlandı (3.5'teki FAN `'w'` senkron kararı `AUDIT.md E5` uyarınca korundu; `v0.13.0` udev kuralları `sudo` gecikmesini doğrudan ortadan kaldırıyor). **3.6** ise sıradaki **`v0.12.0`** sürümünün ana hedefidir.*

### 3.1. Makinece okunabilir CLI (`--json`), `--dry-run`, `snapshot diff` ve çıkış kodu sözleşmesi
- **`ctron --status --json` ve `ctron --doctor --json`:** Modern Linux ekosisteminde (`jq`, Waybar custom module, Polybar, Eww, AGS, shell betikleri, monitoring agent'ları) altın standart. Waybar desteğini doğrudan kodlamadan Waybar kullanıcılarına temiz entegrasyon sunar. Sıfır ekstra bağımlılıkla ~120 satır temiz JSON serializer yeterli.
- **`ctron snapshot diff <name>`:** TUI'deki `profile_meta_diff()` zaten saf C fonksiyonu — bunu CLI'ya açmak ~15 satır.
- **`--dry-run` (veya `--mode <name> --dry-run`):** Hangi adımların hangi değerle çalıştırılacağını donanıma dokunmadan listeler (otomasyon yazarken güven verir).
- **Belirgin çıkış kodları:** `--help` ve `man` sayfasında belgelenmiş sözleşme: `0` başarı, `1` çalışma zamanı/doğrulama hatası, `2` argüman/kullanım hatası, `3` yetki/sudo eksikliği (`EACCES`).

### 3.2. TUI içinde Log Görüntüleyici (24 satırlık ring buffer görünür olsun)
- **Sorun:** `ut_log()` 24 satır saklıyor (`UT_LOG_ENTRIES`), ancak TUI'de yalnızca en son **1 satır** görünüyor. Kısmi hata olduğunda `ctron` `"N step(s) failed (see log)"` diyor ama TUI kullanıcısı o logun geri kalanını okuyamıyor!
- **Çözüm:** `L` (veya LIVE panelinde `l`/buton) ile açılan basit, salt-okunur bir **LOG** overlay'i (son 24 işlem zaman damgalı, `FAILED` satırları kırmızı vurgulu).

### 3.3. `.ctr` snapshot kapsamının dürüst ve eksiksiz olması
- **Sorun:** Bugün `profile_export()` yalnızca `profile`, `epp`, `freq`, `hz`, `battery`, `fan_*`, `kbd` kaydediyor; POWER ekranındaki `ppt`, `gpu-clock`, `panel-od`, `cpu-boost` snapshot'a girmiyor (ve `freq core` import bozuk — bkz. 1.1).
- **Çözüm:** En azından `panel-od`, `cpu-boost`, `gpu-clock` (yalnız `gpu_clock_lock > 0` ise) ve `ppt` (yalnız okunabilen/bilinen `> 0` ise, stale `-1` asla yazılmadan) alanlarını snapshot'a dahil et; `profile_capture_line()` ve `profile_meta_diff()` ile birebir örtüşsün.

### 3.4. Gerçek imleçli metin girişi (`tinput_t`)
- **Sorun:** `tinput_t` yalnızca sona karakter ekleyip backspace ile silebiliyor (`src/ui/ui.c:308-323`). 200 karakterlik `modes.ini` adımlarında baştaki bir harfi düzeltmek için tüm satırı silmek gerekiyor.
- **Çözüm:** `size_t cursor` alanı ekle; `Left`/`Right`, `Home`/`End`, `Delete`, `Ctrl+U` (satırı temizle), `Ctrl+W` (son kelimeyi sil) desteği ekle (~40 satır, tüm TUI input'ları anında modernleşir).

### 3.5. Yıkıcı eylemlerde güvenli onay & FAN yazımının tutarlılığı
- **Silme onayı:** Snapshot silme (`d`, `panel_profiles.c:247`) ve mode silme (`d`, `panel_settings.c:334`) tek tuşta geri dönüşsüz dosya/kayıt siliyor. İlk basışta `"press d again to delete '<name>'"` (ikinci `d` siler, başka herhangi bir tuş iptal eder) yaklaşımı TUI akışını bozmadan kazaları önler. *(Not: `scripts/tui_smoke.py:782` `flow_snapshots` tek `d` bekliyor — aynı commit'te güncellenmeli.)*
- **FAN `'w'` akıcılığı (bilinçli karar değişimi):** `AUDIT.md` E5'te FAN `'w'` şu an bilinçli olarak senkron kayıtlı; ancak 2.2'deki `udev` kuralı kurulu olmayan makinelerde `ctrl_fan_write()` UI thread'ini 2–4 s donduruyor. `APPLY_FAN` türüyle arka plan işçisine (`ui_spawn_apply`) alınacaksa yalnızca FAN `'w'` değil `panel_controls.c:102-106` (CONTROLS Fan satırı) ve `editor_fan.c:365-368,468-471` (`o` toggle) de birlikte taşınmalı ve `AUDIT.md` E5 güncellenmeli.
- **Desteklenmeyen satırların dürüst etkileşimi:** `asusd_ac == -2` (asusd yok) iken `pw_nudge_row` yine de gizli staging değerini döndürüyor ama ekran `--` gösteriyor; bu satırlar destek yoksa etkileşimsiz (veya gri + kısa neden) olmalı.

### 3.6. Yeni TUI Tasarımı & Kısayol Mimarisi — **[TAMAMLANDI: `v0.12.0`, `7baf997` + `76f01dd`]**
Mevcut TUI fonksiyonel olarak çalışsa da görsel dil olarak tipik "AI üretimi neon dashboard" (AI slop) hissi veriyor ve kısayol haritası zamanla yamalanarak karmaşıklaşmış durumda:

- **Görsel & Yerleşim Sorunları (Koddan Tespitler):**
  1. **Zorla boyanan neon tema & opak arka plan (`src/ui/ui.c:40-48`):** `0x00E5FF` camgöbeği / `0x00FF66` neon yeşil / `0xFF3366` pembe vurgu renkleri ve tüm ekranı `0x050A0E` ile boyayan opak dolgu, kullanıcının terminal temasını ve şeffaflığını/bulanıklığını eziyor. Gerçek Linux TUI araçları (`htop`, `nvtop`, `lazygit`, `impala`) varsayılan terminal arka planına (`default bg`) saygı duyar ve ölçülü vurgu renkleri kullanır.
  2. **4 kutulu Bento sıkışıklığı, sahte GUI butonları ve işlev tekrarı:** Aynı ekranda 4 ağır çerçeve (`SNAPSHOTS`, `CONTROLS`, `WORKSPACE`, `LIVE`) + gölgeli çift çerçeve modal (`╔══╗`), her kutunun ilk satırında tekrar eden `j/k · h/l · Enter ...` açıklama kalabalığı ve terminal içinde `[ Apply ] [ Save ] [ Del ]` sahte buton şeritleri var. Üstelik `Profile`, `Fan`, `Kbd` hem `CONTROLS` kutusunda hem `POWER`/`FAN`/`LIGHT` görünümlerinde tekrarlanıyor; `Fan curve: open editor >` ve `Settings: open >` satırları gereksiz yönlendirme dolgusundan ibaret.
  3. **İkili yazma modeli kafa karışıklığı:** `CONTROLS` ve `LIGHT` üzerinde `Enter` donanıma anında yazarken, `POWER` üzerinde stage edip toplu yazıyor, `FAN` editöründe ise `Enter` hiçbir şey yazmayıp yalnızca `w` EC'ye yazıyor.
- **Kısayol / Tuş Ataması Sorunları:**
  1. **`1..8` bölünmüş kişiliği (`src/ui/ui.c:965-990`):** `1..4` panel odağını değiştirirken `5..8` birdenbire 3. panelin iç sekmesini değiştiriyor (`6` = Power, `2` = Controls — zihinsel haritası yok).
  2. **`ESC` tuşunun Ayarlar'ı açması (`src/ui/ui.c:958`):** Her terminal kullanıcısı `ESC` tuşundan "iptal / geri / kapat" bekler; ana ekranda `ESC`'in `SETTINGS` penceresini açması ters köşe yaratıyor.
  3. **Bağlama göre anlam değiştiren tek-harf tuşları:** `s` (genelde Settings, Snapshots'ta Save, Telemetry detayda pencere/since-reset), `t` (genelde Telemetry, Fan/Power/Corefreq'te değer yazma), `c` (Snapshots'ta not, Fan'da CPU, Power'da Core limits).
- **Yapılacak (`v0.12.0`):**
  - **Sade, karakterli UNIX TUI düzeni:** Üstte tek satır temiz başlık + görünüm sekmeleri + anlık özet (`CPU/GPU °C · RPM · W · Profil`), ortada seçili görünümün ferah tam çalışma alanı (4'e bölünmüş kutu karmaşası yerine net görünümler), en altta **tek birleşik durum + bağlamsal kısayol çubuğu** (`key` vurgulu, açıklaması sade; kutu içi yardım satırları ve sahte butonlar kaldırılır).
  - **Terminal-dostu tema sistemi:** Varsayılan olarak terminal arka planını koruyan (`default` bg + ANSI/ölçülü RGB vurgular) tema + isteğe bağlı yüksek kontrast profiller.
  - **Tek ve öngörülebilir kısayol sözlüğü (`keymap`):**
    - `1..N` ve `Tab` / `Shift+Tab` (`[` / `]`): doğrudan ana görünümler arasında geçiş (odak vs sekme ayrımı kalkar).
    - `ESC`: her yerde **yalnızca** geri / iptal / modal kapatma (asla menü açmaz; Ayarlar `,` tuşuna veya kendi sekmesine taşınır).
    - Tekil, çakışmasız eylem tuşları (örn. değer düzenleme için `e` veya `Enter`, donanıma uygulama için tutarlı tek tuş, tam ekran/detay telemetri ve log için sabit tuşlar) — tek bir merkezi `keymap` tablosundan okunur ve alt çubuk otomatik olarak oradan üretilir.

---

## 4. Dağıtım, Paketleme, CI/CD ve Depo Hijyeni (P1)

Bir projeyi GitHub'da dışarıdan bakan bir mühendisin gözünde "profesyonel" yapan katman burasıdır:

### 4.1. Standart Linux `Makefile` kurulumu (`PREFIX` / `DESTDIR`) ve tek sürüm kaynağı
- **Mevcut durum:** `Makefile:71-77` sabit `$(HOME)/.local/bin` dizinine kuruyor. Bu yüzden `flake.nix:23-27` bile `make install`'u atlayıp kendi `installPhase`'ini yazmak zorunda kalmış; Arch `PKGBUILD` veya Fedora `.spec` yazarken de aynı sorun çıkar.
- **Yapılacak:**
  - `PREFIX ?= $(HOME)/.local`, `BINDIR ?= $(PREFIX)/bin`, `MANDIR ?= $(PREFIX)/share/man/man1`, `DESTDIR ?=` standart hedefleri — böylece mevcut root'suz `make install` (`~/.local/bin/ctron`, `AGENTS.md`/`README.md`) alışkanlığı kırılmaz; paketleyiciler (`flake.nix`, `PKGBUILD`, `.spec`) standart biçimde `make install PREFIX=/usr DESTDIR=...` geçer.
  - Sürüm numarasını tek bir `VERSION` dosyasından (veya `Makefile`'dan `-DCTRON_VERSION=\"...\"`) oku; `make check-version` hedefi `VERSION` == `README.md` badge == `flake.nix` tutarlılığını doğrulasın.

### 4.2. Eksik paketleme & masaüstü artefaktları (`man/`, `completions/`, `contrib/`)
- **`man/ctron.1`:** Bayraklar, çıkış kodları, ortam değişkenleri (`CTRON_CONFIG`, `DISPLAY_BACKEND`, `DAEBOARD_BIN`), config dosya yolları ve örnekler.
- **`completions/`:** `ctron.bash`, `_ctron` (zsh), `ctron.fish` — bayrak tamamlama + dinamik `ctron mode list` / `ctron snapshot list` tamamlaması.
- **`contrib/`:**
  - `contrib/99-ctron.rules` + `contrib/ctron.tmpfiles.conf` (bölüm 2.2 udev + tmpfiles kuralları)
  - `contrib/sudoers.ctron.example` (kopyala-yapıştır hatasını bitirir)
  - `contrib/ctron-ppt` (`SECURITY.md §4.3`'teki 3-tamsayılı sarmalayıcı, çalıştırılabilir gerçek dosya olarak)
  - `contrib/ctron.desktop` (KDE/Hyprland/launcher menüsü için)
  - `contrib/PKGBUILD` (Arch/CachyOS/AUR hazırlığı)
- **`flake.lock`:** FA507NVR (Arcioth) doğrulamasıyla birlikte `nix build` + `nix flake check` çalışır hale getirilip commit edilmeli.

### 4.3. Otomatik CI/CD (`.github/workflows/ci.yml`)
Şu an repoda hiç CI yok — tüm kalite güvencesi manuel oturum disiplinine dayanıyor. Tek bir hafif GitHub Actions workflow'u her commit/PR'da şunları otomatik kanıtlar:
1. `gcc` ve `clang` ile `-Wall -Wextra -Werror` derleme (normal + `HARDEN=1` + `LTO=1`). *(Not: GitHub Ubuntu runner'ındaki `libnotcurses-dev` sürümüyle `ncinput_shift_p` uyumu ilk koşuda doğrulanmalı.)*
2. `make test` + **UBSan** (`-fsanitize=undefined -fno-sanitize-recover=all`) + **LSan** (`-fsanitize=leak`).
3. `clang --analyze` statik analiz.
4. `shellcheck scripts/ctron-toggle.sh contrib/ctron-ppt`.
5. Sürüm & `--help` ↔ `README.md` bayrak listesi senkron kontrolü.
6. (2.4 tamamlanınca) `scripts/tui_smoke.py --offline`.

### 4.4. Depo kök dizini ve doküman hijyeni
- **[TAMAMLANDI 2026-11-23]:**
  - Tarihi kayıtlar (`PLAN.md`, `PLANS.md`), 2026-09/10 `HANDOFF.md` oturumları (`docs/history/handoff-2026-09-10.md`) ve 2860 satırlık ayrıntılı günlük `CHANGELOG.md` (`docs/history/changelog-2026-09-11.md`, eksik 3 başlık onarılarak) veri kayıpsız `docs/history/` altına arşivlendi.
  - Kök `CHANGELOG.md` kullanıcı-odaklı Keep a Changelog biçimine, kök `HANDOFF.md` güncel oturumlara, `NEXT.md` yalnızca açık işlere indirgendi; `README.md` (kırık anchor'lar dahil), `AGENTS.md`, `SECURITY.md`, `HARDWARE.md` ve `AUDIT.md` (çift `F4` + bayat `[x]` kayıtları) kodla hizalandı.
- **Kalan (`v0.14.0` paketinde):**
  - `CONTRIBUTING.md` ve `.github/ISSUE_TEMPLATE/hardware_report.yml` (`--doctor` çıktısı, kernel sürümü, laptop modeli isteyen şablon) ekle.

---

## 5. Uygulama Sırası (`0.x` Yol Haritası — `v1` Hedefi Şimdilik Yok)

Her aşama kendi içinde derlenir, test edilir ve bağımsız sürümlenir (`v1.0.0` baskısı olmadan `0.x` olarak ilerler):

| Hedef | Odak | Kapsam |
|---|---|---|
| **`v0.10.0`** | **Doğruluk & Hata Kapanışı (P0)** — **[TAMAMLANDI]** | Bölüm 1'in tamamı (1.1–1.11) + `cpufreq_write_all` direct-first + `tui_smoke.py` `flow_telem_full` cleanup (`8a70b77`..`b1eabe3`). |
| **`v0.11.0`** | **CLI, JSON & Sıfır-Shell (P1)** — **[TAMAMLANDI]** | `--status --json` / `--doctor --json`, `snapshot diff`, `--dry-run`, çıkış kodları (`0/1/2/3`); TUI log viewer (`L`, 24 satır), imleçli `tinput_t`, çift-`d` silme onayı, dürüst POWER satırları, `.ctr` kapsam tamamlama; kalan tüm `popen`/`ut_exec` noktalarının kaldırılması (`143d2f7`, `e77622f`, `2241130`). |
| **`v0.12.0`** | **Yeni TUI Tasarımı & Kısayol Mimarisi** — **[TAMAMLANDI]** | Bölüm 3.6: Konsept C (ince sol ray + tam boy sağ çalışma alanı, `< 96` kolonda otomatik üst sekme), terminal-doğal `Native`/`Mono` şeffaf + `Slate`/`Gruvbox`/`Light` temalar, `5 Telemetry` ana sekmesi + `startup_view` tercihi, tekil `Display & Light` (`APPLY_SYSTEM`), `ESC` yalnız geri/iptal, `,` Preferences, `?` Help, düz `1..5`/`Tab` (`7baf997`, `76f01dd`). |
| **`v0.13.0`** | **Snapshot Editor, Kayıtlı Fan Eğrileri, Dürüst PPT & Boot Snapshot** — **[TAMAMLANDI]** | İki panelli 15 alanlı Snapshot Inspector/Editor, klavye AURA okuma/doğrulama, per-core snapshot ızgarası, manuel 5 W PPT adımlama, Fan Eğrisi altyapısı + Kayıtlı Fan Eğrileri (`.fc`) + bölünmüş `4 Snapshots`, `1 Power` `r` revert + BIOS-kilitli `PPT limits: off` + `--` sahte-değersiz PPT + `boot_id` hafızası + `,` Preferences `Boot snapshot` & `--apply-startup` + `contrib/` systemd oneshot üniteleri. |
| **`v0.14.0`** | **Paketleme, `udev` & CLI/Masaüstü Artefaktları** | Bölüm 2.2, 4.1, 4.2, 4.4: `Makefile` `PREFIX`/`DESTDIR` + tek `VERSION` (`make check-version`), `flake.nix` uyumu, `contrib/` (`99-ctron.rules` + `ctron.tmpfiles.conf`, `ctron-ppt`, `sudoers.ctron.example`, `ctron.desktop`, `PKGBUILD`), `man/ctron.1`, shell completions (`bash`/`zsh`/`fish`), `CONTRIBUTING.md` + issue şablonu. |
| **`v0.15.0`** | **Offline Test Kalkanı, CI/CD & Modüler Temizlik** | Bölüm 2.3, 2.4, 4.3: Yeni TUI ve kısayol şeması üzerine `CTRON_SYSFS_ROOT` mock `/sys`+`/proc` testleri + `tui_smoke.py --offline`, `.github/workflows/ci.yml` (`gcc`+`clang`, `HARDEN=1`, UBSan/LSan, `clang --analyze`, `shellcheck`, offline smoke), `src/hw.c` ve UI modül bölünmesi + gizli `static` önbelleklerin `hw_state_t` içine alınması. |

---

## 6. Bilinçli Olarak Önerilmeyenler (Kapsam Koruması)

Profesyonelleşme "her şeyi eklemek" değil, **doğru sınırları korumaktır**:

- **Grafik/sparkline geri getirmemek:** Kullanıcı sayısal telemetri kararını net verdi; sayılar + `min/avg/max` + `since reset` daha okunaklı ve dürüst.
- **Arka plan daemon'ı yazmamak:** `ctron`'un en büyük gücü "çalışmıyorken sıfır kaynak, sıfır gizli servis" olması. Ayrıcalık ayrımı daemon yerine `udev` + `tmpfiles.d` + minimal `ctron-ppt` ile çözülmeli.
- **Yeni C kütüphanesi (jansson, libdbus, libnvml) eklememek:** Tek bağımlılık (`notcurses`) paketlemeyi ve taşınabilirliği olağanüstü kolaylaştırıyor; JSON çıktısı üretmek için kütüphane gerekmez.
- **Park edilmiş donanım kapsamını açmamak:** MUX/dGPU switch, Waybar doğrudan entegrasyonu (`--json` bunu temizce çözer), `throttle_thermal_policy`, genel non-ASUS plugin çerçevesi kapsam dışı kalmalı.
