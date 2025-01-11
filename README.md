# Akıllı Bitki Bakım Sistemi

Bu proje, ESP8266 (NodeMCU v3) kullanarak bitki bakımını otomatikleştirmek için tasarlanmıştır. Sistem, toprak nemi kontrolü, otomatik sulama, programlanabilir LED aydınlatma ve web tabanlı yönetim arayüzü sunar.

## Özellikler

- 🌱 Otomatik toprak nemi kontrolü ve sulama
- 💡 Programlanabilir LED aydınlatma sistemi
- 📱 Responsive web arayüzü
- 📊 Gerçek zamanlı durum izleme
- 📝 Olay kayıt sistemi
- ⚙️ Ayarlanabilir parametreler
- 📂 Dosya yönetim sistemi
- 🕒 NTP zaman senkronizasyonu
- 🔄 OTA (Havadan Güncelleme) desteği

## Donanım Gereksinimleri

- NodeMCU v3 (ESP8266)
- Kapasitif toprak nem sensörü
- 2x 5V röle modülü
- LED aydınlatma sistemi
- 5V güç kaynağı
- Jumper kablolar

## Bağlantı Şeması

- **Toprak Nem Sensörü:**
  - VCC → 3.3V
  - GND → GND
  - AOUT → A0

- **Sulama Rölesi:**
  - IN → D1
  - VCC → 5V
  - GND → GND

- **LED Aydınlatma Rölesi:**
  - IN → D2
  - VCC → 5V
  - GND → GND

## Yazılım Kurulumu

1. Arduino IDE'ye ESP8266 desteği ekleyin
2. Gerekli kütüphaneleri yükleyin:
   - ESP8266WiFi
   - ESP8266mDNS
   - ESP8266WebServer
   - ArduinoOTA
   - WiFiManager
   - LittleFS
   - ArduinoJson
   - TimeLib
   - NTPClient

3. LittleFS veri sistemi için ESP8266 Sketch Data Upload aracını yükleyin
4. Kodu NodeMCU'ya yükleyin
5. İlk çalıştırmada "BitkiSulama_AP" adlı WiFi ağına bağlanarak konfigürasyonu yapın

## Web Arayüzü Özellikleri

1. **Ana Sayfa (Durum)**
   - Toprak nemi yüzdesi
   - Sulama durumu
   - LED aydınlatma durumu
   - Görsel göstergeler

2. **Kontrol Paneli**
   - Manuel sulama kontrolü
   - Manuel LED kontrolü

3. **Ayarlar**
   - Nem eşik değeri
   - Sulama süresi
   - LED açılma/kapanma zamanları

4. **Kayıtlar**
   - Tarih bazlı filtreleme
   - Sistem olayları
   - Hata kayıtları

5. **Dosya Yöneticisi**
   - Sistem dosyalarını görüntüleme
   - Dosya düzenleme
   - Dosya yükleme/silme
   - Dosya yeniden adlandırma

## Güvenlik Notları

- Varsayılan WiFi yapılandırması kullanmayın
- Web arayüzüne erişim için güvenlik önlemleri alın
- Röle bağlantılarını dikkatli yapın

## Sorun Giderme

1. **Bağlantı Sorunları**
   - WiFi sinyalini kontrol edin
   - mDNS servisinin çalıştığından emin olun

2. **Sensör Sorunları**
   - Toprak nem sensörü kalibrasyonunu kontrol edin
   - Bağlantıları kontrol edin

3. **Sistem Sorunları**
   - Sistem loglarını kontrol edin
   - Dosya sisteminin düzgün çalıştığından emin olun

## Lisans

Bu proje MIT lisansı altında dağıtılmaktadır. Detaylar için `LICENSE` dosyasına bakın.
