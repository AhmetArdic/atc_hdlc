# HDLC PROTOKOL KÜTÜPHANESİ — MİMARİ TASARIM BELGESİ

> **Bu belge, bir HDLC protokol kütüphanesinin implementasyonu için mimari referanstır.**
> **Davranış sözleşmelerini ve kısıtları tanımlar. Fonksiyon isimleri, parametre yapıları, dosya organizasyonu ve algoritmik detaylar implementasyon kararıdır.**

- **Hedef Standart:** ISO/IEC 13239 (HDLC Procedures)
- **Dil:** C99
- **Hedef:** Gömülü sistemler (bare-metal, RTOS, Linux embedded)

---

## 1. KAPSAM

### İlk sürümde desteklenenler

- ABM modu (combined station, eşit taraflar)
- I-frame, S-frame (RR, RNR, REJ), U-frame (SABM, DISC, UA, DM, FRMR, UI, TEST)
- Mod-8 sıra numaralama (3-bit N(S)/N(R), pencere 1–7)
- FCS-16 (ITU-T CRC-CCITT)
- Byte stuffing (0x7E flag, 0x7D escape)
- Go-Back-N ARQ (REJ tabanlı)
- Sliding window flow control (remote ve local)
- T1 (retransmission), T2 (ack delay), T3 (idle/keep-alive) zamanlayıcıları
- N2 (max retry) parametresi
- Piggybacked acknowledgement
- TEST frame ile link diagnostiği
- İstatistik toplama

### Ertelenen özellikler (future-proof altyapı hazır, implemente edilmeyecek)

NRM modu, ARM modu, mod-128 (SABME), SREJ, XID, genişletilmiş adresleme (multi-byte), FCS-32, segmentasyon/reassembly.

### Kapsam dışı

Fiziksel katman, OS bağımlılıkları, şifreleme, IP entegrasyonu.

### Platform gereksinimleri

- CHAR_BIT >= 8 olan tüm platformlar desteklenir.
- Wire format her zaman 8-bit oktet tabanlıdır.
- Kütüphane kendi oktet tipini tanımlar: CHAR_BIT==8'de uint8_t eşdeğeri, CHAR_BIT>8'de (TI C2000 gibi) daha geniş tamsayı — yalnızca alt 8 bit anlamlıdır.
- CHAR_BIT>8'de her oktet bir adreslenebilir birim kaplar (bellek overhead kabul edilir).

---

## 2. TASARIM İLKELERİ

1. **Sıfır dinamik bellek.** malloc/calloc/realloc/free çağrılmaz. Tüm bellek kullanıcı tarafından statik olarak sağlanır.
2. **Platform bağımsızlığı.** OS/HAL/donanım çağrısı yapılmaz. Fiziksel gönderim, zamanlama ve olay bildirimi kullanıcıya delege edilir. Delegasyon mekanizması (callback, tick, vb.) implementasyon kararıdır.
3. **İki katmanlı mimari.** Frame (stateless) + Station (tüm protokol mantığı).
4. **Konfigürasyon ile davranış kontrolü.** Tüm protokol parametreleri çalışma zamanında belirlenir. Derleme zamanı sabitleri yalnızca opsiyonel optimizasyonlar içindir.
5. **Future-proof genişletilebilirlik.** Yeni mod/özellik eklemek mevcut API'yi bozmaz.

---

## 3. MİMARİ

```
  Kullanıcı Uygulaması
        │
  [STATION]       Tek API yüzeyi
        │         Dahili sorumluluk alanları:
        │           ▸ U-frame işleme (bağlantı yaşam döngüsü, mod yönetimi)
        │           ▸ I/S-frame işleme (sıra numaralama, pencere, ack/retry)
        │
  [FRAME]         Stateless çerçeve oluşturma/ayrıştırma, FCS
        │
  Platform Entegrasyonu
```

### Frame Katmanı

Tamamen stateless, yan etkisiz. Buffer girer, buffer çıkar. Station'dan bağımsız kullanılabilir. Sorumlulukları: çerçeve serileştirme (adres + kontrol + payload + FCS + stuffing), çerçeve ayrıştırma (destuffing, FCS doğrulama, alan ayrıştırma), FCS hesaplama/doğrulama.

### Station Katmanı

Bir HDLC ucunun tüm protokol mantığını barındırır. Kullanıcı yalnızca bu katmanla muhatap olur.

Dahili iki sorumluluk alanı vardır (bu ayrım dışarıya yansımaz, API tektir):

- **U-frame işleme:** Bağlantı yaşam döngüsü (SABM/UA/DISC/DM), FRMR, UI, TEST, mod yönetimi. NRM/ARM eklendiğinde bu alan değişir.
- **I/S-frame işleme:** V(S)/V(R), sliding window, ack (piggybacked+standalone), RR/RNR (remote+local), REJ/Go-Back-N, retry. Mod bağımsızdır.

---

## 4. BELLEK MODELİ

### Temel ilke

Kütüphane hiçbir buffer tanımlamaz, hiçbir ayırma yapmaz. Tüm bellek (tx pencere slot'ları + rx buffer) kullanıcı tarafından oluşturulup init sırasında pointer olarak enjekte edilir. Kütüphane yalnızca pointer ve boyut saklar.

### Tutarlılık kontrolü (init sırasında)

- Slot sayısı == konfigürasyondaki pencere boyutu.
- Her slot kapasitesi >= max_frame_size (oktet cinsinden).
- rx buffer boyutu >= max_frame_size (oktet cinsinden).
- CHAR_BIT>8 platformlarda boyutlar oktet cinsinden değerlendirilir.
- Tutarsızlık → hata döner.

### Payload teslimat modeli

İki yaklaşım desteklenmelidir:

1. **Kopyalamalı:** Kütüphane rx buffer'daki payload'a pointer sunar. Kullanıcı teslimat sırasında kopyalar. Teslimat dönüşünden sonra buffer tekrar kullanılır.
2. **Pointer swap (zero-copy):** Kullanıcı dolu rx buffer pointer'ını alıp yerine boş buffer pointer'ı verir. DMA senaryolarında kritiktir.

İmplementasyon en azından zero-copy'yi mümkün kılmalıdır (rx buffer pointer'ı çalışma zamanında değiştirilebilir olmalı).

### Alignment uyarısı

Payload opak oktet dizisi olarak teslim edilir. Struct'a cast sorumluluğu kullanıcıdadır. ARM Cortex-M vb. mimarilerde unaligned access → HardFault riski. Kütüphane rx buffer hizalaması garanti etmez.

### Station bellek düzeni (kavramsal)

Üç bölüm: sabit durum alanları (sayaçlar, flagler, config referansı, platform bilgisi, istatistikler) + tx pencere referansı (slot dizisine pointer) + rx buffer referansı (buffer'a pointer).

---

## 5. TİP TANIMLARI

### Oktet tipi

Kütüphane kendi oktet tipini tanımlar. CHAR_BIT==8'de uint8_t eşdeğeri, CHAR_BIT>8'de uint_least8_t veya unsigned char. Tüm wire-format işlemleri 0xFF mask'ı ile çalışır. Tüm buffer parametreleri, payload pointer'ları ve frame verileri bu tip cinsindendir.

### Modlar

| Mod | Durum | Açıklama |
|-----|-------|----------|
| ABM | İlk sürüm | Eşit taraflar, her ikisi komut/yanıt gönderebilir |
| NRM | Reserved | Primary/secondary, secondary yalnızca poll edildiğinde gönderir |
| ARM | Reserved | Secondary poll beklemeden gönderebilir, kontrol primary'de |

### Çerçeve tipleri

| Tip | İçerik | Sıra no. |
|-----|--------|----------|
| I-frame | Kullanıcı verisi | N(S), N(R) |
| S-frame | Akış kontrolü/hata kurtarma, veri taşımaz | N(R) |
| U-frame | Bağlantı yönetimi, bazıları (UI, TEST) veri taşır | Yok |

### Supervisory alt tipleri

| Alt tip | İşlev |
|---------|-------|
| RR | Alıcı hazır, N(R)'ye kadar onay |
| RNR | Alıcı meşgul, gönderimi durdur |
| REJ | N(R)'den itibaren yeniden gönder (Go-Back-N) |
| SREJ | Yalnızca belirtilen frame'i yeniden gönder **(future)** |

### Unnumbered alt tipleri

| Alt tip | İşlev | Durum |
|---------|-------|-------|
| SABM | ABM bağlantısı kurma (P=1, UA ile onay) | İlk sürüm |
| SABME | Mod-128 ABM kurulumu | Future |
| DISC | Bağlantı kesme (P=1, UA ile onay) | İlk sürüm |
| UA | Komut onayı (SABM/DISC'e F=1) | İlk sürüm |
| DM | Karşı taraf disconnected modda | İlk sürüm |
| FRMR | Kurtarılamaz protokol hatası. Bilgi alanı: reddedilen kontrol, V(S), V(R), neden bitleri (W: geçersiz kontrol, X: izin verilmeyen bilgi alanı, Y: bilgi alanı çok uzun, Z: geçersiz N(R)) | İlk sürüm |
| UI | Bağlantısız veri, sıra numarasız, ack gerektirmez | İlk sürüm |
| TEST | Link diagnostiği. Bilgi alanı içerebilir. Alıcı aynı bilgi alanıyla TEST(F=1) döner. Bağlantı durumundan bağımsız | İlk sürüm |
| XID | Parametre müzakeresi | Future |
| SNRM | NRM kurulumu | Future |
| SARM | ARM kurulumu | Future |

### Bağlantı durumları (state machine)

Station bir durum makinesi ile yönetilir. Durumlar:

- Bağlantı yok (başlangıç/bitiş)
- Bağlantı kurulumu bekleniyor
- Aktif veri transferi
- Karşı taraf meşgul (gönderim askıda)
- Yerel taraf meşgul
- Sıra hatası, yeniden iletim bekleniyor
- Kurtarılamaz hata (FRMR)
- Bağlantı kesme onayı bekleniyor

**Geçiş kuralları:**

- U-frame'ler → büyük durum geçişleri
- S-frame'ler → aktif bağlantı içi alt durum geçişleri
- Zamanlayıcı dolumları → retry, max retry aşımında bağlantı düşer
- Yerel meşgullük → kullanıcı tarafından tetiklenir

**FRMR durumu özel kilitleme durumudur:** Yalnızca bağlantı sıfırlama ve bağlantı kesme geçerlidir. Diğer tüm işlemler hata döner.

**UI ve TEST** bağlantı durumundan bağımsız, her durumda işlenebilir.

### Hata kodları

Ortak hata kodu tipi, tüm API fonksiyonlarından döner. Başarı=0, hatalar negatif. Kategoriler: başarı, çerçeve hataları (FCS, kısa frame), kaynak hataları (buffer/bellek), protokol hataları (sıra, geçersiz komut, FRMR), durum hataları (geçersiz işlem, desteklenmeyen mod), zamanlama hataları (max retry), parametre hataları (geçersiz girdi), akış kontrolü hataları (karşı taraf meşgul). Spesifik değerler ve granülarite implementasyon kararıdır.

### Olaylar

Asenkron bildirim mekanizması (callback, kuyruk, flag, polling — implementasyon kararı). Bildirilmesi gereken kategoriler:

- **Bağlantı yaşam döngüsü:** kurulma, kesilme, sıfırlanma
- **Akış kontrolü:** remote meşgullük başlangıcı/bitişi, pencerede yer açılması
- **Hatalar:** FRMR, max retry aşımı
- **Diagnostik:** TEST sonuçları

### Çerçeve yapısı (kavramsal)

Bir frame yapısı şunları taşır: adres, frame tipi (I/S/U), tipe göre kontrol bilgileri (I: N(S)/N(R)/P/F, S: fonksiyon/N(R)/P/F, U: fonksiyon/P/F), opsiyonel payload referansı ve uzunluğu. Tüm wire-format verileri oktet tipi cinsindendir.

### Konfigürasyon parametreleri

| Parametre | Açıklama |
|-----------|----------|
| mode | Çalışma modu (ilk sürüm: yalnızca ABM) |
| address | İstasyon adresi |
| window_size | Sliding window boyutu (mod-8: 1–7) |
| max_frame_size | Bilgi alanı max boyutu (MRU), oktet cinsinden |
| max_retries | N2, max yeniden iletim sayısı |
| t1_ms | Retransmission timer (tipik 200–3000 ms) |
| t2_ms | Ack delay timer (T1'den küçük olmalı) |
| t3_ms | Idle timer (tipik 10000–60000 ms) |
| use_extended | Mod-128 flag (ilk sürüm: false) |

### Platform entegrasyonu

Platforma bağımlı tüm işlemler kullanıcıya delege edilir. Mekanizma implementasyon kararıdır. Sağlanması gereken yetenekler:

**Fiziksel gönderim:** Encode edilmiş oktet dizisini fiziksel ortama iletme. Sonuç (başarı/hata) geri bildirilmelidir.

**Zamanlama:** T1, T2, T3 yönetimi için zamanlama bilgisi. Mekanizma: periyodik tick, platform timer callback, dışarıdan süre bildirimi veya başka yöntem — implementasyon kararıdır. Düşük güç sistemlerde periyodik tick derin uyku modlarını engeller; donanım timer'ları ile asenkron çalışma tercih edilmelidir. Kütüphane "bir sonraki zamanlayıcı dolumuna kalan süre" bilgisini sunabilmeli ki platform uyku süresini hesaplayabilsin.

**Olay bildirimi:** Durum değişiklikleri ve olayları üst katmana bildirme.

**Veri teslimi:** Doğrulanmış payload'u üst katmana iletme. Teslimat modeli §4'te tanımlıdır (kopyalamalı veya zero-copy).

**Kullanıcı context'i:** Platform yeteneklerinin uygulama context'ine erişim mekanizması.

### Timer gereksinimleri

| Timer | İşlev | Dolum davranışı |
|-------|-------|-----------------|
| T1 | Retransmission | Retry mekanizması tetiklenir |
| T2 | Ack delay | Standalone ack gönderilir |
| T3 | Idle/keep-alive | Link testi tetiklenir |

Başlatma, durdurma ve dolum tespiti platform sorumluluğundadır. Ek olarak "bir sonraki doluma kalan süre" sorgusu desteklenmelidir (tickless/düşük güç desteği).

### İstatistikler

Kategoriler: gönderim (I-frame sayısı, byte), alım (I-frame sayısı, byte), hata (FCS, FRMR, timeout), akış kontrolü (REJ/RNR gönderilen/alınan, local busy geçişleri), diagnostik (TEST gönderilen/başarılı/başarısız). Derleme zamanı seçeneği ile devre dışı bırakılabilir.

### TEST sonuç bilgisi

Başarı durumu (payload eşleşmesi), timeout olup olmadığı, test payload boyutu.

---

## 6. YETENEKLER VE DAVRANIŞ SÖZLEŞMELERİ

> Yeteneklerin fonksiyonlara nasıl eşlendiği, isimlendirme, parametre yapıları ve gruplamalar implementasyon kararıdır.

### 6.1. Frame yetenekleri

**Çerçeve oluşturma**
- Girdi: frame bilgisi + hedef buffer
- Çıktı: wire-format HDLC çerçevesi + yazılan byte sayısı
- Adres + kontrol + payload → FCS-16 → byte stuffing → flag'ler
- Oktet tipi cinsinden çalışır
- Stateless, yan etkisiz
- Hata: geçersiz parametre, buffer yetersiz

**Çerçeve ayrıştırma**
- Girdi: ham oktet dizisi
- Çıktı: frame bilgisi
- Destuffing → uzunluk kontrolü → FCS doğrulama → alan ayrıştırma
- Kontrol byte bit düzeninden frame tipi/alt tipi belirlenir
- Stateless, yan etkisiz
- Hata: FCS hatası, kısa çerçeve, geçersiz parametre

**FCS hesaplama**
- ITU-T CRC-CCITT (polinom 0x8408, başlangıç 0xFFFF, one's complement)
- Her oktetin yalnızca alt 8 biti kullanılır
- Lookup table veya bitwise: derleme zamanı seçimi

**FCS doğrulama**
- FCS dahil veri üzerinden hesaplama → sihirli sabitle karşılaştırma

### 6.2. Başlatma ve sıfırlama

**Başlatma**
- Station yapısını hazırlar, config/platform bilgisini kaydeder
- Buffer pointer'larını alır, tutarlılık kontrolü yapar (§4)
- Sayaçları sıfırlar, durumu başlangıca ayarlar
- Ön koşullar: mode==ABM, use_extended==false, window_size 1–7, platform yetenekleri sağlanmış, buffer'lar tutarlı
- Hata: desteklenmeyen mod, geçersiz parametre, tutarsız buffer

**Sıfırlama**
- Tüm zamanlayıcıları iptal, sayaçları/tx penceresini temizle, durum → başlangıç
- Olay bildirimi **yapmaz** (dahili reset, olay kararı çağırana ait)

### 6.3. Bağlantı yönetimi (U-frame alanı)

**Bağlantı kurma**
- Sayaçları sıfırlar → SABM(P=1) gönderir → kurulum bekleme → T1 başlatır
- Asenkron: UA geldiğinde veri alımı yeteneği bağlantıyı aktifleştirir
- Ön koşul: bağlantısız durumda
- Hata: yanlış durum

**Bağlantı kesme**
- DISC(P=1) gönderir → kesme onayı bekleme → T1 başlatır → UA gelince bağlantı düşer
- Ön koşul: aktif bağlantı, meşgul, reject VEYA FRMR durumunda
- Hata: yanlış durum

**Bağlantı sıfırlama**
- Dahili sıfırlama → yeniden bağlantı kurma (SABM)
- RESET olayı bildirir
- FRMR sonrası birincil kurtarma mekanizması
- FRMR durumunda: yalnızca bu yetenek veya bağlantı kesme geçerlidir, diğerleri hata döner

**Bağlantısız veri gönderimi**
- UI frame gönderir
- Bağlantı durumundan bağımsız
- Güvenilir değil (ack/retry yok)
- Hata: boyut aşımı

**Link diagnostiği (TEST)**
- TEST(P=1) gönderir, pattern saklar, T1 başlatır
- Yanıt gelince payload karşılaştırması → sonuç bildirilir
- Bağlantı durumundan bağımsız
- Pattern boş olabilir
- Aynı anda yalnızca bir TEST
- Hata: boyut aşımı, zaten bekleyen TEST var

### 6.4. Veri transferi ve akış kontrolü (I/S-frame alanı)

**Veri gönderimi**
- Payload → I-frame (N(S)=V(S), piggybacked N(R)=V(R))
- Tx penceresinde slot'a kaydet → encode → gönder
- V(S) ilerlet, T1 gerekirse başlat, T2 iptal et
- Ön koşul: aktif bağlantı, pencerede boş slot, karşı taraf meşgul değil
- Hata: yanlış durum, pencere dolu, karşı taraf meşgul, boyut aşımı

**Veri alımı**
- Ham oktet dizisini decode → adres kontrolü → T3 sıfırla → frame tipine göre dallan:

| Gelen frame | Davranış |
|-------------|----------|
| I-frame | N(R) ack işleme. N(S)==V(R) → payload teslim, V(R)++, T2 başlat. Sıra dışı → REJ gönder (reject durumunda değilse). P set → RR(F=1). Yerel meşgullük aktif → RNR yanıtla, payload teslim etme. |
| RR | Ack işleme. Meşgullük varsa temizle. P set → RR(F=1). |
| RNR | Ack işleme. Meşgullük set et. P set → RR(F=1). |
| REJ | Ack işleme. N(R)'den itibaren Go-Back-N yeniden gönderim. T1 restart. |
| SREJ | İlk sürümde geçersiz komut hatası **(future)**. |
| SABM | Sayaçlar sıfırla → UA(F=1) gönder → bağlantı aktif. |
| DISC | UA(F=1) gönder → station sıfırla → bağlantı düşer. |
| UA | Kurulum bekliyorsa → bağlantı aktif. Kesme onayı bekliyorsa → bağlantı düşer. Diğer → sessizce at. |
| DM | Station sıfırla → bağlantı düşer. |
| FRMR | Hata durumuna geç. Kurtarma üst katmana. |
| UI | Bağlantı durumundan bağımsız → payload teslim. |
| TEST | P=1 → aynı payload ile TEST(F=1) gönder. F=1 → bekleyen test varsa karşılaştır, sonuç bildir. |

**Dahili ack işleme** (tüm N(R) taşıyan frame'ler için ortak):
- N(R) geçerliliği kontrol (aralık dışı → FRMR gönder)
- N(R)'ye kadar tx slot'larını serbest bırak
- Yer açılırsa → olay bildir
- Tüm frame onaylandı → T1 iptal; hâlâ bekleyen var → T1 restart

FCS hatası veya kısa frame → frame atılır.

**Yerel meşgullük yönetimi**
- Kullanıcı yerel meşgullük bildirir (rx kuyruğu dolu, işlem gecikmesi vb.)
- Meşgullük set → gelen I-frame'lere RNR ile yanıt, karşı taraf gönderimini durdurur
- Meşgullük kaldır → RR gönder, karşı taraf devam eder

### 6.5. Zamanlayıcı dolumu işleme

Mekanizma (ayrı fonksiyon, tick içinde kontrol, vb.) implementasyon kararıdır.

| Timer | Dolum davranışı |
|-------|-----------------|
| T1 | retry_count++. Max retry aşıldı → hata bildirimi, bağlantı düşer. Aşılmadı → duruma göre: kurulum bekleme → SABM retry; kesme onayı bekleme → DISC retry; aktif bağlantı → checkpoint polling RR(P=1); meşgul → durum sorgusu. Bekleyen TEST → test başarısız. T1 restart. |
| T2 | Standalone RR gönder (piggybacking süresi doldu). |
| T3 | Keep-alive: RR(P=1) gönder → T1 başlat. |

### 6.6. Durum sorgulama

| Sorgu | Dönen bilgi | Yan etki |
|-------|-------------|----------|
| Bekleyen ack | Onaylanmamış alım var mı (piggybacking fırsatı) | Yok |
| Pencere durumu | Boş slot sayısı (0 = gönderilemez) | Yok |
| Bağlantı durumu | State machine durumu | Yok |
| İstatistikler | Tüm metriklerin kopyası | Yok |
| Sonraki timer dolumu | En yakın dolum süresine kalan ms (tickless/düşük güç için) | Yok |

---

## 7. TEST FRAME DAVRANIŞI

TEST, ISO/IEC 13239'da tanımlı U-frame. Bağlantı durumundan bağımsız.

**Gönderen taraf:**
1. TEST(P=1) gönder, pattern sakla, T1 başlat, test_pending set et.

**Alan taraf:**
1. TEST(P=1) al → aynı bilgi alanıyla TEST(F=1) gönder. Durum değişikliği yok.

**Yanıt işleme (gönderen):**
1. TEST(F=1) al → payload karşılaştır → sonuç bildir → T1 iptal → test_pending temizle.

**Timeout:**
1. T1 dolarsa + test_pending → test başarısız, timeout olarak bildir.

**Senaryolar:** Bağlantı öncesi fiziksel katman doğrulama, periyodik link kalitesi izleme, loopback testi.

---

## 8. HATA KURTARMA

| Hata | Davranış |
|------|----------|
| FCS hatası | Frame sessizce atılır. Karşı taraf T1 timeout ile retry yapar. |
| Kayıp frame | REJ gönderilir → Go-Back-N yeniden iletim. |
| Timeout | Retry sayacı artırılır → checkpoint polling. Max retry aşılırsa → hata bildirimi, bağlantı düşer. |
| FRMR | Station FRMR durumuna geçer. Yalnızca iki yol: bağlantı sıfırlama (SABM) veya bağlantı kesme (DISC). Diğer işlemler hata döner. |

---

## 9. ÇALIŞTIRMA MODELİ

**Temel kural:** Tüm station yetenekleri tek bir execution context'ten çağrılmalıdır (bare-metal: main loop, RTOS: tek task). Eş zamanlı çağrıya karşı koruma yoktur.

**ISR entegrasyonu:** ISR'ler doğrudan kütüphane fonksiyonlarını çağırmamalıdır. ISR → ara mekanizma (ring buffer, kuyruk, flag, DMA descriptor) → ana context → kütüphane çağrısı.

```
ISR (UART RX)  ──▸  Ring Buffer  ──▸  Ana Context  ──▸  Veri alımı
ISR (Timer)    ──▸  Flag/Event   ──▸  Ana Context  ──▸  Timer dolumu işleme
Ana Context    ──▸  Veri gönderimi  ──▸  Platform gönderim
```

Ara mekanizma detayları (boyut, yapı) kullanıcı sorumluluğundadır.

**Frame katmanı istisnası:** Stateless ve reentrant — ISR'den çağrılabilir (örn. ISR'de decode edip geçerli frame'leri kuyruğa yazmak). Station yetenekleri hiçbir koşulda ISR'den çağrılmamalıdır.

---

## 10. GENİŞLEME NOKTALARI

| Özellik | Etkilenen alan | Değişiklik |
|---------|---------------|------------|
| NRM | U-frame alanı | Primary/secondary rol, SNRM, polling mantığı. I/S-frame alanı değişmez. |
| ARM | U-frame alanı | NRM üzerine. Secondary poll beklemeden gönderir. SARM desteği. |
| Mod-128 | Frame + I/S-frame | use_extended aktif. Kontrol 2 byte, N(S)/N(R) 7 bit, pencere 127. SABME. |
| SREJ | I/S-frame alanı | Rx reorder buffer. Selective Repeat ARQ. |
| XID | U-frame alanı | Parametre müzakeresi. |
| Multi-byte adres | Frame | EA bit mekanizması. |
| FCS-32 | Frame | İkinci FCS yeteneği. Config'e fcs_mode. |

---

## 11. THREAD SAFETY

Station thread-safe değildir. Çalıştırma modeli §9'da tanımlıdır. Frame katmanı stateless/reentrant, eş zamanlı çağrılabilir.

---

## 12. DERLEME ZAMANI KONFİGÜRASYON

- FCS lookup table (hız vs ROM)
- İstatistik toplama (aktif/pasif)
- Dahili assert kontrolleri (debug/release)
