# Hardware support matrix / Donanım destek matrisi

*This file is bilingual: **English first**, the Turkish version follows
below under **Türkçe**. — Bu dosya iki dillidir: önce İngilizce, Türkçe
sürümü aşağıdadır.*

ctron depends on four separate layers, so hardware support is
**row-by-row, not all-or-nothing**:

1. **ASUS WMI stack** (`asus-nb-wmi` / `asus-armoury`) — fan curves,
   PPT, NV boost/temp, panel overdrive, keyboard: the heart of ctron,
   ASUS-only.
2. **AMD CPU side** (`k10temp`, `amd-pstate` EPP, cpufreq) — changes
   root-level on Intel.
3. **NVIDIA driver** (`nvidia-smi`) — dGPU temperature.
4. **Desktop stack** — KDE Wayland / Hyprland for refresh rate,
   passwordless `sudo -n` for privileged writes.

## English

| Device class | Expected | Why |
|---|---|---|
| TUF A16 FA608PP (Ryzen 9 + RTX 5070, CachyOS) | ✅ Full — verified on kernels **7.2** and **6.18-lts** | Everything present: armoury limits, fan curves, EPP, k10temp, nvidia-smi, `sudo -n`. On 6.18.52-cachyos-lts (2026-09-28) re-verified end to end: build 0 warnings, `make test` + `make tuitest` green, live status full (amd-pstate ceiling 5353/5386 MHz, custom-curve hwmon, fan write verify, KDE Hz). One quirk: the Quiet platform profile pins the cpuinfo ceiling to 2401 MHz (kernel behaviour, not a ctron bug). |
| TUF A15 FA507NVR (Ryzen 7 + RTX 4060, NixOS, Hyprland 0.55) | ✅ Full, two exceptions — verified | No `asus-armoury` → PPT limits fall back to the generic 90/120/120 W ceiling and PPT reads go stale (`--` often); the rest behaves identically. |
| ASUS ROG / Zephyrus (AMD + NVIDIA, current kernel) | 🟡 Expected to work — untested | Same ASUS WMI stack; scope is TUF, but the software layer is identical. |
| ASUS + Intel CPU (TUF F15, Dash F15…) | 🟡 Partial | Fan curves, platform profile, panel OD and keyboard come from ASUS WMI → work. But `k10temp` is absent (ctron does not read coretemp → CPU temp `--`), the PPT attributes are AMD-model-specific → PPT rows dead, and Intel pstate has no `cpufreq/boost` file → the CPU boost key does nothing. EPP and the frequency limit work. |
| ASUS iGPU-only (Vivobook / Zenbook, no dGPU) | 🟡 Narrowed | NV boost/temp `--`, GPU temp `--`, panel OD missing on most models; custom fan-curve WMI absent on many Vivobooks. What remains: EPP, frequency limit, battery limit, platform profile (if present). |
| ASUS on an older kernel (≲ 6.1) | 🟡 Feature loss | `fan_curve_cpu/gpu`, `nv_dynamic_boost`, `nv_temp_target` and EPP (`amd-pstate-epp`) attributes depend on kernel version; ctron reports them honestly as `--` / FAILED, never fakes them. |
| Non-ASUS laptops (Lenovo / HP / Dell…) | 🔴 Out of scope, mostly dead | The ASUS WMI stack is entirely absent: no fan curves, PPT, aura, keyboard. What remains: EPP, cpufreq limit, k10temp (if AMD), battery limit (`charge_control_end_threshold` is generic). Platform-profile writes usually fail — `quiet` does not exist on most OEMs. |
| No NVIDIA dGPU (any brand, incl. AMD dGPU) | 🔴 GPU rows dead | `nvidia-smi` and the ASUS NV WMI attributes need an NVIDIA dGPU. |
| GNOME Wayland / X11 (any hardware) | 🔴 Refresh-rate control unavailable | Hz needs KDE Wayland (kscreen-doctor) or Hyprland; no GNOME Wayland or X11 backend. Everything else is display-independent. |

Notes:

- **It fails honestly, not silently.** A missing interface shows `--`
  (reads) or logs `FAILED (needs root; no passwordless sudo)` (writes).
  Fabricated data does not exist in ctron.
- **The most common breakage is sudo.** Without asusctl, every write
  falls through to `sudo -n`; on stock Ubuntu / Fedora / NixOS setups
  the write side fails wholesale while reads keep working.
- **The two verified machines are deliberately different**: FA608PP
  shows "everything + armoury limits", FA507NVR shows "everything but
  the generic PPT ceiling + stale reads". The gap is exactly the
  kernel / asus-armoury difference.
- **`asusctl fan-curve --default` is a footgun (probed 2026-10-18)**:
  it installs asusd's GENERIC fallback curves — every profile comes
  back `enabled: false` and quiet gains a 255 °C sentinel point — not
  the OEM factory curve. The pre-existing `fan_curves.ron` (seeded
  from the EC at asusd's first init) is the better factory reference;
  do not reset it expecting factory behaviour.
- **KDE M4 / Armoury Crate key (FA608PP, verified 2026-10-03)**: the key
  emits scancode `0x26` → `KEY_PROG3` → XKB keysym `XF86Launch3`, so the
  KDE shortcut picker shows it as **"Launch (3)"**. Bind it to ctron via
  System Settings → Keyboard → Shortcuts → Add New → Command or Script
  (e.g. `konsole -e ctron`), then press the key. If the picker refuses,
  read the real code with `sudo evtest /dev/input/event11` ("Asus WMI
  hotkeys"); note that XKB keycodes are the Linux input code **+ 8**
  (`KEY_PROG3` = 202 → `<I210>`). Bound keys are consumed by kwin —
  `kitty +kitten show_key` and friends never see them, which is normal.
  Verified live 2026-10-11 after re-login. Use `ctron-toggle`
  (`make install` puts it in `~/.local/bin`) as the command: a desktop
  shortcut only spawns, so a bare `konsole -e ctron` opened a new
  window per key press; the toggle closes the running ctron instead.
- **CPU PPT watt limits (FA608PP, 2026-10-08)**: the legacy nb-wmi
  `ppt_*` nodes accept writes (readback echoes them) but the SMU
  ignores them under load — an OCCT test with 30/40/50 W set pinned
  the package at 80 W. The official asus-armoury attributes report
  ppt_* (and nv_*) "unavailable" (ENODEV). The only working interface
  is **ryzenadj** (SMU mailbox via /dev/mem as root; without the
  ryzen_smu module): user-verified pinning limits under load. ctron
  probes the interface once at init (`--doctor` → "ppt iface") and
  maps SPL→stapm(-a), SPPT→slow(-c), FPPT→fast(-b). Requires a
  sudoers rule naming the path LITERALLY (`/usr/bin/ryzenadj` —
  sudoers cannot see through the merged-usr /usr/sbin→bin symlink).
  Honesty limits: Dragon Range exposes no SMU read-back, so writes are
  logged as unverified; SMU limits are VOLATILE — a reboot, suspend or
  platform-profile change can clear them, re-apply after. Security
  note: NOPASSWD ryzenadj also unlocks `--tctl-temp` (raising the
  thermal cap) for anything running as the user; argument-constrained
  sudoers rules are weak protection (`*` spans argument boundaries).
  Optional hardening — a root-owned wrapper that only ever forwards
  three integers:
  `/usr/local/bin/ctron-ppt`:
  `#!/bin/sh\n[ $# -eq 3 ] || exit 2\ncase "$1$2$3" in *[!0-9]*) exit 2;; esac\nexec /usr/bin/ryzenadj -a "$1" -c "$2" -b "$3"\n`
  + sudoers `NOPASSWD: /usr/local/bin/ctron-ppt` (ctron keeps using
  the direct path until a future opt-in).
- **GPU clock lock (FA608PP, verified 2026-10-04)**: laptop GPU power
  limits are locked (`power.limit` reads `[N/A]`) and the nb-wmi
  `nv_dynamic_boost` / `nv_temp_target` rows apply without any
  observable effect on this machine. The working lever is the core
  clock: `nvidia-smi -lgc <mhz>,<mhz>` (reset `-rgc`) — a 1400 lock
  pins the core at 1395 MHz (driver clock grid) under load. On driver
  615.71.09 the applications-clocks queries are deprecated and no
  event-reason bit marks the lock, so ctron verifies by sampling
  `clocks.current.graphics` (every sample must stay at or below the
  lock). Needs a sudoers rule for the real path
  (`/usr/sbin/nvidia-smi`); the persistence-mode warning is cosmetic.
- **CCD disable vs CPU boost ceilings (FA608PP, measured 2026-10-06)**:
  under full GPU load (114 W) the CPU boost ceiling oscillates on its
  own — firmware CPPC management (Dynamic Boost power shift + shared
  heatsink; sawtooth ~4.3-5.4 GHz with the package temp pinned at the
  ~95 °C soft limit). Runtime CPU offlining does NOT change that
  policy: with CCD2 offlined via /sys the machine clamped to the
  nominal 2500 MHz at 95 °C — worse than 16 cores. Disabling the CCD
  in BIOS does: with the true 8-core topology the SMU re-budgets
  power (CPU package power observed dropping ~80 -> 45-60 W) and the
  ceiling holds 5386 MHz ~80-85 % of the time, dips shallow and
  recovering within seconds. Practical: 8C/16T single-CCD (no
  cross-CCD latency) is a sensible gaming configuration, but
  scripting runtime offlining is NOT a substitute — a BIOS change +
  reboot is required.

- **Telemetry sensor map (the 't' screen, v0.7.0)** — every source is
  read-only sysfs/procfs except nvidia-smi (one query, 2 s cache);
  absent sensors print `--`, never a guess:
  - **CPU**: k10temp `temp1` (Tctl) + `temp3`/`temp4` (Tccd1/2 —
    accepted only when their label files say `Tccd`, so a different
    chip layout can never mislabel a sensor); per-core ceilings from
    cpufreq `scaling_max_freq`.
  - **Fans**: asus hwmon `fan1/2_input` RPM (0 is honest — the EC
    stops the GPU fan at idle on many machines).
  - **dGPU** (nvidia-smi, one query): temperature, power draw,
    graphics clock, utilisation, VRAM, pstate and the ten named
    `clocks_event_reasons.*` flags. Traps: the dashed names printed
    by `--help` are NOT valid query fields (underscores only);
    `clocks_event_reasons.active` returns a hex bitmask on current
    drivers, so the self-labeled Active/Not Active fields are parsed
    instead; one unknown field rejects the WHOLE query — ctron
    latches a permanent in-session fallback to the old six-field
    form so temperatures never go dark.
  - **iGPU** (amdgpu hwmon): temperature, clock, `power1_input` =
    **SoC PPT — the whole-APU package (CPU + iGPU)**, not an
    iGPU-only rail; vddgfx/vddnb millivolts. DRM device adds
    `gpu_busy_percent` and the active `pp_dpm_sclk` level.
    `gpu_metrics`: the only live power slice on Dragon Range is the
    gfx field (signature-gated read) — the CPU blocks of that blob
    are permanently 0xFF (probed live 2026-10-27) and RAPL
    `energy_uj` is root-only, so there is **no honest sudoless CPU
    watt on FA608PP**.
  - **RAM**: spd5118 module temps are uncommon hardware — most
    laptops have none and show `--`. Where present, the limits
    (`temp1_max`/`temp1_crit`) and the kernel `max_alarm` flag
    surface too (a module over its limit marks the grid line
    `!>55`).
  - **Storage**: NVMe composite (`temp1`) plus Sensor 1/2
    (`temp2/3` — the components run hotter than the composite),
    `/` usage via statvfs, per-mount usage from
    `/proc/self/mounts` (whole-disk mounts only, one row per
    device — btrfs subvolumes share the device's space and are
    deduped with a note), disk I/O from /proc/diskstats (whole
    disks only — partitions/loops excluded so bytes are never
    counted twice).
  - **NET**: `/sys/class/net/*/statistics` for physical + wifi
    interfaces only (lo, veth*, docker*, br-*, virbr*, tap*, tun*,
    wg*, tailscale* excluded — the same double-count guard); rx/tx
    are byte-counter deltas between ticks, so an interface
    disappearing reads as one unknown tick, not a spike. Wifi link
    quality comes from `/proc/net/wireless` (mac80211 0-70 scale,
    shown as %; the level is dBm only when the driver reports a
    negative value — other scales stay unknown).
  - **Power**: battery %/status/voltage/current, health from
    `charge_full(_design)` (this pack has no battery temperature
    sensor and `cycle_count` reports 0 — unsupported, never
    displayed); USB-C PD input from ucsi `type=USB` supplies (reads
    0 while the machine runs on barrel AC).

## Türkçe

ctron dört ayrı katmana dayanır; bu yüzden donanım desteği **hepten
değil, satır satır** belirlenir:

1. **ASUS WMI yığını** (`asus-nb-wmi` / `asus-armoury`) — fan
   eğrileri, PPT, NV boost/sıcaklık, panel overdrive, klavye:
   ctron'un kalbi, yalnızca ASUS'ta var.
2. **AMD CPU tarafı** (`k10temp`, `amd-pstate` EPP, cpufreq) —
   Intel'de kökten değişir.
3. **NVIDIA sürücüsü** (`nvidia-smi`) — dGPU sıcaklığı.
4. **Masaüstü yığını** — yenileme hızı için KDE Wayland / Hyprland,
   yetkili yazımlar için şifresiz `sudo -n`.

| Cihaz sınıfı | Beklenen durum | Neden |
|---|---|---|
| TUF A16 FA608PP (Ryzen 9 + RTX 5070, CachyOS) | ✅ Tam — **7.2** ve **6.18-lts** kernel'lerde doğrulandı | Her şey var: armoury limitleri, fan eğrileri, EPP, k10temp, nvidia-smi, `sudo -n`. 6.18.52-cachyos-lts üzerinde (2026-09-28) uçtan uca yeniden doğrulandı: derleme 0 uyarı, `make test` + `make tuitest` yeşil, canlı durum tam (amd-pstate tavanı 5353/5386 MHz, custom-curve hwmon, fan yazma doğrulaması, KDE Hz). Tek tuhaflık: Quiet platform profili cpuinfo tavanını 2401 MHz'e kilitliyor (kernel davranışı, ctron hatası değil). |
| TUF A15 FA507NVR (Ryzen 7 + RTX 4060, NixOS, Hyprland 0.55) | ✅ Tam, iki istisna — doğrulandı | `asus-armoury` yok → PPT limitleri jenerik 90/120/120 W tavana düşer, PPT okuması sık bayatlar (`--`); geri kalanı birebir aynı çalışır. |
| ASUS ROG / Zephyrus (AMD + NVIDIA, güncel kernel) | 🟡 Çalışması beklenir — test edilmedi | Aynı ASUS WMI yığını; kapsam TUF ama yazılım katmanı birebir aynı. |
| ASUS + Intel CPU (TUF F15, Dash F15…) | 🟡 Kısmi | Fan eğrileri, platform profili, panel OD ve klavye ASUS WMI'den gelir → çalışır. Ama `k10temp` yok (ctron coretemp okumuyor → CPU sıcaklığı `--`), PPT attr'ları AMD modeline özgü → PPT satırları ölü, Intel pstate'te `cpufreq/boost` dosyası yok → CPU boost anahtarı işlevsiz. EPP ve frekans limiti çalışır. |
| ASUS iGPU-only (Vivobook / Zenbook, dGPU'suz) | 🟡 Daraltılmış | NV boost/sıcaklık `--`, GPU sıcaklığı `--`, panel OD çoğu modelde yok; çok sayıda Vivobook'ta fan eğrisi WMI'sı hiç yok. Kalan: EPP, frekans limiti, batarya limiti, (varsa) platform profili. |
| ASUS + eski kernel (≲ 6.1) | 🟡 Özellik kaybı | `fan_curve_cpu/gpu`, `nv_dynamic_boost`, `nv_temp_target` ve EPP (`amd-pstate-epp`) attr'ları kernel sürümüne bağlı; ctron bunları dürüstçe `--` / FAILED olarak raporlar, asla uydurmaz. |
| ASUS olmayan laptop (Lenovo / HP / Dell…) | 🔴 Kapsam dışı, çoğu ölü | ASUS WMI yığını bütünüyle yok: fan eğrileri, PPT, aura, klavye yok. Kalan: EPP, cpufreq limiti, k10temp (AMD ise), batarya limiti (`charge_control_end_threshold` geneldir). Platform profili yazımları genelde başarısız — `quiet` çoğu OEM'de yok. |
| NVIDIA dGPU yok (her marka, AMD dGPU dahil) | 🔴 GPU satırları ölü | `nvidia-smi` ve ASUS NV WMI attr'ları NVIDIA dGPU ister. |
| GNOME Wayland / X11 (her donanım) | 🔴 Hz kontrolü yok | Yenileme hızı için KDE Wayland (kscreen-doctor) ya da Hyprland gerekir; GNOME Wayland ve X11 backend'i yok. Geri kalan her şey ekrandan bağımsızdır. |

Notlar:

- **Sessiz değil, dürüst düşer.** Olmayan bir arayüz ya `--` gösterir
  (okuma) ya da log'a `FAILED (needs root; no passwordless sudo)`
  yazar (yazım). ctron'da uydurma veri yoktur.
- **En yaygın kırılma nedeni sudo'dur.** asusctl'siz makinelerde her
  yazım `sudo -n`'e düşer; standart Ubuntu / Fedora / NixOS kurulumunda
  yazım tarafı topluca başarısız olurken okumalar çalışmayı sürdürür.
- **İki doğrulanmış makine bilinçli olarak birbirinden farklı**: FA608PP
  "her şey + armoury limitleri", FA507NVR "her şey ama jenerik PPT
  tavanı + bayat okuma" profili çizer. Aradaki fark tam olarak
  kernel / asus-armoury farkıdır.
- **`asusctl fan-curve --default` bir tuzak (2026-10-18'de denendi)**:
  asusd'nin GENERIC yedek eğrilerini kurar — her profil `enabled:
  false` döner, quiet'e 255 °C sentinel noktası girer — OEM fabrika
  eğrisini değil. Önceden var olan `fan_curves.ron` (asusd ilk açılışta
  EC'den aldığı değerler) daha iyi fabrika referansı; fabrika davranışı
  bekleyerek sıfırlamayın.
- **KDE M4 / Armoury Crate tuşu (FA608PP, 2026-10-03'te doğrulandı)**:
  tuş `0x26` tarama kodu → `KEY_PROG3` → XKB `XF86Launch3` yollar; KDE
  kısayol seçicisinde **"Launch (3)"** olarak görünür. ctron'a
  bağlamak için: Sistem Ayarları → Klavye → Kısayollar → Yeni ekle →
  Komut veya betik (örn. `konsole -e ctron`), ardından tuşa bas.
  Seçici yakalamazsa gerçek kodu `sudo evtest /dev/input/event11`
  ("Asus WMI hotkeys") ile oku; XKB tuş kodu Linux input kodunun
  **8 fazlasıdır** (`KEY_PROG3` = 202 → `<I210>`) — bu tuzağa dikkat.
  Bağlanan tuşları kwin yakar; `kitty +kitten show_key` gibi araçlar
  göremez — bu normaldir. 2026-10-11'da re-login sonrası canlı
  doğrulandı. Komut olarak `ctron-toggle` kullan (`make install`
  `~/.local/bin`'e koyar): masaüstü kısayolu yalnızca süreç başlatır,
  çıplak `konsole -e ctron` her basışta yeni pencere açıyordu; toggle
  ayaktaki ctron'u kapatır.
- **CPU PPT watt limitleri (FA608PP, 2026-10-08)**: eski nb-wmi `ppt_*`
  düğümleri yazımı kabul ediyor (geri okuma değeri yansıtıyor) ama SMU
  yük altında yok sayıyor — 30/40/50 W ayarlanan OCCT testinde paket
  80 W'a çıktı. Resmi asus-armoury öznitelikleri ppt_* (ve nv_*) için
  "unavailable" (ENODEV) diyor. Çalışan tek arayüz **ryzenadj**
  (root'ta /dev/mem üzerinden SMU posta kutusu; ryzen_smu modülü
  olmadan): kullanıcı tarafından yük altında doğrulandı. ctron arayüzü
  init'te bir kez yoklar (`--doctor` → "ppt iface") ve
  SPL→stapm(-a), SPPT→slow(-c), FPPT→fast(-b) eşler. Sudoers kuralı
  yolu LITERAL yazmalıdır (`/usr/bin/ryzenadj` — sudoers, merged-usr
  /usr/sbin→bin symlink'in içini göremez). Dürüstlük sınırları:
  Dragon Range'te SMU geri okuması yok → yazımlar doğrulanmamış olarak
  loglanır; SMU limitleri UÇUCUDUR — reboot, suspend veya platform
  profili değişimi temizleyebilir, sonra yeniden uygula. Güvenlik
  notu: NOPASSWD ryzenadj, kullanıcı adına çalışan her şeye
  `--tctl-temp`'i (termal tavanı yükseltme) de açar; argüman-kısıtlı
  sudoers kuralları zayıf korumadır (`*` argüman sınırlarını aşar).
  İsteğe bağlı sertleştirme — yalnız üç tam sayıyı ileten root-sahipli
  sarmalayıcı:
  `/usr/local/bin/ctron-ppt`:
  `#!/bin/sh\n[ $# -eq 3 ] || exit 2\ncase "$1$2$3" in *[!0-9]*) exit 2;; esac\nexec /usr/bin/ryzenadj -a "$1" -c "$2" -b "$3"\n`
  + sudoers `NOPASSWD: /usr/local/bin/ctron-ppt` (ctron, ileride bir
  tercih açılana dek doğrudan yolu kullanır).
- **GPU saat kilidi (FA608PP, 2026-10-04'te doğrulandı)**: laptop GPU
  watt limitleri kilitlidir (`power.limit` = `[N/A]`) ve nb-wmi
  `nv_dynamic_boost` / `nv_temp_target` satırları bu makinede
  gözlemlenebilir etki vermeden uygulanır. Çalışılan manivela çekirdek
  saatidir: `nvidia-smi -lgc <mhz>,<mhz>` (sıfırlama `-rgc`) — 1400
  kilidi, yük altında çekirdeği 1395 MHz'de (sürücü saati ızgarası)
  sabitler. 615.71.09 sürücüsünde applications-clocks sorguları
  deprecated ve kilit için hiçbir event-reason biti yanmıyor; ctron
  bu yüzden `clocks.current.graphics` örnekleyerek doğrular (her
  örnek kilidin altında kalmalı). Gerçek yol için sudoers kuralı
  gerekir (`/usr/sbin/nvidia-smi`); persistence-mode uyarısı
  kozmetiktir.
- **CCD kapatma vs CPU boost tavanları (FA608PP, 2026-10-06'da
  ölçüldü)**: tam GPU yükünde (114 W) CPU boost tavanı kendi kendine
  salınır — firmware CPPC yönetimi (Dynamic Boost güç aktarımı +
  paylaşımlı soğutucu; testere ~4,3-5,4 GHz, paket sıcaklığı ~95 °C
  yumuşak sınıra çakılı). Runtime çekirdek çevrimdışı bırakma bu
  politikayı DEĞİŞTİRMİYOR: CCD2'yi /sys üzerinden kapatınca makine
  nominal 2500 MHz'e 95 °C'de kilitlendi — 16 çekirdektekinden kötü.
  BIOS'tan CCD kapatmak değiştiriyor: gerçek 8-çekirdek topolojisiyle
  SMU gücü yeniden bütçeliyor (CPU paket gücü ~80 -> 45-60 W'a düştü
  gözlendi) ve tavan zamanın ~%80-85'inde 5386 MHz'de kalıyor;
  dalışlar sığ ve saniyeler içinde toparlıyor. Pratik: 8C/16T tek CCD
  (CCD'ler arası gecikme yok) oyun için makul bir yapı; runtime
  offline betiklemek yerine geçmez — BIOS değişikliği + reboot
  gerekir.
- **Telemetri sensör haritası ('t' ekranı, v0.7.0)** — nvidia-smi
  dışında (tek sorgu, 2 sn önbellek) her kaynak salt-okunur
  sysfs/procfs; olmayan sensör `--` basar, asla tahmin etmez:
  - **CPU**: k10temp `temp1` (Tctl) + `temp3`/`temp4` (Tccd1/2 —
    yalnızca etiket dosyaları `Tccd` diyorsa kabul edilir, başka bir
    çip düzeninde yanlış etiket imkânsız); çekirdek başına tavanlar
    cpufreq `scaling_max_freq`'ten.
  - **Fanlar**: asus hwmon `fan1/2_input` RPM (0 dürüst değerdir —
    birçok makinede EC, GPU fanını boştayla tamamen durdurur).
  - **dGPU** (nvidia-smi, tek sorgu): sıcaklık, güç, grafik saati,
    kullanım, VRAM, pstate ve on adlandırılmış
    `clocks_event_reasons.*` bayrağı. Tuzaklar: `--help`'in
    yazdığı çizgili adlar GEÇERSİZ sorgu alanıdır (yalnız alt tire);
    `clocks_event_reasons.active` güncel sürücülerde hex bitmaska
    döner, bu yüzden kendi kendini etiketleyen Active/Not Active
    alanları ayrıştırılır; tek bilinmeyen alan TÜM sorguyu reddeder
    — ctron kalıcı oturum-içi eski altı-alan formuna düşer,
    sıcaklıklar asla kararmaz.
  - **iGPU** (amdgpu hwmon): sıcaklık, saat, `power1_input` = **SoC
    PPT — APU'nun TAM paketi (CPU + iGPU)**, yalnız iGPU rayı değil;
    vddgfx/vddnb milivoltları. DRM cihazı `gpu_busy_percent` ve
    etkin `pp_dpm_sclk` kademesini ekler. `gpu_metrics`: Dragon
    Range'de tek canlı güç dilimi gfx alanıdır (imza-kapılı okuma) —
    o bloğun CPU kısımları kalıcı 0xFF (2026-10-27'de canlı
    yoklandı) ve RAPL `energy_uj` root-only olduğundan FA608PP'de
    **sudo'suz dürüst CPU watt yoktur**.
  - **RAM**: spd5118 modül sıcaklıkları nadir donanımdır — çoğu
    laptop'ta yoktur, `--` görünür. Varsa limitler
    (`temp1_max`/`temp1_crit`) ve çekirdeğin `max_alarm` bayrağı da
    yüzeye çıkar (limiti aşan modül grid satırını `!>55` işaretler).
  - **Depolama**: NVMe bileşik (`temp1`) + Sensor 1/2 (`temp2/3` —
    bileşenler bileşikten daha sıcak çalışır), statvfs ile `/`
    doluluk, `/proc/self/mounts`'tan mount başına doluluk (yalnız
    gerçek disk mountları, cihaz başına tek satır — btrfs alt
    birimleri aynı cihazın alanını paylaşır, notla teklenir),
    /proc/diskstats'tan disk I/O (yalnız bütün diskler —
    bölüm/loop'lar hariç, baytlar iki kez sayılmaz).
  - **NET**: `/sys/class/net/*/statistics` yalnız fiziksel + wifi
    arayüzler için (lo, veth*, docker*, br-*, virbr*, tap*, tun*,
    wg*, tailscale* hariç — aynı çift-sayım koruması); rx/tx tik
    arası bayt-sayaç deltalarıdır, arayüz kaybolursa tek bilinmeyen
    tik okunur, spayk değil. Wifi sinyal kalitesi
    `/proc/net/wireless`'ten (mac80211 0-70 ölçeği, yüzde
    gösterilir; seviye yalnız sürücü negatif (dBm) raporlarken
    okunur — diğer ölçekler bilinmiyor kalır).
  - **Güç**: batarya %/durum/gerilim/akım, sağlık
    `charge_full(_design)`'dan (bu pakette batarya sıcaklık sensörü
    yok ve `cycle_count` 0 raporlar — desteklenmiyor, asla
    gösterilmez); USB-C PD girişi ucsi `type=USB` kaynaklarından
    (fişli-fıçı AC'de 0 okur).
