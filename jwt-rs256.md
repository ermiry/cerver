# Generate RSA-256 Keys

0. Create a dedicated keys directory

```
mkdir keys
```

1. Run the following command to generate key pair and don't add passphrase

```
ssh-keygen -t rsa -b 4096 -m PEM -f ./keys/key.key
```

2. Format public key

```
openssl rsa -in ./keys/key.key -pubout -outform PEM -out ./keys/key.pub
```
