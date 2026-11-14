# cerver — Roadmap de la línea 3.0 (`labs`)

> El plan de trabajo de la línea de cerver con código 100 % propio: el JSON, el JWT, el parser HTTP, la arquitectura
> de I/O sobre io_uring, la API de aplicación, deflate, HTTP/2, TLS y WebSocket, escritos y medidos para ganarle a las
> mejores implementaciones que hay hoy.
>
> | | |
> |---|---|
> | **Fecha** | 2026-11-13 (L0, el plan de la línea) |
> | **Rama base** | `nightly` en la 2.1b-13 (`b8217b7b`); `labs-beta` desde `nightly-beta` en el tag `2.1b-13` (`a6f32f14`) |
> | **Ramas** | `labs` (desarrollo) y `labs-beta` (las betas 3.0b-N) |
> | **Estado** | L0 publicada (3.0b-1, PR #418); L0.1, la estructura (3.0b-2, §4.0.1), en implementación |

**Las citas `archivo:línea`:**

- son relativas a `src/cerver/` salvo que se indique otra cosa;
- corresponden a `nightly` en la 2.1b-13, con las rutas de antes de la 3.0b-2 (la tabla de §11 da las nuevas);
- cada fase mueve las líneas, así que se re-verifican antes de tocar cada zona.

---

## 1. Visión y principios

**Las líneas de cerver:**

| Línea | Ramas | Qué es |
|---|---|---|
| 1.x | `core` / `beta` | El core sin HTTP |
| 2.0.x | `http` / `http-beta` | La línea estable, la que corre en producción |
| 2.1.x | `nightly` / `nightly-beta` | El event loop, keep-alive, pools, WebSocket, TLS y HTTP/2. Sale cuando está completamente validada |
| **3.0** | **`labs` / `labs-beta`** | **Todo el código de nuestra autoría.** No sale hasta llegar a su meta final (§2) |

**`labs` es el laboratorio de cerver.** Puede romper la API, la ABI y la arquitectura cuando el cambio lleva a un
mejor diseño; cada ruptura va a `incompatibilities.md` con la beta que la trae.

**Los principios de cerver se conservan:** ligero, enfocado al rendimiento y personalizable. Se suman:

1. **El código es nuestro.** Los módulos que vinieron de otros proyectos se reemplazan por implementaciones propias:
   - el JSON de Jansson;
   - el JWT de libjwt;
   - el parser de llhttp;
   - el SHA-256 de sha-2 y el base64 de fastbase64 (`utils/`);
   - lo que hoy se toma de OpenSSL que no es criptografía (SHA-1, la limpieza de memoria) y de zlib (deflate).

   Tampoco se agregan dependencias nuevas: io_uring se usa con syscalls propias, sin liburing.
2. **La criptografía no se escribe.** RSA, ECDSA, EdDSA, HMAC y TLS van detrás de una interfaz propia
   (`cerver/crypto`), con OpenSSL como proveedor por default e intercambiable (BoringSSL, libsodium, PKCS#11). Los
   bytes aleatorios salen del CSPRNG del kernel (`getrandom ()`, como ya hace `sessions.c:46`), no de un generador
   propio. Escribir criptografía de clave pública o un TLS propio es la forma más rápida de perder el «más seguro».
3. **Ganar se mide, no se promete.** Cada módulo se compara contra las mejores implementaciones de la industria (§5)
   en x86_64 y arm64, con un método reproducible. Un módulo propio entra cuando no pierde contra el que reemplaza, y
   la 3.0 sale cuando les gana a todos sus competidores.
4. **Seguro por construcción.** Cada parser es estricto por default, y lo lenient se pide de forma explícita y
   acotada. Cada módulo pasa:
   - las suites de conformidad públicas;
   - fuzz diferencial contra la implementación que reemplaza;
   - mutantes;
   - inyección de fallos.
5. **Personalizable sin costo.** Cada módulo recibe su allocator (una arena por petición o por documento) y nunca
   llama a `malloc ()` en el camino caliente; los límites son configurables con defaults seguros; SIMD siempre con una
   versión escalar equivalente.
6. **Igual desde C y desde Python.** La API se diseña para que pycerver la ligue sin copias ni callbacks por
   petición; pycerver 2.0 acompaña a la 3.0 (§8).

## 2. La meta final: cuándo sale la 3.0

La 3.0 sale de `labs` a `master` («Release 3.0») cuando se cumplen las cinco condiciones:

1. **`thirdparty.md` ya no existe.** `src/` e `include/` no tienen código ajeno. Las dependencias externas son solo:
   - libc y pthreads;
   - el proveedor de criptografía;
   - zlib, opcional con un flag de build, hasta que deflate sea propio (L6).
2. **Cada módulo le gana a sus competidores** (§5) en x86_64 y en arm64, en throughput, latencia (p99 y p99.9),
   memoria y asignaciones, medidos en la máquina oficial.
3. **La conformidad completa:**
   - JSON: RFC 8259 y JSONTestSuite.
   - JWT: RFC 7515, 7518, 7519, 7520 y 8037, y Wycheproof a través del proveedor.
   - HTTP/1.1: RFC 9110 y 9112, con las suites de request smuggling.
   - HTTP/2: RFC 9113 y 7541 (h2spec).
   - WebSocket: RFC 6455 y 7692, con autobahn 517 de 517.
4. **El gate de `labs`** (§6) en verde sobre el release.
5. **pycerver 2.0** con toda la API, y aplicaciones reales validadas con la 3.0 fuera de este repositorio (§6).

**El orden con las otras líneas:** la 3.0 llega a `master` después de la 2.1. Antes de su PR, `labs` registra la
historia de `master` con `git merge -s ours origin/master` (como la 2.0 con la 1.x). Después de la 3.0, la línea 2.x
sigue con arreglos (2.1.N) desde `http`.

---

## 3. Lo que hay hoy (la base, 2.1b-13)

**El código ajeno** (`thirdparty.md`):

| Módulo | Origen | Licencia | Líneas `.c` | Quién lo usa |
|---|---|---|---|---|
| `http/json/` | Jansson 2.13 | MIT | 5 809 | `http/auth.c`, `http/admin.c`, `http/http.c`, `http/response.c`, `http/tree.c`, `http/jwt/jwt.c` |
| `http/jwt/` (salvo `key.c`) | libjwt 1.12 | MPL 2.0 | 2 182 | `http/auth.c`, `http/http.c` |
| `http/parser/` | llhttp 9.4.3, generado por llparse | MIT | 11 266 | `http/http.c`, `http/stream.c`, `http/access.c`, `http/websockets/client.c` |
| `utils/sha256.c` | sha-2 (Alain Mosnier) | Unlicense o 0BSD | 268 | `sessions.c`, `game/lobby.c` |
| `utils/base64.c` | fastbase64 (modp_b64, base64simd y aklomp/base64) | BSD 2-Clause | 763 | `http/jwt/jwt.c`, `http/websockets/handshake.c`, `http/http.c` |

**sha-2 y fastbase64 no estaban en `thirdparty.md`** hasta la 3.0b-1: se encontraron al buscar atribuciones en
`src/` e `include/` (sus comentarios de origen y los nombres `chromium_base64_*` y `MODP_B64_ERROR`). Antes de dar por
cerrada L6, una revisión de origen de todo el árbol (los nombres heredados, como el `bsem` de los semáforos binarios,
y los archivos sin historia propia) confirma que no queda nada más.

**OpenSSL y zlib fuera de `jwt/`:**

| Uso | Dónde | Qué lo reemplaza |
|---|---|---|
| `RAND_bytes ()`, el id de una petición | `http/request.c:315` | `getrandom ()` detrás de `cerver/crypto` (L0) |
| `RAND_bytes ()`, la llave del handshake del cliente | `http/websockets/handshake.c:77` | Ídem |
| `RAND_bytes ()`, las máscaras de los frames | `http/websockets/websocket.c:422` | Ídem |
| `EVP_Digest (… EVP_sha1 ())`, el accept del handshake | `http/websockets/handshake.c:105` | `cerver/crypto` con el proveedor (L0), y un SHA-1 propio (L6) |
| `OPENSSL_cleanse ()`, el material de una llave | `http/http.c:690` | `cerver/crypto` (L0) |
| Las llaves y firmas del JWT | `http/jwt/key.c`, `http/jwt/jwt.c` | `cerver/crypto` con el proveedor (L2) |
| zlib, gzip de las respuestas | `http/compression.c` | Deflate propio (L6) |
| zlib, permessage-deflate | `http/websockets/deflate.c` | Ídem |

`http/http.c:20-21` incluye `openssl/sha.h` y `openssl/crypto.h`, pero de ellos solo usa `OPENSSL_cleanse ()`.

**Lo que ya es propio y se reusa:**

- el base64url canónico de `utils/base64.c` (2.0b-67, sobre el código de fastbase64);
- `jwt/key.c` (las llaves contra el algoritmo al arrancar, desde la 2.0b-67);
- `collections/arena.c`, `collections/freelist.c` y `collections/pool.c`;
- el `getrandom ()` de `sessions.c:46`;
- WebSocket completo (`http/websockets/`, F6a y la 2.1b-13);
- la lista de cabeceras y la respuesta estructurada de F1 (`HttpHeaders`, `HttpResponseMessage`).

**Lo que asigna una petición** (`test/performance.sh`, la 2.1b-13): GET 0; POST JSON 14, todas del `json_loads ()` del
handler; un GET con dos parámetros de query, 15; un multipart con un campo y un archivo, 45.

---

## 4. Fases

**Cada fase es al menos una beta 3.0b-N** de `labs` → `labs-beta`, y cierra con:

1. gcc con `TYPE=development|test|beta|production`, `CC=g++` y clang, sin warnings con `WERROR=1`, sobre `gcc:16.2`
   / `debian:trixie-slim`;
2. el gate de `labs` en verde (§6);
3. sus benchmarks contra los competidores (§5), con los resultados en `benchmarks/competitors/results/`;
4. los bindings de pycerver al día cuando la fase cambia la API (§8);
5. el commit dedicado «Updated version to Beta 3.0b-N» con su `CHANGELOG.md`;
6. el estado al día en este roadmap.

**Cada fase lleva su plan aprobado** antes de implementarse. Los números de las betas son tentativos: una fase grande
puede ocupar varias.

| Fase | Beta | Contenido | Prerrequisito |
|---|---|---|---|
| **L0** | 3.0b-1 | La línea, Apache-2.0, `cerver/crypto`, la inyección de fallos, el arnés de competidores | 2.1b-13 publicada |
| **L0.1** | 3.0b-2 | La estructura: subcarpetas, archivos partidos, headers internos en `src/`, paraguas | L0 |
| **L0.2** | 3.0b-3 | La documentación de los headers: Doxygen con sintaxis Javadoc y su gate | L0.1 |
| **L1** | 3.0b-4 | El JSON propio | L0.2 |
| **L2** | 3.0b-5 | El JWT propio | L1 |
| **L3** | 3.0b-6 | El parser HTTP/1.1 propio | L0.2 |
| **L4** | 3.0b-7 | La arquitectura de I/O: io_uring, epoll de respaldo, thread-per-core | L3 |
| **L5** | 3.0b-8 | La API de aplicación sobre el I/O nuevo | L4 |
| **L6** | 3.0b-9 | SHA-1, SHA-256, base64 y deflate propios | L0.2 |
| **L7** | 3.0b-10 | HPACK y HTTP/2 | L4, L5 |
| **L8** | 3.0b-11 | TLS con el proveedor, sobre el ring | L4 |
| **L9** | 3.0b-12 | WebSocket sobre el I/O nuevo | L4, L5 |
| **macOS** | con L4 | kqueue, `.dylib`, el CI en `macos-latest` | L4 |
| **Release** | → 3.0 | §2 | 2.1 en `master` |

**Fuera de la meta:** la implementación mayor de game (los `GameSettings` de la aplicación, un lobby sobre el loop)
pasa a una 3.x. La auth asíncrona de F7 llega por la API de L5 (un cliente HTTP propio sobre el ring en lugar de
`curl_multi`), en el plan de esa fase.

### 4.0 L0 — La línea (3.0b-1)

1. **Las ramas:**
   - `labs` nace de la punta de `nightly` (la 2.1b-13, `b8217b7b`);
   - `labs-beta`, de la punta de `nightly-beta` (`a6f32f14`, tag `2.1b-13`);
   - así `labs-beta` tiene a `labs` como ancestro, y el PR de la 3.0b-1 solo trae lo suyo.
2. **Sin merges entre líneas:** el `.gitattributes` pierde las seis líneas `merge=ours` de `nightly` y conserva la de
   `test/fuzz/corpus/** -text`. La sincronía es por ports a mano (§8).
3. **Los workflows (§7):**
   - el nombre y la imagen de `labs-beta` en `pre-release.yml` y `beta.yml`;
   - la imagen `labs` en `build.yml`;
   - el gate de `labs` en `scheduled.yml`.
4. **La licencia:**
   - `LICENSE.md` con el texto de Apache-2.0;
   - un `NOTICE` con el copyright de cerver y la referencia a `thirdparty.md`;
   - `thirdparty.md` sigue mientras haya código ajeno: L1, L2 y L3 le quitan cada una su fila;
   - el MIT de Jansson y llhttp y el MPL 2.0 de libjwt (por archivo) son compatibles con Apache-2.0 mientras sus
     archivos estén en el árbol.
5. **`cerver/crypto`, la frontera con el proveedor:**
   - `include/cerver/crypto/random.h` y `src/cerver/crypto/random.c`: bytes aleatorios desde `getrandom ()`, con el
     ciclo de `sessions.c:46` movido aquí. `sessions.c`, `http/request.c:315`, `handshake.c:77` y
     `websocket.c:422` lo usan;
   - `include/cerver/crypto/hash.h` y `src/cerver/crypto/hash.c`: SHA-1 y la limpieza de memoria;
   - `src/cerver/crypto/openssl.c`: el proveedor, con `EVP_Digest ()`; la limpieza, `explicit_bzero ()`;
   - `handshake.c:105` y `http/http.c:690` lo usan, y `http/http.c` deja de incluir OpenSSL;
   - el proveedor se elige al compilar (`CRYPTO=openssl` en el `makefile`, el único por ahora);
   - **los nombres llevan el prefijo `cerver_crypto_`**, para no chocar con los `crypto_*` de libsodium si un día es
     el proveedor;
   - `http/jwt/` sigue con OpenSSL directo hasta L2.
6. **La inyección de fallos en los tests** (`test/faults/`, el binario `test/bin/faults`):
   - `test/faults/faults.c` define `malloc`, `calloc`, `realloc` (sobre los `__libc_*` de glibc) y `pthread_create`
     (sobre el siguiente, `dlsym (RTLD_NEXT)`) en el binario de la suite: las llamadas de `libcerver.so` se resuelven
     a ellos, sin `LD_PRELOAD`;
   - un test arma la siguiente llamada de una función para que falle (`faults_arm ()`), en cualquier hilo o solo en
     el suyo, y en una asignación, solo la de un tamaño;
   - no se compila con un `SANITIZER` (reemplazan el allocator) ni corre con el `RUNNER` de `test/run.sh` (valgrind
     también lo reemplaza);
   - **los primeros tests y sus mutantes:** C14 (los frames que llegaron con el 101, sin memoria) y C8 (el hilo de un
     cliente que no se crea) mueren con su test; S4 (el hilo de una conexión del servidor que no se crea) prueba que
     la conexión termina, devuelve su lugar y no deja fds, y su mutante «sin el `CLOSED`» queda vivo: solo se nota
     con un envío de otro hilo a la vez, una carrera.
7. **El arnés de competidores** (§5):
   - `Dockerfile.competitors` compila a cada competidor desde su fuente en una versión fija (sus `ENV`: yyjson
     0.13.0, simdjson 5.1.1, Jansson 2.15.1, RapidJSON `297637bc`, llhttp 9.4.4, picohttpparser `dd573d8f`);
   - `benchmarks/competitors/build.sh` arma un binario por implementación con el driver de su módulo y `measure.c`
     (la mediana de 5 corridas, las asignaciones con su propio `malloc ()`, el RSS);
   - `run.sh` escribe `results/<arquitectura>.txt`. **La primera corrida** (aarch64, la Mac con otra carga, no
     cuenta como oficial): el JSON de cerver lee a 79–139 MB/s, yyjson a 2.1–3.8 GB/s y simdjson a 1.7–2.7 GB/s, con
     0–2 asignaciones contra 54–65 553; el parser de cerver iguala a llhttp 9.4.4 y picohttpparser hace de 1.5 a 3
     veces más peticiones por segundo. Es la línea base de L1 y L3.
8. **Este roadmap** reemplaza al de `nightly` en `labs`; `CLAUDE.md` y el README describen la línea.
9. **«Updated version to Beta 3.0b-1»**, con `incompatibilities.md` «3.0b-1 (from 2.1b-13)»: la licencia, `cerver/crypto`
   y los símbolos que cambian de lugar.
10. **pycerver:**
    - la rama `labs` de py-cerver nace de su `nightly` cuando esa rama tenga commits, con la versión 2.0b1, solo
      como tag de GitHub;
    - si todavía no los tiene, espera a L1: la 3.0b-1 no cambia nada que pycerver use.

### 4.0.1 L0.1 — La estructura (3.0b-2)

Antes de que L1–L9 agreguen archivos, el árbol se ordena por dominio. **El único cambio de símbolos** es que las
funciones con `_by_` pasan a `_with_` (`client_get_with_sock_fd ()`, `http_headers_find_with_name ()`, …); fuera de
eso `nm -D` de `libcerver.so` es el de la 3.0b-1. La 1.9b-3 de `core` adopta después la parte del core tal cual,
para que los ports entre líneas sigan siendo directos.

1. **Las carpetas** (archivos de una palabra; la tabla completa en §11):
   - arriba, solo `config.h`, `version.h` y el paraguas `cerver.h`;
   - el core: `core/` (el `Cerver`), `admin/`, `auth/` (la autenticación, las conexiones *on hold* y las sesiones),
     `client/`, `connection/`, `files/`, `handler/`, `load/` (el balancer), `network/`, `packets/` y `system/`, más
     `collections/`, `crypto/`, `game/` (con `types` en lugar de `gametype`), `threads/`, `types/` y `utils/`;
   - HTTP: `http/core/` (el `HttpCerver` y lo transversal), `http/headers/`, `http/requests/`, `http/responses/`,
     `http/routes/` y `http/security/`, más `json/`, `jwt/`, `parser/` y `websockets/`, que no cambian.
2. **Los archivos grandes se parten por sus regiones,** el `.c` y el `.h`: `cerver` (en `core/`), `http` (en
   `http/core/`), `admin`, `auth`, `client`, `connection`, `files`, `balancer` (en `load/`), `packets` y `headers`
   (en `http/headers/`). El header principal de cada módulo tiene sus tipos y sus partes lo incluyen; los includes
   van siempre al inicio de cada archivo, así que el header principal no incluye a sus partes y cada archivo incluye
   las partes que usa. Una función `static` que pasa a usarse desde otra parte se declara `CERVER_HIDDEN` en el
   `internal.h` de su carpeta.
3. **Los paraguas:** `<cerver/cerver.h>` incluye todo el core y `<cerver/http/http.h>` todo HTTP, cada parte
   incluida; no declaran nada.
4. **Lo interno vive en `src/`, junto a su `.c`** (el modelo de curl y libuv); `include/` solo tiene lo que se instala:
   - los headers que solo declaran funciones `CERVER_PRIVATE` (`access`, `recycle`, `compatibility`) se mueven a
     `src/` con su nombre;
   - las declaraciones internas de un header público pasan al `internal.h` de su carpeta en `src/`;
   - un header interno nunca tiene la misma ruta relativa que uno público (`-I include -I src`);
   - `http/json/` y `http/parser/` se quedan como están: L1 y L3 los borran, y `HttpReceive` embebe el `llhttp_t`.
5. **Los includes:** dentro de la librería (`include/` y `src/`) con comillas, `"cerver/..."`, como antes; los
   tests, los examples y los benchmarks, que la usan como un programa, con `<cerver/...>` (y comillas para los
   internos que leen). Al inicio de cada archivo, en bloques separados por una línea en blanco y en orden alfabético
   por la ruta dentro de cada bloque: la biblioteca estándar de C; el sistema (`pthread.h`, `sys/*`, …); las
   bibliotecas especializadas (OpenSSL, zlib, curl); `cerver/types/`; `cerver/collections/`; los archivos de arriba
   (`config.h`, `version.h`); el core de cerver; un bloque por paquete (`crypto`, `game`, `http` con todas sus
   carpetas, `threads`); `cerver/utils/`; y al final lo local de los tests y los examples.
6. **Los tests en espejo de `src/`:** los unit tests sueltos pasan a la carpeta de su módulo, con un binario por carpeta
   (`test/bin/network` corre `network`, `socket` y `udp`); `test/http/` en subcarpetas con un solo `test/bin/http`;
   `test/version.c` sigue en la raíz, como `version.h`. Los pares de integración, la app, los fuzzers, los examples y
   los benchmarks solo cambian sus includes.
7. **Lo que no cambia:** los nombres de los símbolos salvo `_by_` → `_with_` (el prefijo es por módulo, §10) y los
   nombres de archivo con abreviaturas, que se renombran con su módulo.
8. **El gate:** el de `labs` completo (§6), más `nm -D` igual al de la 3.0b-1 salvo los renombres; `make install`
   en un contenedor limpio y un programa que solo incluye los paraguas, sin `-I src`; cada header instalado compila
   solo; ningún header interno instalado; guards únicos; pycerver `labs` con las rutas y los nombres nuevos.
9. **pycerver:** la rama `labs` de py-cerver nace de su `nightly` (1.2b-1) con la 2.0b1.

### 4.0.2 L0.2 — La documentación de los headers (3.0b-3)

- **Doxygen con sintaxis Javadoc** en los headers públicos: `/** */`, `@brief`, `@param[in|out]`, `@return`,
  `@retval`, `@note`, `@warning`, `@since`, `@deprecated`, y el alias `@threadsafety` para el contrato de hilos.
- **Qué se documenta:** la API estable completa (core, collections, threads, utils, crypto, websockets, game); lo que
  L1–L5 reemplazan nace documentado en su fase.
- Los `.c` siguen con `//` para el porqué; los headers internos con `@internal`; en los headers, `#pragma region`
  pasa a grupos de Doxygen (`@defgroup`, `@{ @}`). En inglés.
- Un `Doxyfile`, `make docs`, y la guía de comentarios en `CLAUDE.md`.
- **El gate:** Doxygen sin warnings (`WARN_AS_ERROR = FAIL_ON_WARNINGS`, `WARN_NO_PARAMDOC = YES`) y `clang
  -Wdocumentation -Wdocumentation-unknown-command` en el job de clang con `WERROR=1`. Desde esta beta, un símbolo
  público sin documentar no entra.
- **La referencia** se genera en el CI como artifact y se publica con el release 3.0. El wiki (`docs/`) no se toca
  hasta el release.

### 4.1 L1 — El JSON propio

- **El modelo:**
  - un DOM inmutable construido en una arena por documento, con el presupuesto de memoria de la 2.0b-67 por hilo;
  - un lector por streaming (eventos) para los bodies grandes;
  - un escritor que serializa a un búfer del caller o a la respuesta, sin un `malloc ()` por valor.
- **La lectura:**
  - escaneo de estructura con SIMD (SSE4.2 / AVX2, AVX-512 opcional, NEON) y una versión escalar con el mismo
    resultado;
  - UTF-8 validado;
  - números exactos sin locale: enteros de 64 bits y dobles con la conversión correcta de ida y vuelta;
  - profundidad, tamaño y cantidad de claves con límites.
- **La API:**
  - con prefijo de cerver (los `json_*` de Jansson chocan con la libjansson del sistema);
  - las consultas por puntero (RFC 6901);
  - el JSON entra al core (#207) solo si la 1.9 lo pide, desde aquí por port.
- **Quién cambia:** `http/auth.c`, `http/admin.c`, `http/http.c`, `http/response.c`, `http/tree.c`, los handlers de
  los examples y de los tests, y pycerver.
- **Las pruebas:**
  - JSONTestSuite (los `y_`, `n_` e `i_` con su decisión documentada);
  - fuzz diferencial contra la libjansson del sistema en `Dockerfile.fuzz` (el mismo árbol o el mismo error);
  - la ida y vuelta de los números.
- **La meta medible:** POST JSON con 0 asignaciones de `malloc ()` por petición, y le gana a yyjson y a simdjson en
  lectura y escritura.
- **Al cerrar:** se borran `include/cerver/http/json/` y `src/cerver/http/json/`, y su fila de `thirdparty.md`.

### 4.2 L2 — El JWT propio

- JWS compacto (RFC 7515):
  - el header y los claims con el JSON de L1;
  - base64url de `utils/base64.c`;
  - las firmas a través de `cerver/crypto`, con `jwt/key.c` llevado a esa interfaz.
- **Los algoritmos:**
  - HS256/384/512, RS256/384/512, PS256/384/512, ES256/384/512 y EdDSA (RFC 8037);
  - la lista permitida es por cerver;
  - `none` nunca se acepta;
  - la llave decide el algoritmo, no el header.
- **Los claims** (RFC 7519): `exp`, `nbf` e `iat` con su tolerancia, `aud`, `iss` y `sub`, más los claims de la
  aplicación sin copias.
- **Las pruebas:**
  - los ejemplos de RFC 7520;
  - Wycheproof a través del proveedor;
  - fuzz del parser del token;
  - fuzz diferencial contra la libjwt del sistema.
- **Contra quién:** l8w8jwt, libjwt y jwt-cpp, en firmar y en verificar.
- **Al cerrar:** se quita libjwt (`jwt.c`, `jwt.h`, la tabla de algoritmos) y su fila de `thirdparty.md`.

### 4.3 L3 — El parser HTTP/1.1 propio

- **Push-based e incremental,** como llhttp: acepta la petición partida en cualquier byte (lo exige
  `test/http/receive.c`).
- **Estricto por default (RFC 9112):**
  - solo `HTTP/1.0` y `1.1`;
  - `Transfer-Encoding` con `chunked` al final;
  - sin `Content-Length` y `Transfer-Encoding` a la vez;
  - sin espacios antes de `:`;
  - sin NUL ni CR sueltos;
  - límites de línea, de cabeceras, de chunks y de trailers.

  Lo lenient es un flag por caso, nunca uno global.
- **Sin copias:** el parser entrega vistas (`const char *` + largo) sobre el búfer de recepción; la lista de cabeceras
  de F1 guarda esas vistas.
- **SIMD** para buscar los delimitadores (como picohttpparser con SSE4.2 y AVX2, más NEON), con una versión escalar.
- **Las pruebas:**
  - el corpus de los fuzzers `request`, `lenient` y `pipeline`;
  - fuzz diferencial contra llhttp y picohttpparser;
  - las suites de request smuggling (CL.TE, TE.CL, TE.TE, las obs-fold, los espacios);
  - la matriz de `test/http/disconnect.c`.
- **El bug de llhttp 9.4.3** (el bucle infinito con un NUL bajo `LENIENT_HEADER_VALUE_RELAXED`) es un caso de la suite
  desde el día uno.
- **Al cerrar:** se borran `include/cerver/http/parser/` y `src/cerver/http/parser/` y su fila de `thirdparty.md`.

### 4.4 L4 — La arquitectura de I/O

- **Thread-per-core:**
  - un loop por núcleo, sin estado compartido entre loops;
  - cada uno con su listen fd (`SO_REUSEPORT`) o con el accept repartido;
  - una conexión vive y muere en su loop.
- **io_uring con syscalls propias:**
  - `io_uring_setup`, `io_uring_enter` e `io_uring_register`, más las barreras de memoria de los anillos, sin
    liburing;
  - accept y recv multishot, buffer rings provistos, archivos registrados;
  - `send` zero-copy para respuestas grandes;
  - timeouts del ring;
  - el kernel mínimo (propuesta: 6.1 LTS) se fija en el plan de la fase.
- **epoll de respaldo** detrás de la misma abstracción, para kernels sin io_uring o con io_uring bloqueado por
  seccomp. kqueue en macOS (con L4, §4.10).
- **Una rueda de timers** por loop y **una máquina de estados** por conexión: el header, el body, el handler, el
  envío, idle con keep-alive y pipelining en orden.
- **El trabajo bloqueante** (los handlers que hacen I/O, la auth custom) corre en pools nombrados, fuera del loop. Las
  respuestas vuelven al loop por una cola sin locks.
- **Las pruebas:**
  - la suite de aceptación de `test/http/server.c` (408, slowloris, idle, slow body, overload, drain, EINTR, sin fds);
  - los pares web en io_uring y en epoll;
  - nginx con keep-alive y reinicios bajo carga;
  - la inyección de fallos de las syscalls (una CQE con error, un ring lleno, `ENOBUFS`).
- **Lo de `nightly`:** F3–F5 dejan lecciones y tests que se reusan, pero el diseño de L4 es propio. **Riesgo:** el
  perfil seccomp por default de Docker bloquea io_uring; el plan de la fase define el perfil de las imágenes de test y
  el aviso al arrancar.

### 4.5 L5 — La API de aplicación

- El handler, `HttpRequest` y la respuesta sobre el I/O nuevo, con la petición en una arena:
  - los parámetros, las cabeceras y el body como vistas;
  - el multipart en streaming;
  - la respuesta escrita directo al búfer de envío.
- **Las rutas:** solo la API de grupos; se quitan la API vieja (`http_route_*`) y THREADS para web.
- **Lo que hoy se configura por cerver** (límites, CORS, compresión, ETag, auth) se puede fijar por grupo y por
  endpoint, con defaults seguros.
- **Un cliente HTTP propio** (1.1, y 2 desde L7) sobre el mismo loop, que reemplaza a curl en los tests y los
  benchmarks y es la base de la auth asíncrona.
- **pycerver 2.0bN se rehace aquí:**
  - los handlers sin un `CFUNCTYPE` por petición;
  - las vistas expuestas como `memoryview`, sin copias.

### 4.6 L6 — SHA-1, SHA-256, base64 y deflate propios

- **SHA-1 y SHA-256 propios** (FIPS 180-4): SHA-1 solo para el accept de WebSocket (RFC 6455), SHA-256 para los ids
  de sesión y los lobbies; los vectores del NIST y el proveedor como oráculo en los tests. Se quita `utils/sha256.c`
  (sha-2).
- **base64 y base64url propios** (RFC 4648), escalar y SIMD (AVX2, NEON), con el base64url canónico de la 2.0b-67
  y fuzz diferencial contra el código actual. Se quita el código de fastbase64 de `utils/base64.c`.
- **Inflate y deflate propios (RFC 1951)**, con gzip (RFC 1952) para las respuestas y permessage-deflate (RFC 7692)
  para WebSocket:
  - con ventana y contexto por conexión;
  - inflate con límite de salida contra las bombas.
- **Las pruebas:** fuzz diferencial contra zlib, en las dos direcciones; los casos de autobahn 12.x y 13.x.
- **Contra quién:** zlib, zlib-ng y libdeflate.
- **Al cerrar:** zlib deja de ser una dependencia, y las filas de sha-2 y de fastbase64 salen de `thirdparty.md`.
- **Contra quién**, también: el base64 de aklomp y el de simdutf; el SHA del proveedor.

### 4.7 L7 — HPACK y HTTP/2

- HPACK (RFC 7541) con Huffman, y frames y streams (RFC 9113) sobre L4.
- Las prioridades de RFC 9218 y WebSocket sobre h2 (RFC 8441).
- **Las defensas:** rapid reset, CONTINUATION flood, la bomba de HPACK, los floods de PING y SETTINGS.
- Lo que `nightly` haga en F9a se reusa si ya está publicado.
- **Las pruebas:** h2spec, fuzz de frames y de HPACK, fuzz diferencial de HPACK contra nghttp2 en la imagen de fuzz.

### 4.8 L8 — TLS

- TLS 1.2 mínimo y 1.3 (RFC 9325) con el proveedor, con SNI, ALPN (h2 y http/1.1), mTLS y recarga sin reinicio.
- Sobre el ring: los handshakes en el loop, y kTLS para que el kernel cifre los envíos y `send` zero-copy siga
  funcionando.
- Lo que `nightly` diseñó en F8 se reusa.

### 4.9 L9 — WebSocket sobre el I/O nuevo

- El protocolo y el cliente de F6a y la 2.1b-13, sin un hilo por conexión.
- La cola de salida acotada, escrita por el loop, con backpressure (A5 de la revisión de la 2.1b-10: `on_drain`, el
  tamaño encolado, la política de desborde).
- El deflate propio de L6.
- Autobahn 517 de 517 en servidor y cliente, en io_uring y en epoll.

### 4.10 macOS

Con L4: kqueue detrás de la abstracción del loop, `libcerver.dylib` (`-install_name`, `-rpath`), los equivalentes de
lo que hoy es de Linux (`sendfile`, `prctl`, `MSG_NOSIGNAL` → `SO_NOSIGPIPE`), y jobs de `macos-latest` con la
librería, los tests y los examples. arm64 y x86_64.

---

## 5. Los competidores y cómo se mide

**Las métricas,** todas en cada comparación:

- throughput;
- latencia (p50, p99 y p99.9);
- memoria (RSS en régimen, por conexión);
- asignaciones por petición o por documento;
- el tiempo de arranque.

**Las arquitecturas:**

- x86_64 con AVX2, y AVX-512 cuando la máquina lo tenga;
- arm64 con NEON.

Cada módulo con SIMD se mide también con su versión escalar.

**Los competidores:**

| Qué | Contra quién |
|---|---|
| JSON | yyjson, simdjson, RapidJSON, Jansson |
| El parser HTTP/1.1 | picohttpparser, llhttp |
| JWT | l8w8jwt, libjwt, jwt-cpp |
| deflate | zlib, zlib-ng, libdeflate |
| base64 y SHA | aklomp/base64, simdutf; el SHA del proveedor |
| HPACK | nghttp2 |
| El servidor completo | h2o, nginx, Drogon, fasthttp, actix-web; las pruebas plaintext y JSON de TechEmpower |
| Desde Python | granian, uvicorn, socketify |

**El método:**

- `benchmarks/competitors/` compila a cada competidor desde su fuente en una versión fija (la última estable al
  crearlo), anotada en `Dockerfile.competitors` y en cada archivo de resultados;
- los mismos datos de entrada para todos (los corpus de JSON y de peticiones van en el repo, generados, sin datos de
  nadie);
- varias corridas, con la mediana y la dispersión;
- **la medición oficial es en una máquina dedicada** (una VM sin otra carga, con CPUs fijas); la Mac sirve para
  desarrollar, no para decidir;
- los resultados de cada beta se guardan en `benchmarks/competitors/results/` con la arquitectura, la máquina y la
  versión.

**Publicar** (TechEmpower u otros) es una decisión aparte, cuando la 3.0 se acerque a su meta.

---

## 6. El gate de `labs`

**El de `nightly`** (§5 de su roadmap):

- gcc con los 4 TYPEs, g++ y clang;
- unit con ASan + UBSan, TSan y valgrind;
- los pares de integración × 4 variantes en IPv4 y `::1`;
- `disconnect.sh` y nginx;
- fuzz de 30 min por target nuevo;
- el flaky hunter de 200 repeticiones en las suites que tocan lo nuevo;
- autobahn en servidor y cliente (normal, ASan + UBSan y TSan);
- `performance.sh`.

**Más:**

- **Fuzz diferencial** de cada módulo propio contra el que reemplaza (o una referencia instalada en la imagen de
  fuzz).
- **Las suites de conformidad** del módulo (§2).
- **Mutantes** de cada módulo: cada arreglo y cada regla de seguridad tiene un test que lo mata.
- **La inyección de fallos** (`test/faults.c` y, desde L4, las syscalls del ring).
- **El benchmark como gate:** un módulo no entra si pierde contra el que reemplaza en alguna métrica de §5 (con la
  dispersión medida); `benchmarks/competitors/results/` guarda la evidencia.
- **Desde L4:** los pares en io_uring y en epoll, y un soak de 4 h con una aplicación real.

**La validación con aplicaciones reales** es fuera de este repositorio, con una imagen de `labs` y sus bindings, y
nunca en producción.

---

## 7. Los workflows

- **`pre-release.yml` y `beta.yml`** corren en cualquier rama `'*beta'`. Su bloque `case` suma:
  - `labs-beta) title=Labs Beta; moving=labs-beta`;
  - siempre `ermiry/cerver:<version.txt>` (3.0b-N);
  - el tag móvil `:labs-beta`.
- **`build.yml`:** en un push a `labs` construye `ermiry/cerver:labs` y `:labs-YYYYMMDD`, sin publicar.
- **`scheduled.yml`:**
  - el gate largo de `labs`, con `workflow_dispatch`;
  - el despachador de `master` suma `labs` a su matriz con el siguiente release a `master`;
  - hasta entonces se lanza a mano.
- **`test.yml`:**
  - desde L0, el job de los tests con fallos inyectados;
  - desde L1, los de conformidad;
  - el arnés de competidores no corre en el CI de los PRs: sus cifras solo valen en la máquina oficial.

---

## 8. La sincronía con las otras líneas

- **Sin merges, en ninguna dirección.** `http` sigue mergeándose en `nightly`, pero `labs` no recibe merges ni los da.
- **De `nightly` a `labs`:**
  - al principio de cada beta de `labs` se portan a mano los arreglos de `nightly` en el código que `labs` todavía
    comparte;
  - lo que `labs` ya reemplazó no recibe ports, pero el arreglo se revisa contra el código nuevo y, si aplica, su
    test entra a la suite;
  - desde la 3.0b-2 las rutas de `labs` son otras: cada archivo se traduce con la tabla de §11;
  - el port no tiene beta propia.
- **De `labs` a `nightly`:**
  - un módulo propio **madura** cuando pasa el gate de `labs` y no pierde en ninguna métrica contra el que
    reemplaza;
  - entonces se puede portar a mano a `nightly` como opción (un flag de build o un setter), con su plan aprobado, y
    llegar a la 2.x por el camino normal;
  - la 3.0 no espera a que eso pase.
- **pycerver:**
  - la rama `labs` de py-cerver (versiones 2.0bN, solo tags de GitHub, sin PyPI) acompaña a cada beta que cambia
    la API;
  - la rama `work` sigue con la 1.x, para cerver 2.x.

---

## 9. Riesgos

- **io_uring y Docker:** el perfil seccomp por default bloquea sus syscalls. Las imágenes de test llevan un perfil
  propio, y el respaldo epoll evita que una aplicación no arranque.
- **Cuatro líneas a la vez:** los ports a mano cuestan; `labs` solo recibe los arreglos del código que todavía
  comparte.
- **La medición:** una cifra suelta no decide nada. Una comparación sin la máquina oficial, sin varias corridas o sin
  la dispersión no cuenta como evidencia.
- **El alcance:** cada fase con su plan, su gate y su beta; lo que no es de la meta (game, otros protocolos) espera a
  una 3.x.
- **La seguridad de lo propio:** un parser nuevo trae bugs nuevos. Por eso el fuzz diferencial, las suites de
  smuggling y los mutantes son condición de entrada, no un extra.

---

## 10. Convenciones

- Las de `CLAUDE.md`:
  - el estilo de cerver;
  - constantes con nombre;
  - ramas cortas en funciones static;
  - nombres completos, sin abreviaturas;
  - archivos de una sola palabra;
  - carpetas en plural;
  - todo `.h` / `.c` tocado termina con salto de línea.
- **Cada bug se arregla con su test**, visto fallar contra una copia de la beta anterior.
- **Los defaults nuevos son potencias de 2.**
- **Cada ruptura de API o de ABI** va a `incompatibilities.md` con la beta que la trae.
- **El prefijo `cerver_`** es la regla de todos los símbolos públicos, aplicada por módulo: los que se reescriben
  (JSON, JWT, parser, request, response) nacen con él en su fase; los estables lo toman en su propia beta, con sus
  archivos renombrados sin abreviaturas a la vez.
- **Ningún archivo nombra ni copia nada de aplicaciones o servicios externos:** rutas, datos y configuraciones
  sintéticos.

---

## 11. Las rutas de la 3.0b-2

**Para los ports** desde `nightly` (y hacia la 1.9b-3 de `core`). Cada fila vale para `include/cerver/` (`.h`) y para
`src/cerver/` (`.c`); «partido» quiere decir que el archivo se divide en las partes que se nombran, con las regiones
de antes entre paréntesis. Un arreglo de `nightly` en una función se porta a la parte que tiene su región.

**El core:**

| Antes | Después |
|---|---|
| `cerver` | `core/`: `cerver` (main, create), `types` (global, types), `info`, `data` (stats, requests), `handler` (handlers), `start` (init, start), `update`, `end`, `report`, `serialization`; `cerver.h` queda como el paraguas |
| `admin` | `admin/`: `admin` (admin), `cerver` (main), `stats`, `lifecycle` (start, end), `handler`, `poll` |
| `auth` | `auth/`: `auth` (errors, data, method, auth), `hold` (handler, connections, poll de *on hold*) |
| `sessions` | `auth/sessions` |
| `balancer` | `load/`: `balancer` (types, main), `stats`, `service` (services), `events`, `start`, `route`, `end` |
| `client` | `client/`: `client` (main, stats, recycle, client), `events` (events, errors), `connect` (connect, start, end), `requests` (requests, files), `handler` |
| `connection` | `connection/`: `connection` (states, main, setters), `stats`, `recycle`, `data` (auth data, datagram token), `lifecycle` (init, connect, reconnect, start), `end` (stop, reset, end, drop), `registry` (lookups, registers), `update` (receive, update thread), `send` |
| `files` | `files/`: `files` (main), `cerver`, `images`, `send`, `receive` |
| `handler`, `receive`, `events`, `errors` | `handler/` |
| `network`, `socket`, `udp` | `network/` |
| `packets` | `packets/`: `packets` (packets, recycle), `header` (protocol, version, types, header) |
| `serializer` | `packets/serializer` |
| `system`, `timer`, `input` | `system/` |
| `version` | `version` (sigue arriba) |
| `config` | sin cambio |
| `game/gametype` | `game/types` |
| `collections/`, `crypto/`, `threads/`, `types/`, `utils/` | sin cambio |

**HTTP:**

| Antes | Después |
|---|---|
| `http/http` | `http/core/`: `http` (main, http), `pairs` (kvp, query pairs), `public`, `routes`, `uploads`, `limits`, `settings` (headers, proxies, origins, data, responses), `stats`, `admin` (admin, junto con el `admin.c` de antes), `callbacks` (parser), `forms` (mpart), `handler`, `upgrade` (websocket); la región `auth` fue a `http/security/auth.c`; `http/http.h` queda como el paraguas |
| `http/custom`, `http/metrics`, `http/utils` | `http/core/` |
| `http/access`, `http/recycle` | `http/core/`, internos en `src/` |
| `http/admin` (interno) | `http/core/admin.c`, sus declaraciones en `src/cerver/http/core/internal.h` |
| `http/headers` | `http/headers/`: `fields` (la lista `HttpHeaders`), `types` (el mapa de los headers), `header` (`HttpHeader`), `buffer` (la búsqueda en un header compilado) |
| `http/request`, `http/method`, `http/url`, `http/multipart`, `http/content` | `http/requests/` |
| `http/response`, `http/status`, `http/stream`, `http/compression`, `http/conditional`, `http/date` | `http/responses/` |
| `http/tree`, `http/route`, `http/middleware`, `http/validator` | `http/routes/` |
| `http/compatibility` | `http/routes/compatibility`, interno en `src/` |
| `http/auth`, `http/origin`, `http/proxy` | `http/security/` |
| `http/json/`, `http/jwt/`, `http/parser/`, `http/websockets/` | sin cambio |

**Los nombres:** cada función con `_by_` pasa a `_with_` (43, la lista en `incompatibilities.md`).

**Los tests:**

| Antes | Después | Binario |
|---|---|---|
| `test/connection.c` | `test/connection/` | `test/bin/connection` |
| `test/network.c`, `test/socket.c`, `test/udp.c` | `test/network/` | `test/bin/network` |
| `test/packets.c` | `test/packets/` | `test/bin/packets` |
| `test/receive.c` | `test/handler/` | `test/bin/handler` |
| `test/system.c` | `test/system/` | `test/bin/system` |
| `test/files.c` | `test/files/` | `test/bin/files` |
| `test/balancer.c`, `test/routing.c` | `test/load/` | `test/bin/load` |
| `test/handler/{cerver,client}.c` (el par) | `test/cerver/handler.c`, `test/client/handler.c` | el par `cerver/handler` |
| `test/http/*.c` | `test/http/{core, headers, requests, responses, routes, security}/` por su módulo | `test/bin/http` |
