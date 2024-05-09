# A HTTP Server with REST API 

## Create private key and certificate
```
openssl genrsa -out privkey.pem

openssl req -new -x509 -key privkey.pem -out cert.pem -days 3650
```

## Use
```
bin/sd-server -m ../models/sd_xl_base_1.0.safetensors --vae ../models/sdxl.vae.safetensors --host localhost --port 8080 --cert cert.pem --key private.pem
```