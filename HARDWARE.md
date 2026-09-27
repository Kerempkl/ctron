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
| TUF A16 FA608PP (Ryzen 9 + RTX 5070, CachyOS, kernel 7.2) | ✅ Full — verified | Everything present: armoury limits, fan curves, EPP, k10temp, nvidia-smi, `sudo -n`. One quirk: the Quiet platform profile pins the cpuinfo ceiling to 2401 MHz (kernel behaviour, not a ctron bug). |
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
| TUF A16 FA608PP (Ryzen 9 + RTX 5070, CachyOS, kernel 7.2) | ✅ Tam — doğrulandı | Her şey var: armoury limitleri, fan eğrileri, EPP, k10temp, nvidia-smi, `sudo -n`. Tek tuhaflık: Quiet platform profili cpuinfo tavanını 2401 MHz'e kilitliyor (kernel davranışı, ctron hatası değil). |
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
