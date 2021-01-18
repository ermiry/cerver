# Build Cerver Docker Images

## Development

Build development image. Cerver sources, examples and tests are compiled using development flags. Examples and tests can be accessible without mounting any volume.

```
sudo docker build -t ermiry/cerver:development -f Dockerfile.dev .
```

## Testing

Build test image. Cerver sources and examples are compiled without development.

```
sudo docker build -t ermiry/cerver:test -f Dockerfile.test .
```

## Buildev

Build buildev image. Development cerver sources with gcc base.

```
sudo docker build -t ermiry/cerver:buildev -f Dockerfile.buildev .
```

## Builder

Build builder image. Production cerver sources with gcc base.

```
sudo docker build -t ermiry/cerver:builder -f Dockerfile.builder .
```

## Production

Build production image. Only cerver sources are compiled.

```
sudo docker build -t ermiry/cerver:latest -f Dockerfile .
```