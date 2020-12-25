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

## Production

Build production image. Only cerver sources are compiled.

```
sudo docker build -t ermiry/cerver:latest -f Dockerfile .
```