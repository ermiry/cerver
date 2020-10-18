ssh-keygen -t rsa -b 4096 -m PEM -f ./keys/jwt-rs256.key
# Don't add passphrase
openssl rsa -in ./keys/jwt-rs256.key -pubout -outform PEM -out ./keys/jwt-rs256.key.pub
# cat jwt-rs256.key
# cat jwt-rs256.key.pub