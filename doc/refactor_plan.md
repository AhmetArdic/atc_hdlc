# HDLC Refactor Planı (Linux Kernel LAPB Yaklaşımı)

## 1. Analiz: Mevcut Kod Neden Kalabalık?
Şu anki implementasyon, bir çekirdek (kernel) veya düşük seviye gömülü sistem kütüphanesinden ziyade aşırı soyutlanmış (over-abstracted) bir yapıya sahip. Temel sorunlar:
- **Gereksiz Abstraction (Soyutlama):** Frame (çerçeve) türleri (`ATC_HDLC_U_FRAME_TYPE_SABM`, `S_FRAME_TYPE_RR` vb.) çok fazla enum içine alınmış ve gelen her paket için gereksiz yere büyük struct'lar doldurulmaya çalışılıyor. Linux Kernel LAPB kodunda bunlar sadece bit-mask içeren hex makrolarıdır (ör. `#define HDLC_SABM 0x2F`) ve doğrudan değerlendirilir.
- **State Machine Şişkinliği:** Durum makinesi fonksiyonları aşırı dallanmış durumda. Gelen paketin tipine göre tekrar tekrar yazılan mantıklar kodu uzatıyor.
- **Rol ve Komut/Cevap Ayrımının Eksikliği (Future-Proof Değil):** Kod sadece ABM (Asynchronous Balanced Mode) üzerine inşa edilmiş. İleride NRM (Normal Response Mode) veya ARM (Asynchronous Response Mode) eklenebilmesi için istasyon rollerinin (Primary, Secondary, Combined) ve Command/Response paketi ayrımının mimarinin merkezinde olması gerekir.

## 2. Tasarım Prensipleri (Linus Torvalds Stili)
- **Minimalizm ve "Az Ama Öz" Kod:** Sadece gerçekten ihtiyaç duyulan değişkenler ve mantıklar kullanılacak. Mevcut kod base incelenerek gereksiz kısımlar refactor edilecek.
- **Doğrudan Bit Manipülasyonu:** Çerçeve kontrol baytını ayrıştırmak için devasa parser fonksiyonları yerine, 1-2 satırlık inline maskeleme mantığı kullanılacak.
- **Dışa Açık API vs. İç Yapı:** Kullanıcıya sunulan Public API (`inc/` altındaki dosyalar) yazarın imzası niteliğindeki `atc_hdlc_` önekini koruyacaktır. Ancak kütüphanenin iç C dosyalarında (`src/` altı) static fonksiyonlar ve iç işleyiş, kernel stilinde çok daha kısa, net ve öneksiz (`hdlc_state0_machine` vb.) olacak.
- **Spaghetti Koda Son:** Karmaşık handler fonksiyonları, state (durum) numaralarına göre lineer hale getirilecek.

## 3. Geleceğe Yönelik (Future-Proof) Mimari: NRM ve ARM Hazırlığı
NRM ve ARM destekleyebilmek için çerçeveler "Command" (Komut) veya "Response" (Cevap) olarak ikiye ayrılmalıdır.
- Primary istasyon Command gönderir, Response alır. 
- Secondary istasyon Command alır, Response gönderir.
- Combined istasyon (ABM) her ikisini de yapar.

**Planlanan Ekleme:** Context yapısına `role` (Primary/Secondary/Combined) eklenecek. Gelen paketin adresine ve kendi rolümüze bakarak paketin Command mı yoksa Response mu olduğunu anlayan tek bir `inline bool hdlc_is_cmd(cb, frame)` fonksiyonu yazılacak. Başlangıçta kod sadece ABM rolünü çalıştıracak olsa da, switch-case yapıları şimdiden "Komut geldiğinde" ve "Cevap geldiğinde" ne yapılacağını bilecek şekilde kurgulanacak.

## 4. Uygulama Planı (Adım Adım)

### Adım 1: Public API'ın Korunması ve Gereksiz Tiplerin Temizlenmesi
- `inc/hdlc_types.h` içindeki dışa açık API (`atc_hdlc_context_t`, `atc_hdlc_init` vb.) aynen korunacak.
- Ancak frame alt tiplerini belirten karmaşık enum yığınları silinecek. Bunun yerine C makroları (`#define ATC_HDLC_CMD_SABM 0x2F` vb.) getirilerek memory ve işlemci döngüsü israfı önlenecek.

### Adım 2: Çerçeve (Frame) Çözümlemenin Basitleştirilmesi
- `hdlc_resolve_frame_type` gibi ağır fonksiyonlar atılacak.
- Linux kernelindeki gibi gelen paketin kontrol baytı doğrudan `switch(ctrl & ~PF_BIT)` tarzı bir yapı ile en düşük seviyede okunup işlenecek.

### Adım 3: State Machine'in (Durum Makinesinin) Yeniden Yazılması
- `src/station/hdlc_frame_handlers.c` dosyası, modüler ve kernel stiline uygun olarak (örn: `hdlc_state1_machine`) yeniden yazılacak.
- Durum fonksiyonları basitleştirilecek:
  - `hdlc_state0_machine` (Disconnected)
  - `hdlc_state1_machine` (Connecting)
  - `hdlc_state2_machine` (Disconnecting)
  - `hdlc_state3_machine` (Connected)
  - `hdlc_state4_machine` (FRMR Error)

### Adım 4: Roller ve Mod Altyapısı (Command / Response Ayrımı)
- Adres baytı ve "Role" bazlı paket ayrıştırma mekanizması eklenecek.
- Alt sistem başlatılırken `role = ATC_HDLC_ROLE_COMBINED` olarak ayarlanacak. İleride kullanıcı `config->mode = ATC_HDLC_MODE_NRM` dediğinde, sadece `role = ATC_HDLC_ROLE_PRIMARY` (veya SECONDARY) yapmak sistemin doğru çalışması için yeterli bir altyapı sunacak.