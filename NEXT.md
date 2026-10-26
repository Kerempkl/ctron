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
- flake.nix'teki `version` da aynı release commit'inde güncellenir
  (VERSION + README badge + flake version üçlüsü).
- İlk kesim **v0.3.0** — 0.2.1'den (09-27) bu yana birikim MINOR
  seviyesinde: fan presets modali, M4 toggle + tek-örnek guard,
  PPT/ryzenadj yazma yolu, GPU clock lock, daeboard control + editör,
  apply worker. *(v0.3.0 atıldı 10-25; **v0.3.1** atıldı 10-26 —
  PATCH: FAZ 3 güvenlik zinciri + dürüstlük/doküman fixes; **v0.4.0**
  atıldı 10-26 — MINOR: tam ekran bölümü telemetri ekranı 't'.)*

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
3. **flake.nix [YAZILDI 10-26 — Arcioth doğrulaması bekliyor]** —
   `nix build` (packages.ctron + default) + `nix develop` (devShell =
   dokümante edilen toolchain). Bu makinede nix YOK → derleme
   doğrulaması FA507NVR'da (Arcioth): `nix build`, `nix develop`
   içinde `make`, ilk koşuda `flake.lock` üretilip commit edilmeli.

## FAZ 2 — Saha & doğrulama borcu (makineli, paralel yürür)

4. **FA507NVR canlı testleri (Arcioth):** `pwmN_enable=1` yazımı
   (custom mod canlı), asılı daeboard socket'i (SO_RCVTIMEO yolu
   canlı), `--gpu-clock` canlı, **çok monitör canlı proof (HDMI-A-1 +
   panel; hypr backend odaklı monitörün modlarını listeliyor)**.
   (HANDOFF 10-08 Grok oturumunun "not done live"
   listesi — bugüne dek hiçbir todo dosyasında değildi.)
5. **FA608PP saha testleri:** CCD kolonlarını elle doğrulama; post-OCCT
   kernel clamp → reboot → yeniden test; (fırsat çıkarsa) harici
   monitör takılıyken kde çok-ekran listesi.
6. **[BİTTİ 10-26 — kod tarafı]** Çok monitör: kde backend artık blok
   bazlı (eDP* panel tercih edilir, yoksa ilk blok; Mod satırı yalnız
   kendi bloğundan), hypr backend adı/geometriyi/mod listesini TEK
   odaklı monitör objesinden alıyor (önceki: bellekteki ilk
   availableModes). Offline iki-ekran testleri pinned; **canlı
   HDMI-A-1 proof'u** FAZ 2'ye taşındı (item 4 hypr / item 5 kde
   tarafı).
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
11. **[BİTTİ 10-26]** Sudoers sertleştirme dokümanı — ctron-ppt wrapper
    betiği + yol-kapsamlı kurallar (kurulum tarafı; kod değişikliği
    değil). (Faz 1.2 → SECURITY.md §4.)
12. **[BİTTİ 10-26]** ctron-ppt wrapper probe (3.11'in kod yarısı) — `ctrl_ppt_ryzen_cmd`
    çağrı zinciri önce `/usr/local/bin/ctron-ppt`'yi denesin (varsa
    `sudo -n /usr/local/bin/ctron-ppt SPL SPPT FPPT`); SECURITY.md §4.3'ü
    ctron'un kendisi için etkin kılar (~10 satır + test).

## FAZ 4 — Performans & optimizasyon (2026-10-26 ölçümlü denetim)

> Kaynak: o günün kapsamlı taraması (sıcak yolların kod okuması + canlı
> ölçüm; yöntem: pty harness `Session` + `/proc/<pid>/stat` utime/stime
> örnekleme, `/usr/bin/time` — probe atılır araçtı, kalıcı değil).
> Başlangıç zaten iyi: TUI boşta **%1.1 CPU**, çizim yolunda malloc/exec
> yok, harici sorgular cache'li (path cache 10-01, apply toplu yazma
> 10-09, nvidia-smi 2 s cache 10-26). Bu faz cila seviyesinde; tek
> güçlü aday 13.

| Ölçüm (FA608PP, 160×50, 250 ms poll) | Sonuç |
| --- | --- |
| TUI boşta | %1.10 CPU |
| telemetri tam ekran ('t') | %1.13 CPU (çizim maliyeti önemsiz) |
| tuş basılı tutma (~30 Hz tekrar) | %8.7 CPU — **tuş başına 2.6 ms** |
| nvidia-smi geniş sorgu | 20–30 ms, 2 s'de bir, UI thread'inde |
| `--status` uçtan uca | 0.21 s (harici araçlar 10–30 ms'şer) |
| `hw_refresh_fast` | ~69 dosya aç/oku/kapat / tick |
| binary | 285 KB, strip'siz |

13. **Tuş → tam yenileme eşlemesinin kesilmesi (en büyük ölçülmüş
    kazanç).** `ui_run` ana döngüsü her tuş olayından sonra koşulsuz
    `hw_refresh_fast` + `telem_poll` çalıştırıyor; her tuş = ~69 dosya
    okuma + tam yeniden çizim + render → basılı tutulan tuşta %8.7
    CPU. İki yol (tek başına ya da birlikte):
    (a) yenilemeyi rate-limit'le: son yenilemeden beri <100–150 ms
    olduysa atla — yazma etkisi olan eylem, apply bitişi (finalize) ve
    overlay aç/kapa muaf (anında yansıma gerekir);
    (b) input batching: `notcurses_get_nblock` ile bekleyen tuşları
    topla, hepsini işle, TEK yenileme+çizim yap.
    Doğrulama: aynı probe ile tuş-burst tekrar ölçüm (hedef
    %8.7 → ~%1–2), tuitest akışları yeşil kalmalı.
14. **`cpu_limits_sweep` ayrıştırması.** `scaling_max_freq` gerçekten
    salınıyor (amd-pstate; sweep'in varlık nedeni) ama
    `cpuinfo_min/max_freq` statik-dirençli. cpuinfo_* kolonunu 5–10
    s'de bir oku, scaling_max her tick kalsın → sweep 48 → 17 dosya
    okuması; poll yolundaki syscall'ların ~yarısı gider (laptop
    C-state'leri için de iyi). Dikkat: cpuinfo_max raise-only kuralı
    (2401 tuzağı, hw.c yorumu) aynen korunur — yalnızca
    `cpu_mhz_min/max` alanının güncellenmesi gecikir (pratikte
    değişmezler).
15. **Kurulumda strip + isteğe bağlı LTO.** `install` hedefi binary'yi
    olduğu gibi kopyalıyor; `install -s` (veya `STRIP ?= strip` +
    `$(STRIP)`) ile 285 KB → ~150–180 KB beklenen. LTO
    (`CFLAGS += -flto`, link aşamasına da) gcc 16'da sorunsuz ve
    taşınabilir — küçük boyut/hız kazancı; `make` uyarısız + `make
    test` şart. `-march=native` PARK: taşınabilirlik şartıyla çelişir.
16. **nvidia-smi arka plan thread'ine (KOŞULLU — şimdilik dokunma).**
    Burada ölçülen 25 ms / 2 s: 250 ms poll'da 8 framede 1'i yavaş,
    gözle görünmez. Yük altında nvidia-smi uzarsa (sürücü çekişmesi)
    apply-worker deseniyle arka plan örnekleme + UI'ya atomik sonuç
    geçişi; yalnızca saha gözlemi varsa yapılır.

Ölçüm gerekçesi zayıf — denetim kaydı (default hayır, istenirse):

- sysfs fd'lerini açık tutup `pread` ile yeniden okuma: open/close
  kalkar ama suspend/resume path-düşürme modeliyle çelişir; idle %1.1
  iken gerekçe yok.
- `statvfs("/")` 2–5 s cache: yerelde önemsiz; ağ FS'sinde pahalı
  olabilir (o duruma özel düşünülür).
- `ui_box` putchar-dolgusu → `ncplane_erase_region`: ölçülen fark yok
  (boşta ile tam ekran telemetri aynı CPU).
- `read_telem_extras`'ın 't' kapalıyken de örneklendirmesi TASARIM
  KARARI (ringler dolu tutulur → 't' açılınca 60 s geçmiş hazır) —
  bilinçli, değişmesin.

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
