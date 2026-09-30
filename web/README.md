# MikroDMZ web sitesi

Bu proje, kendi sunucunuzda çalışacak şekilde hazırlanmış modern bir landing page örneğidir. Ana hedef, güvenli, teknik ve profesyonel bir görünüm elde etmektir.

## Dosyalar

- `index.html` - ana arayüz
- `styles.css` - tasarım ve responsive düzen
- `script.js` - küçük JS davranışları

## Yerel çalıştırma

Tarayıcıdan doğrudan açılabilir; ancak sunucu üzerinden çalıştırmak en doğru yöntemdir:

```bash
python -m http.server 8000
```

Ardından tarayıcıda şu adresi açın:

http://localhost:8000

## Kendi sunucuda yayınlama

1. Sunucuya dosyaları yükleyin.
2. Nginx veya Apache için site konfigürasyonunu oluşturun.
3. Alan adını sunucunuzun IP adresine yönlendirin.
4. HTTPS için Let's Encrypt kurulumunu yapın.

Örnek Nginx konfigürasyonu:

```nginx
server {
    listen 80;
    server_name example.com www.example.com;
    root /var/www/mikrodmz;
    index index.html;

    location / {
        try_files $uri $uri/ =404;
    }
}
```

HTTPS için certbot kullanabilirsiniz:

```bash
sudo apt install certbot python3-certbot-nginx
sudo certbot --nginx -d example.com -d www.example.com
```

## Not

Ticari marka veya mevcut site kopyası oluşturmak yerine özgün bir tasarım hazırlanmıştır. İsterseniz daha sonra bunu gerçek bir admin paneli, blog, iletişim formu veya çok sayfalı tasarıma dönüştürebiliriz.
