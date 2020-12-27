# Generate RSA-256 Keys

1. Run the following command to generate key pair and don't add passphrase

```
ssh-keygen -t rsa -b 4096 -m PEM -f ./keys/jwt-rs256.key
```

2. Format public key

```
openssl rsa -in ./keys/jwt-rs256.key -pubout -outform PEM -out ./keys/jwt-rs256.key.pub
```