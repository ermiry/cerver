# Build Cerver-CMongo Docker Images

## Development

Build development image

```
sudo docker build -t ermiry/cerver-cmongo:development -f ./cmongo/Dockerfile.dev .
```

## Buildev

Build development builder image

```
sudo docker build -t ermiry/cerver-cmongo:builder -f ./cmongo/Dockerfile.builder .
```

## Builder

Build builder image

```
sudo docker build -t ermiry/cerver-cmongo:builder -f ./cmongo/Dockerfile.builder .
```

## Production

Build production image

```
sudo docker build -t ermiry/cerver-cmongo:latest -f ./cmongo/Dockerfile .
```