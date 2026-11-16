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
> | **Estado** | L0 a L0.4 hechas (de la 3.0b-1 a la 3.0b-5); L1, el JSON propio, hecha en dos betas: la 3.0b-6 (el módulo, §4.1) y la 3.0b-7 (HTTP sobre él, Jansson fuera de la API); L2, el JWT propio, hecha en la 3.0b-8 (§4.2, libjwt y Jansson borrados); sigue L3, el parser HTTP/1.1 propio (§4.3) |

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
| **L0.3** | 3.0b-4 | El backlog de la revisión: los fixes de la 1.9b-2 y de la 2.0b-75, lo sin uso quitado, el dueño del stream de WebSocket, los analizadores estáticos | L0.2 |
| **L0.4** | 3.0b-5 | El event loop, keep-alive y los pools de `nightly` (sus F3, F4 y F5) | L0.3 |
| **L1** | 3.0b-6 | El JSON propio: el módulo `cerver/json`, medido y con su suite, junto al de Jansson | L0.4 |
| **L1** | 3.0b-7 | El JSON propio: la migración de HTTP y Jansson fuera de la API pública | 3.0b-6 |
| **L2** | 3.0b-8 | El JWT propio | L1 |
| **L3** | 3.0b-9 | El parser HTTP/1.1 propio | L0.2 |
| **L4** | 3.0b-10 | La arquitectura de I/O: io_uring, epoll de respaldo, thread-per-core | L3 |
| **L5** | 3.0b-11 | La API de aplicación sobre el I/O nuevo | L4 |
| **L6** | 3.0b-12 | SHA-1, SHA-256, base64 y deflate propios | L0.2 |
| **L7** | 3.0b-13 | HPACK y HTTP/2 | L4, L5 |
| **L8** | 3.0b-14 | TLS con el proveedor, sobre el ring | L4 |
| **L9** | 3.0b-15 | WebSocket sobre el I/O nuevo | L4, L5 |
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
- **Hecha en la sesión 47 (2026-11-13), sin commit:** la referencia sale en `reference/` (no en `docs/reference/`:
  `docs/` es el submódulo del wiki); `Dockerfile.docs` con Doxygen 1.9.8 y el job `docs` de `build.yml`; el `INPUT`
  del `Doxyfile` lista las carpetas documentadas, y `http/json/`, `http/parser/` y `http/jwt/jwt.h` (de terceros)
  quedan fuera; `-Wdocumentation` va en todo build de clang, con `-Wno-documentation-deprecated-sync` (un
  `@deprecated` no agrega el atributo). La guía está en `CLAUDE.md` (Conventions).
- **La sincronía con `core`** (la 1.9b-1): `core` no tenía nada que `labs` no tuviera; `core` recibió el layout de la
  3.0b-2, `crypto/random`, `_by_` → `_with_`, la licencia y la misma documentación, y los archivos del core solo
  difieren en las partes web de `cerver` y `handler`, el paraguas y lo de OpenSSL de `crypto/`. Un arreglo del core
  se hace en las dos ramas.

### 4.0.3 L0.3 — El backlog de la revisión (3.0b-4)

- **El port de la 1.9b-2 de `core`:** el stop desde una señal con un pipe, los reemplazos que liberan, las variantes
  seguras de collections, jobs y lobby, las referencias de los argumentos de los eventos, los límites del admin,
  `config.h` sin `__USE_*`, `str_set ()` que crece, la copia acotada, `test/faults/core.c` y los analizadores
  estáticos (`test/analyze.sh`, `.clang-tidy`, `test/suppressions.txt`, `Dockerfile.analyze`, el job `analyzers`).
- **Lo de HTTP de la 2.0b-75:** el video con RFC 9110 (200, 206 inclusivo, 416), los custom data que liberan el
  anterior, el pool de las respuestas y de los JWT, `http_request_set_dirname ()` con su último carácter, los
  timeouts sin aviso, los pares sin memoria (`test/faults/http.c`) y el aviso MIT de multipart-parser-c (L3 lo
  reemplaza).
- **Lo sin uso se quita** (en `core` y `http` quedó deprecado): `cerver_set_sockets_pool_init ()` y
  `CERVER_DEFAULT_SOCKETS_INIT`, `lobby_stats_set_threshold_time ()`, `lobby_get ()` y `lobby_search_with_id ()` (sin
  referencia), `timer_get_gmt_time ()` y `timer_get_local_time ()`, `game_set_player_comparator ()` y su campo, los
  `threshold_time` de las conexiones, los clientes, el admin y los lobbies, `CONNECTION_DEFAULT_BAD_PACKETS`,
  `SERVICE_CONNECTION_NAME_SIZE`, `SERVICE_DEFAULT_MAX_SLEEP`, los `GAME_DEFAULT_*` y `ClientEvent::request_type`.
- **El dueño del stream de WebSocket:** las piezas y el fin de un hilo que no empezó el stream son un error y no
  sueltan el mutex del dueño (`test_server_foreign_stream`).
- L1 y las fases siguientes se recorren una beta.

### 4.0.4 L0.4 — El event loop, keep-alive y los pools de `nightly` (3.0b-5)

- **El port de F3, F4 y F5 de `nightly`** (la 2.1b-15, la 2.1b-16 y la 2.1b-17, con lo que la 2.1b-18 les cambió),
  a mano sobre la 3.0b-4: `CERVER_HANDLER_TYPE_EVENTS` con sus pollers, la rueda de timers, los loops y su grupo;
  keep-alive en los dos modelos; los pools con nombre, sus lanes y las rutas de health y de ready;
  `thpool_has_queued ()`. Lo que la 3.0b-4 quitó se queda fuera, y las marcas `@since` son de la 3.0b-5.
- **El stop:** el thread de `cerver_start ()` con event loops espera en el extremo de lectura del `stop_pipe` de la
  3.0b-4 (`event_loop_group_stop_pipe_check ()`), sin el eventfd `stop_fd` de `nightly`
  (`test_server_events_stop_from_signal`).
- **Con lo de `nightly`:** el drain de `test/http/disconnect.c` (`disconnect_cases_drain ()`) y el `override` del
  `DEFINES` del `makefile`.
- **El rendimiento:** `test/performance.sh` pide el 90 % de las req/s de THREADS con EVENTS y el 150 % con una conexión
  que se conserva; `nightly` todavía no llega al 150 % (su 2.1b-19), y L4 reemplaza este I/O.

### 4.1 L1 — El JSON propio

L1 son dos betas (decisión del 2026-11-15): la 3.0b-6 entrega el módulo completo junto al de Jansson, sin romper
nada, y la 3.0b-7 migra HTTP y saca a Jansson de la API pública.

**Las decisiones** (2026-11-15):

- **Dónde vive:** `cerver/json/`, un paquete fuera de `http/` (el JSON no es solo de HTTP): `include/cerver/json/`,
  `src/cerver/json/`, `test/json/`. Tipos `CerverJson*` y funciones `cerver_json_*`.
- **El modelo:**
  - un DOM inmutable, con valores de 16 bytes contiguos en la arena del caller (o en una propia del documento): un
    texto que cabe en un chunk se lee sin un `malloc ()`, y la memoria es lineal y acotada por el texto;
  - un lector por eventos sobre un texto completo, que da los mismos errores en las mismas posiciones; el modo
    incremental se diseña con L4;
  - un escritor que serializa a un búfer del caller o a una arena, sin un `malloc ()` por valor;
  - los límites (profundidad, tamaño, valores) y los flags van por llamada: nada global ni por hilo.
- **RFC 8259 por default:** cualquier valor en el tope; las claves repetidas se aceptan y en la búsqueda gana la
  última (como Jansson, Python y JavaScript); un NUL escapado se acepta con su largo. Un flag rechaza las repetidas
  y otro el NUL. Sin modos lenient.
- **Los números:** `int64` y `uint64` exactos y real; un entero que no cabe es un error (un flag lo lee como doble) y
  un real que desborda siempre lo es. Los algoritmos, escritos de cero: Clinger, Eisel-Lemire y un respaldo exacto
  por dígitos para leer; el doble más corto que regresa igual (Schubfach) para escribir. La tabla de potencias de
  cinco la genera `benchmarks/tables.py`.
- **La salida:** el escritor escribe por default como Jansson (`", "` y `": "`), para que los cuerpos de hoy no
  cambien; compacto e indentado son flags.
- **El SIMD:** primero la versión escalar con SWAR; un kernel SIMD se queda solo si le gana en el arnés.
- **El JWT** sigue con la copia de Jansson, interna, hasta L2, que borra a los dos: la fila del JSON de
  `thirdparty.md` muere en L2.
- **La API vieja:** ruptura limpia en la 3.0b-7, sin capa de compatibilidad.
- **El JSON en el core (#207):** el módulo ya vive fuera de `http/`; a `core` llega por port si la 1.x lo pide.

**La 3.0b-6:**

- el módulo: `json`, `document`, `value`, `pointer` (RFC 6901), `events` y `writer`, con sus internos (`scan`,
  `text`, `number`, `tables`);
- `http_request_json ()`: el body de una petición como un valor, leído una vez en la arena de la petición;
- las pruebas:
  - las unidades de `test/json/`, con libc de oráculo para los números y la salida comparada byte a byte con la de
    Jansson;
  - JSONTestSuite a commit fijo (`test/conformance/`, `Dockerfile.conformance`): los `y_`, los `n_` y cada `i_` con
    su veredicto en `test/conformance/json.txt`;
  - los fuzzers `json` (los lectores entre sí y la ida y vuelta), `compare` (contra la copia de Jansson, con las
    diferencias decididas), `number` (contra libc), `writer` y `scan`;
  - los fallos de memoria en `test/faults/json.c`;
- los tests y el fuzzer de Jansson pasan a `test/jansson/` y al target `jansson`;
- **la barra:** no pierde en ninguna métrica contra Jansson, y el POST JSON cuesta 0 asignaciones; la brecha contra
  yyjson, simdjson y RapidJSON queda medida en `benchmarks/competitors/results/`.

**Lo que la 3.0b-6 decidió al medir** (2026-11-15, en la Mac: las cifras de la máquina oficial siguen pendientes):

- **La búsqueda por clave:** recorrer los miembros de un objeto perdía contra la tabla hash de Jansson (gana la
  última clave, así que se comparaban todos). Cada objeto lleva ahora, después de su último miembro, un
  **directorio** (la firma de cada clave, sus primeros 4 bytes, y dónde está) que se compara de 4 en 4 desde el
  último miembro, y con más de 32 miembros una **tabla** por el hash de sus claves, con una semilla del proceso; una
  tabla que claves elegidas llenan no se usa, y su objeto se busca en el directorio. Las distancias entre los
  valores son de 32 bits: ningún límite toma un texto de más de `CERVER_JSON_SIZE_MAX` (2^30 bytes).
- **El SIMD:** un kernel NEON para el scan de los strings se escribió y se midió contra el de 8 bytes por palabra
  (SWAR), y no leyó más rápido ningún caso, tampoco los strings largos (el caso `texts` del arnés): no se quedó.
  Los de x86_64 (SSE2, AVX2 con despacho) esperan a la máquina oficial, donde se pueden medir.
- **El escritor** escribe sin una llamada los bytes que caben y busca de 8 en 8 la parte de un string que sale
  como está.

**Lo que midió** (`benchmarks/competitors/results/aarch64.txt`, la mediana de 5 corridas; MB/s, y millones por
segundo en la búsqueda de 4 claves del documento chico). `legacy` es la copia de Jansson que el módulo reemplaza:

| Caso | Operación | cerver | legacy | Jansson 2.15.1 | yyjson | simdjson | RapidJSON |
|---|---|---|---|---|---|---|---|
| `small` | leer | 1 063.7 | 96.2 | 108.4 | 2 974.5 | 2 498.6 | 478.0 |
| `small` | escribir | 827.0 | 186.9 | 201.0 | 2 992.1 | 947.3 | 691.2 |
| `small` | buscar | 58.0 | 45.4 | 40.6 | 40.1 | 41.6 | 43.4 |
| `records` | leer | 1 061.1 | 83.9 | 105.3 | 2 671.2 | 2 673.5 | 549.7 |
| `records` | escribir | 860.2 | 203.7 | 240.8 | 3 053.6 | 972.3 | 770.0 |
| `numbers` | leer | 1 287.4 | 125.1 | 126.1 | 2 134.1 | 1 681.9 | 1 239.6 |
| `numbers` | escribir | 1 279.6 | 132.1 | 371.6 | 2 673.6 | 1 297.8 | 876.3 |
| `strings` | leer | 1 706.7 | 139.1 | 157.2 | 3 813.2 | 2 284.7 | 336.3 |
| `strings` | escribir | 1 595.2 | 545.2 | 394.1 | 3 967.0 | 3 651.6 | 1 283.8 |
| `texts` | leer | 9 737.0 | 182.9 | 207.7 | 4 988.9 | 8 606.4 | 366.2 |
| `texts` | escribir | 8 704.7 | 727.0 | 479.8 | 38 395.6 | 13 538.6 | 1 524.1 |

- **La barra se cumple:** ninguna métrica pierde contra Jansson (la copia y la 2.15.1), y la corrida más lenta de
  cerver queda sobre la más rápida de las dos en cada fila. Un documento chico se lee con 0 asignaciones (54 con
  Jansson) y uno grande con 2 (13 321). El POST JSON de `test/performance.sh` cuesta 0 asignaciones por petición
  con hilos, con eventos y con conexiones que se conservan (eran 14).
- **La brecha contra yyjson:** de 1.7 a 2.8 veces en la lectura (salvo los strings largos, que cerver lee al doble)
  y de 2.1 a 4.4 en la escritura; contra simdjson, de 1.3 a 2.5 en la lectura. cerver le gana a todos en la
  búsqueda por clave. La meta de L1 sigue después de la 3.0b-7.

**La 3.0b-7:**

- `http/core/admin.c`, `http/routes/tree.c`, `http/security/auth.c` y `http/responses/response.c` sobre el escritor
  nuevo, con los mismos bytes:
  - los tests de cadena exacta de cada documento (la info, las stats, los file systems y los workers de admin, el
    JSON de las rutas, los cuerpos de `http_response_json_*`, el JSON de un token) se escribieron primero y pasaron
    contra Jansson antes de migrar;
  - la única diferencia es la decidida en la 3.0b-6: un real sale con los menos dígitos que lo leen igual;
  - un miembro cuya cadena no es UTF-8 o cuyo real no es un número se sigue omitiendo de las stats, como hacía
    Jansson (`http_json_member_string ()` y `http_json_member_real ()`): el escritor fallaría el texto entero;
  - un GET de admin escribe su JSON en la arena de la petición, sin el árbol intermedio ni un `malloc ()` por
    valor, y un HEAD solo cuenta los bytes;
- los `decode_data` reciben los claims como un valor del JSON nuevo: el tipo `HttpDecodeData`
  (`void *(*) (const CerverJsonValue *claims)`), válido durante la llamada; los claims de un bearer se leen en la
  arena de la petición, sin claves repetidas ni NUL y con a lo más `HTTP_JWT_CLAIMS_DEPTH` (2^5) niveles;
- Jansson interno: sus headers en `src/cerver/http/json/`, sus objetos con `-fvisibility=hidden` (como el parser),
  fuera de `include/` y del paraguas `http/http.h`: `libcerver.so` exporta 87 símbolos menos, los `json_*`. Solo lo
  usa `http/jwt/jwt.c`, hasta L2;
- los tests, los examples y los benchmarks sin `json_*`; `incompatibilities.md` con la tabla de equivalencias;
- **la línea base de L2:** `GET /bench/auth` de `benchmarks/server` (un bearer HS256) cuesta 39 asignaciones por
  petición con el JWT derivado de libjwt, en los tres modos (`AUTH_MAX_ALLOCATIONS` de `test/performance.sh`);
- la rama `labs` de pycerver pasa a después de L2 (decisión del 2026-11-15): la 3.0b-7 y la 3.0b-8 cambian lo
  que liga (los `decode_data`, los nombres de los algoritmos del JWT).

**La meta de L1,** que sigue después de las dos betas: le gana a yyjson y a simdjson en lectura y escritura, con las
cifras de la máquina oficial.

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

**La 3.0b-8:**

- `cerver/crypto` con llaves y firmas (`key`, `signature`, y `jwk` y `openssl` adentro):
  - una `CerverCryptoKey` sale de un secreto, de un PEM sin passphrase o de un JWK (RFC 7517: `oct`, `RSA`, `EC`,
    `OKP`; uno con `use` o `key_ops` que no son de firmas se rechaza);
  - `cerver_crypto_sign ()` y `cerver_crypto_verify ()` sobre búferes del caller: HMAC, RSA PKCS #1 y PSS, ECDSA
    en R ‖ S con S baja, Ed25519 y Ed448;
  - `openssl.c` es el único archivo que incluye OpenSSL;
  - el proveedor revisa una llave hecha de sus números, limpia lo que un fallo deja en la cola de errores del hilo
    y **verifica cada firma de RSA antes de darla**: con OpenSSL 3.5 una asignación que fallaba dentro de una firma
    devolvía, como éxito, una firma que no verificaba (lo encontraron los fallos de memoria);
- `cerver/jwt` (`jwt`, `key`, `sign`, `verify`, `claims`), fuera de `http/`:
  - los 13 algoritmos; la llave lleva el suyo y un token nunca lo elige; `none` es el de ninguna llave;
  - `cerver_jwt_sign ()` escribe el token en un búfer del caller, sin memoria propia;
  - `cerver_jwt_verify ()`: el tamaño, las tres partes en base64url canónico, el header, la firma y solo después
    los claims, leídos en la arena del caller; el header que cerver escribe se compara como bytes;
  - la validación es un struct por llamada (`now`, la tolerancia, `exp` obligatorio, `iss`, `sub`, `aud`, el
    tamaño): nada es estado del proceso;
- HTTP sobre él: los tokens son los mismos bytes que los de la 2.x; `iat` y `sub` se revisan;
  `http_request_claims ()`; el archivo de una llave puede ser un JWK;
- libjwt y la copia de Jansson borrados, con sus dos filas de `thirdparty.md`: `libcerver.so` deja de exportar los
  56 `jwt_*`;
- las pruebas:
  - los ejemplos de RFC 4231, 7515, 7517, 7520, 8032 y 8037 en `test/crypto/` y `test/jwt/`;
  - Wycheproof a commit fijo por el proveedor y por el JWT (`test/conformance/jwt.c`): 25 archivos, 5 037 pruebas,
    con cada decisión y su razón en `test/conformance/jwt.txt`;
  - los fuzzers `jwt`, `jwk` y `differential` (contra la libjwt 1.17.2 del sistema); mientras las dos
    implementaciones convivieron, 30 minutos y 24.7 millones de tokens contra el JWT que se borra, sin diferencias
    (salvo un entero entre 2^63 y 2^64 en los claims, que el JSON nuevo lee y Jansson rechazaba);
  - los fallos de memoria de cada llamada, uno por uno (`test/faults/{crypto,jwt}.c`);
  - 133 mutantes de las reglas de seguridad: 128 muertos y 5 equivalentes;
- **la barra:** un bearer HS256 cuesta 10 asignaciones por petición en los tres modos (eran 39), todas dentro del
  `HMAC ()` de OpenSSL.

**Lo que midió** (2026-11-16, en la Mac con la máquina sola: las cifras de la máquina oficial siguen pendientes;
`benchmarks/competitors/results/aarch64.txt`, la mediana de 5 corridas, tokens por segundo y, entre paréntesis, las
asignaciones de uno). `legacy` es el JWT de libjwt sobre la copia de Jansson que el módulo reemplaza, medido desde
el árbol anterior al borrado:

| Algoritmo | Operación | cerver | legacy | libjwt 1.17.2 | l8w8jwt 2.5.0 | jwt-cpp 0.7.2 |
|---|---|---|---|---|---|---|
| HS256 | firmar | 1 389 563 (10) | 421 486 (66) | 395 529 (70) | 296 913 (12) | 569 364 (59) |
| HS256 | verificar | 1 083 364 (10) | 488 681 (53) | 433 513 (62) | 606 358 (20) | 553 022 (58) |
| HS512 | firmar | 1 159 333 (10) | 397 681 (66) | 363 462 (70) | 281 894 (12) | 502 360 (60) |
| HS512 | verificar | 890 798 (10) | 452 439 (53) | 403 972 (62) | 529 566 (20) | 495 291 (59) |
| RS256 | firmar | 3 072 (51) | 3 131 (85) | 2 025 (337) | 711 (146) | 3 137 (78) |
| RS256 | verificar | 101 554 (22) | 92 846 (65) | 40 532 (239) | 23 409 (36) | 93 867 (66) |
| PS256 | firmar | 3 012 (68) | — | — | 725 (144) | 3 113 (90) |
| PS256 | verificar | 95 949 (30) | — | — | 22 732 (36) | 88 557 (78) |
| ES256 | firmar | 86 038 (58) | 74 916 (119) | 34 931 (458) | 7 213 (559) | 76 389 (113) |
| ES256 | verificar | 31 000 (64) | 30 207 (105) | 21 596 (418) | 2 181 (1 577) | 30 078 (115) |
| ES384 | firmar | 10 788 (73) | 10 541 (134) | 8 594 (516) | 4 622 (655) | 10 609 (129) |
| ES384 | verificar | 4 995 (76) | 5 006 (117) | 4 571 (473) | 1 265 (2 032) | 5 029 (124) |
| EdDSA | firmar | 64 342 (7) | — | — | — | 59 501 (57) |
| EdDSA | verificar | 26 914 (7) | — | — | — | 26 304 (55) |

- **Contra el que reemplaza:** el HMAC firma 3.3 veces más rápido y verifica 2.2 (2.9 y 2.0 con SHA-512); RS256
  verifica 9 % más rápido, ES256 firma 15 % más rápido, y cada operación pide menos memoria (10 asignaciones
  contra 66 y 53 en el HMAC; de un tercio a dos tercios en las demás).
- **Una métrica pierde, por decisión: firmar con RSA es ~2 % más lento** (3 072 contra 3 131 con RS256; contra
  jwt-cpp, 2 % en RS256 y 3 % en PS256). Es lo que cuesta verificar cada firma de RSA antes de darla (una operación
  pública, ~3 % de la privada), la protección contra la firma inválida que OpenSSL 3.5 devolvió como éxito. Quitarla
  recupera la cifra y deja pasar esa firma: se queda mientras el usuario no diga otra cosa.
- **ES384 al verificar empata** (4 995 y 5 012 en dos corridas, contra 5 006 del que reemplaza): la curva es de
  OpenSSL en los dos y la diferencia está dentro del ruido.
- **Contra los competidores:** cerver le gana a libjwt y a l8w8jwt en todas las filas, y a jwt-cpp en todas menos
  en firmar con RSA (lo de arriba) y en el empate de ES384 al verificar.
- `test/performance.sh` con la máquina sola: el bearer en 10 asignaciones en los tres modos; **un check al margen
  falla desde la 3.0b-6** (POST JSON con 64 conexiones, conservadas contra una conexión por petición, mínimo
  150 %: 141 % en la 3.0b-6, 146 % y 152 % en la 3.0b-7, de 147 % a 149.7 % en la 3.0b-8); no lo tocan estas betas.

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
- **Lo de `nightly`:** F3–F5 (en `labs` desde la 3.0b-5) dejan lecciones y tests que se reusan, pero el diseño de L4
  es propio. **Riesgo:** el
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
