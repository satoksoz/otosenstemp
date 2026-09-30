# MikroDMZ Platform

Backend Express, WebSocket ve PostgreSQL kullanır. Yönetim arayüzü, kökteki
`web/` dizininden aynı sunucu tarafından sunulur.

## Yerel geliştirme

PostgreSQL'i `docker-compose.yml` ile başlatıp `.env.example` dosyasını `.env`
olarak kopyalayın. Ardından:

```bash
npm install
npm run dev
```

## Railway dağıtımı

Railway projesine PostgreSQL servisi ve repository kökünden çalışan bir servis
ekleyin. Kök dizindeki `Dockerfile` Node uygulamasını ve `web/` arayüzünü aynı
image'a alır. `railway.json` `/health` sağlık kontrolünü ayarlar.

Uygulama servisinde şu değişkenleri tanımlayın:

- `NODE_ENV=production`
- `DATABASE_URL`: PostgreSQL servisinin `DATABASE_URL` değişkenine Railway variable reference
- `JWT_SECRET`: en az 32 karakter, rastgele ve gizli değer
- `DEVICE_API_KEY`: en az 16 karakter, rastgele ve gizli değer
- `TUNNEL_TOKEN`: en az 16 karakter, rastgele ve gizli değer
- `ADMIN_EMAIL`: ilk yönetici e-posta adresi
- `ADMIN_PASSWORD`: en az 12 karakterlik ilk yönetici parolası
- `PUBLIC_BASE_URL`: uygulamanın `https://` ile başlayan Railway public domain'i
- `CORS_ORIGINS`: izin verilecek origin'ler; birden fazlaysa virgülle ayırın

`PORT` değerini elle sabitlemeyin; Railway sağlar. İlk açılış PostgreSQL'de
tabloları oluşturur ve ilk yönetici hesabını ekler. Sonraki dağıtımlarda yönetici
parolasını değiştirmek için uygulamanın kullanıcı yönetimi ekranını kullanın.

## Endpointler ve cihaz bağlantısı

- Sağlık kontrolü: `GET /health`
- Giriş: `POST /api/login`
- WebSocket: `/ws`
- Cihaz WebSocket tüneli: `/ws/device`
- Cihaz ağ geçidi: `/device/<cihaz-kimliği>/`

Railway domain'i HTTPS sağladığında cihaz firmware'inde `https://` ve `wss://`
adreslerini kullanın. Tünel adresi `/ws/device`, API adresi ise kök domain'dir.
Geliştirme token'larını veya örnek parolaları üretimde kullanmayın.

## Veri ve yerel PostgreSQL

Uygulama `DATABASE_URL` üzerinden PostgreSQL kullanır; SQLite dosyaları çalışma
zamanında okunmaz. Railway'de PostgreSQL servisine kalıcı veri için ayrıca
volume gerekmez. Yerel PostgreSQL için `npm run db:up` ve `npm run db:down`
komutları kullanılabilir.
