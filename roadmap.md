# cerver — Roadmap

> El plan de trabajo principal para llevar cerver de la versión `2.0b-56` a un framework capaz de competir en las
> grandes ligas.
>
> | | |
> |---|---|
> | **Fecha** | 2026-11-04 |
> | **Rama base** | `http`: idéntica al tag `2.0b-56` más los arreglos sin commitear de §3 |
> | **Contexto** | `percepthor-backend/docs/context/cerver-module-context.md` (el estado del módulo, las fases, las versiones y el 502 de gepp) |
> | **Alcance** | cerver (`/Users/ermiry/Documents/cerver`), pycerver (`/Users/ermiry/Documents/py-cerver`), la imagen devel (`percepthor-devops`) y los servicios de validación (gepp, balance, payloads) |

Las citas `archivo:línea` son relativas a `src/cerver/` salvo que se indique otra cosa, y corresponden al estado de la rama
`http` el 2026-11-04. Hay que re-verificarlas antes de tocar cada zona, porque cada fase mueve las líneas.

---

## 1. Visión y principios

cerver nació para ser un framework **ligero, enfocado al máximo rendimiento y que se pueda personalizar** según las
necesidades de cada servidor. La meta es competir con h2o, drogon, nginx-unit o los servidores sobre libuv o
libevent, y ser mejor que ellos en su nicho: un núcleo C pequeño que también se pueda usar desde Python (pycerver).

Principios que guían cada fase:

1. **Correcto antes que rápido.** Un framework rápido que pierde bodies o resetea conexiones no compite. Las Fases
   0–2 arreglan lo que hoy falla.
2. **Cero copias y cero asignaciones en el camino caliente.** Hoy cada petición hace unos 15 `malloc` (§4.2).
3. **Nada de perillas de correctitud.** Ningún valor de configuración debe decidir si un body llega bien.
   `CERVER_RECEIVE_BUFFER_SIZE` debe ser solo una perilla de rendimiento.
4. **Límites explícitos y backpressure.** Todo recurso tiene un máximo configurable, y al llenarse se rechaza con un
   código claro (413, 431, 503), en lugar de crecer sin límite.
5. **Todo configurable, medible y probado.** Cada bug arreglado lleva su test. Cada fase cierra con una validación
   real en test (§6).
6. **Sin dependencias nuevas.** Las únicas siguen siendo pthreads, OpenSSL y libm.
7. **C11 y C++11.** El código debe compilar con gcc y g++ (`compile.sh`), así que los atómicos se escriben con
   `__atomic_*`, no con `<stdatomic.h>`.

## 2. Necesidades del sistema que fijan la prioridad

cerver sostiene todo el backend de Percepthor por pycerver (ctypes sobre `/usr/local/lib/libcerver.so`). Todos los
servicios se configuran igual: `cerver_create_web`, `cerver_set_receive_buffer_size`, `cerver_set_thpool_n_threads`,
`cerver_set_handler_type (THREADS)`, `cerver_set_reusable_address_flags`, uploads a `/var/uploads` (tmpfs) y el árbol de
rutas con auth custom (libauth). Ver por ejemplo `gepp-service/service/service.py:175-230`.

| Servicio | Tráfico | Lo que exige a cerver |
|---|---|---|
| **gepp-service** (`:5060`, 2 réplicas, 32 hilos / cola 64) | Multipart de fotos (`refris/upload` con 1–6 JPEG de 21–44 KB, `censos/v2/upload`). Meta GEPP: 250 k tiendas por semana, 3.5 subidas/s en el pico de 4 h, 7/s de holgura, `breakpoint` hasta 28/s | multipart confiable, nada de RST bajo concurrencia, poca latencia de cerver (el GIL ya serializa a los 32 hilos) |
| **payloads-service** (`:5032`, 16 hilos / cola 32, **buffer 16384**) | Payloads de encuestas: `/upload` de un solo envío, o por chunks `/upload/start` → `/chunk` → `/end`. Una encuesta abandonada deja ~20 MB | bodies grandes y multipart con muchas partes. Es el servicio con el buffer más grande "porque así funciona" |
| **balance-service** (`:6000`, 2 réplicas, **64 hilos / cola 128**) | Proxy de inferencia: reenvía el multipart del cliente a los thors on-prem o a la nube, con timeouts de hasta 180 s | hilos ocupados mucho tiempo: el pool se agota y entonces cerver crea hilos desprendidos sin límite (§4.3) |
| nginx 1.31.6 delante de todos | HTTP/1.1 hacia los upstreams, **reutiliza conexiones**. `client_max_body_size` de 16M (gepp) y 64M (payloads) | `Connection: close` correcto hoy, keep-alive real mañana |

**La configuración de `CERVER_RECEIVE_BUFFER_SIZE` / `CERVER_POOL_THREADS` / `CERVER_CONNECTION_QUEUE`**, por compose
(en `percepthor-backend/` salvo que se indique otra cosa):

| Compose | Servicios | Buffer / hilos / cola |
|---|---|---|
| `docker-compose.yml` (prod) | la mayoría | 8192 / 8 / 8 |
| | payloads, surveys | 16384 / 16 / 32 |
| | exports | 16384 / 4 / 8 |
| | auth, images, results | 8192 / 32 / 64 |
| | controller, recon | 8192 / 32 / 32 |
| `docker-compose.test.yml` | todos | 8192 / 4 / 4 |
| `docker-compose.dev.yml`, `.devel.yml` | todos | 4096 / 4 / 4 |
| `docker-compose.clients.yml` (prod) | gepp | 8192 / 32 / 64 |
| `docker-compose.balance.yml` (prod) | balance | 8192 / 64 / 128 |
| `docker-compose.fifco.yml` | fifco-payloads | 16384 / 16 / 32 |
| `docker-compose.core.yml` | optical | 16384 / 8 / 8 |
| `docker-compose.recon.yml` | recon | 4096 / 8–16 / igual |
| `*/tests/Dockerfile` | todas las suites | 4096 / 4 / 4 |

- El commit `d4590b5` del backend (2022-11-23, "Refactored production services custom receive buffer size") subió 31
  servicios de 4096 a 8192 y uno a 16384, sin dejar razón escrita.
- **La razón real está en §4.1.** No existe un valor correcto: el bug depende de dónde se parte la petición entre
  `recv ()`, y eso lo decide la red, no el buffer.

**Problemas de cerver que el backend ya documentó** (en `percepthor-backend/docs/`):
- Uploads truncados cuando se llena el tmpfs: cerver reporta éxito y `saved_filename`. Los servicios comparan contra
  `http_multi_part_get_total_wrote`. Ver `context/payloads-uploads-recovery-module-context.md` §5 y
  `operations/uploads-maintenance.md`.
- Apagado: el handler de SIGTERM puede no correr mientras `cerver_start` bloquea, y `cerver_shutdown` no espera a los
  handlers en curso. Ver `context/client-rolling-deploy-session-context.md`.
- Agotamiento del pool por handlers síncronos de 15–25 s. Ver `context/client-findings-module-context.md` §3.8.
- La trampa del orden de rutas: `:id/{literal}` hace match con cualquier primer segmento. Ver
  `context/virtual-cycles-session-context.md:542`.
- Multipart:
  - `content_type` siempre es None (`backlog/modules-service-tests-findings.md:21`);
  - `mpart_has_field` reinicia el iterador;
  - los validadores `validate_mparts_*` y `validate_query_*` caen al default en silencio y no reciben `errors`
    (`backlog/payloads-service-tests-findings.md` §2–3).

## 3. Estado actual (rama `http`)

**2026-11-06: la Fase 3 y la suite de tests se publicaron en la Beta 2.0b-59**, con la migración de gepp, balance,
payloads y tickets validada en test (§4.3.4). La Beta 2.0b-60 publicó la Fase 3b (§4.3.5), sin WebSocket, y los
arreglos de CI de 2.0b-59 (el crecimiento geométrico de `String` y la reserva del body).

**2026-11-08: la Beta 2.0b-61** (§4.3.6) trae los tres arreglos que no necesitan el event loop: el 500 de un handler
sin respuesta, la espera con `poll` en un socket no bloqueante lleno y la secuencia de los uploads; además, `gcc:16.2`
en los Dockerfiles. La Fase 4 pasó a ser la línea 2.1 (`nightly`), que nace de `http` después de esta beta. El
detalle está en `percepthor-backend/docs/context/cerver-http-module-context.md`.

- **Rutas:**
  - árbol radix (`http/tree.c`) compilado en `cerver_start`, con prioridad estático > parámetro > comodín;
  - grupos con herencia y endpoints por `(ruta, método)`, params por nombre y validadores;
  - middlewares `before` / `after`;
  - la API vieja (`http_route_*`) se traduce al árbol (`http/compatibility.c`).
- **Semántica:**
  - `PATCH`, HEAD desde GET, OPTIONS automático, 405 opt-in;
  - política del `/` final;
  - `max_body` por endpoint.
- **Rendimiento:**
  - match sin asignaciones (~40 ns contra ~180–230 ns con las rutas reales de gepp e indicators);
  - lookup de cabeceras por hash;
  - cabeceras custom en la arena;
  - respuestas con un solo `sendmsg`.

**La Fase 2** (sobre la Beta 2.0b-57) quedó así:

- **thpool:**
  - una cola acotada (ring buffer) con las políticas `wait` / `reject`;
  - hilos joinables y drenado con deadline;
  - TSan limpio en todos los unit tests.
- **Accept:**
  - sin hilos desprendidos;
  - EINTR = stop ordenado;
  - fd de reserva para `EMFILE`;
  - backlog `SOMAXCONN`;
  - `SO_REUSEPORT` aparte.
- **Apagado:**
  - `cerver_teardown` drena antes de liberar;
  - los hilos de cerver bloquean SIGINT / SIGTERM.
- **Deadlines:** de cabeceras (408), total opcional y `SO_SNDTIMEO`.
- **Memoria:**
  - arenas por hilo para el request;
  - `Socket` con contador de referencias, sin pool.
- **Collections:** los bugs de dlist, AVL, htab y pool corregidos.
- **TCP:** el ensamblado de paquetes se reescribió en `receive.c` (cerver y cliente), y `test/handler` recibe 100 de
  100.

Lo que queda para las fases siguientes:

- la respuesta diferida desde otro hilo (la conexión se cierra cuando el handler termina);
- las listas intrusivas;
- la conexión web sin `ConnectionStats` / `PacketsPerType`, la mayor parte de las 8 asignaciones restantes por GET
  (eran 11 antes de 2.0b-60; POST JSON pasó de 35 a 28).

El estado previo a las Fases 0 y 1:

| Cambio | Archivos | Estado |
|---|---|---|
| **`Connection: close` en todas las respuestas.** cerver cierra después de cada petición, pero respondía `HTTP/1.1` sin decirlo; nginx reutilizaba la conexión y recibía RST → 502 | `include/cerver/http/response.h` (`HTTP_RESPONSE_CONNECTION_CLOSE_HEADER`), los 7 constructores de cabeceras en `http/response.c`, `test/http/responses.c` | arreglo definitivo hasta la Fase 4 |
| **Lock de `pool_push`.** `dlist_end ()` se leía fuera del mutex que protege la inserción | `collections/pool.c:99-115` | arreglo definitivo |
| Instrumentación `CERVER_DIAGNOSTICS` | `cerver.c`, `connection.c`, `handler.c`, `http/http.c`, `http/response.c` | provisional: se integra en la Fase 0 |

Validado en test con `gepp-service:devel`: el reproductor pasó de 2–3 % de 502 a 0 de 640, y `peak` de 0.90 % de
errores a 0 de 12,610.

---

## 4. Fases

Cada fase cierra con:
1. compilación con `TYPE=development|test|beta|production` y `CC=g++`, sin warnings nuevos;
2. `bash test/run.sh` en verde (y desde la Fase 0, con sanitizers);
3. los bindings de pycerver al día (§4.6);
4. la validación devel en test (§6);
5. el commit dedicado de versión ("Updated version to Beta 2.0b-N") con su `CHANGELOG.md`;
6. un documento de contexto en `percepthor-backend/docs/context/`.

### 4.0 Fase 0 — Integración y base de pruebas

**Objetivo:** dejar los arreglos actuales integrados con las convenciones del repo, tener tests confiables y la
cadena devel lista para validar las fases siguientes.

**0.1 Integrar la instrumentación.** Desaparece `CERVER_DIAGNOSTICS`. Cada log pasa al define de su módulo, y los
errores reales se loguean siempre. Se quita el prefijo `[DIAGNOSTICS]` y se usa el formato existente,
`función () - detalle`.

| Archivo | Bloque actual | Destino |
|---|---|---|
| `cerver.c` (`cerver_sockets_pool_push` / `_pop`) | detectores `socket still open`, `socket in use`, `pool corrupted` | `#ifdef SOCKET_DEBUG`, con chequeo de NULL de `cerver->sockets_pool` (hoy se desreferencia sin verificar) |
| `connection.c` (`connection_end`) | log `close fd N - ip:puerto` | `#ifdef CONNECTION_DEBUG` |
| `connection.c` (`connection_end`) | `close () failed` | siempre (`LOG_TYPE_WARNING`, `LOG_TYPE_CONNECTION`) |
| `handler.c` (`cerver_receive_http_actual`) | `recv () failed` con errno y `strerror_r` | siempre (warning) |
| `handler.c` | struct `HttpReceiveDiagnostics` (`last_rc`, `last_errno`, `n_recvs`) | se conserva sin `#ifdef`: cuesta tres asignaciones por `recv` y alimenta el log de `Bad request` |
| `handler.c` (`cerver_receive_http`, inicio del worker) | chequeo con `getpeername` del fd y del puerto | `#ifdef HANDLER_DEBUG` (es una syscall por petición) |
| `handler.c` (`cerver_register_new_connection`) | `accepted fd N - ip:puerto` | `#ifdef HANDLER_DEBUG` |
| `handler.c` (el `default:` de `Bad request`) | log extendido (status, recibido, `recv`s, rc, errno, parser, running, tiempo) | siempre: reemplaza al `"%s -> Bad request"` plano |
| `http/http.c` (`http_receive_handle`) | `parser error` | siempre, fusionado con el `cerver_log_error` existente en una línea (n parsed, received, `http_errno_name`, descripción, status) |
| `http/response.c` (`http_response_send_actual`) | `send () failed` | siempre (warning, con errno y bytes pendientes) |

Además, `percepthor-devops/Dockerfile.devel` pasa su `CERVER_DEFINES` por omisión a `-D _GNU_SOURCE`.

**0.2 Tests.**
- **El test del arreglo de `pool_push`:** un `test/collections/pool.c` nuevo con N hilos haciendo `pool_push` /
  `pool_pop` concurrentes. Al final se verifican `size`, `start` y `end`. Se registra en `test/collections/collections.c`
  y `collections.h`.
- **Arreglar los dos binarios que fallan con gcc 13.** También fallan en el tag `2.0b-56` intacto, y son bugs de los
  tests, no del código:
  - `test/collections/htab.c:277-280` reinserta `data`, que aún apunta al elemento idx 9 liberado en la línea 252, y
    `htab_destroy` lo libera de nuevo. glibc ≥ 2.29 aborta por el double free.
  - `test/utils/c_strings.c:28-30` escribe 41 bytes en un buffer de 32 a propósito ("this causes an overflow"):
    `-fstack-protector` aborta.
  - `test/utils/math.c:62`: `random_int_in_range` es inclusivo, pero el test verifica el rango estricto, así que
    falla de vez en cuando.
- Agregar `./test/bin/version` a `test/run.sh`.

**0.3 Herramientas.**
- `make SANITIZER=address|thread|undefined`, que agrega `-fsanitize=… -fno-omit-frame-pointer` a la librería, los tests
  y los ejemplos.
- `valgrind --leak-check=full --error-exitcode=1` sobre los unit tests.
- CI: los workflows usan `actions/*@v2` y `upload-artifact@v2`, que GitHub deshabilitó. Pasar a `@v4`, agregar una
  matriz de sanitizers y correr `test/handler` (`testhandler`), que hoy no corre.
- `build.yml` corre `make test` en modo development; agregar `TYPE=test`.

**0.4 pycerver y la cadena devel.**
- `percepthor-devops/Dockerfile.devel`: `pip install` desde `--build-context pycerver=/Users/ermiry/Documents/py-cerver`
  en lugar de `pycerver==0.9.8`. `devel.sh` agrega `PYCERVER_DIR`.
- **Auditoría de bindings.** Hay unas 50 funciones `http_*` exportadas sin binding, por ejemplo:
  - `http_cerver_auth_generate_jwt`, `_validate_jwt`, `_load_key`;
  - `http_response_add_custom_header`;
  - `http_request_get_bytes_range`, `http_request_multi_parts_get_all_saved_filenames`;
  - `http_url_decode` / `_encode`, `http_receive_status_str`, `http_request_content_type_is_json`.

  Para obtener la lista se comparan los `CERVER_EXPORT` / `CERVER_PUBLIC` de `include/cerver/**` contra los
  `lib.<símbolo>` de `py-cerver/cerver/**`.
  - Hay que agregar los que son API de usuario, con `argtypes` y `restype` verificados contra el header.
  - Corregir los tipos que no coinciden: `Cerver.receive_buffer_size` es `u32` (`include/cerver/cerver.h:312`), pero
    `cerver_set_receive_buffer_size` recibe `size_t`.
  - Si falta un símbolo, ctypes falla al importar. Por eso se verifica con `python -c "import cerver, cerver.http"`
    dentro de la imagen.
- Nuevos `payloads-service/Dockerfile.devel` y `devel.sh` (submódulo en `percepthor-backend/payloads-service`), con el
  patrón de gepp, y su override de compose en test.

**Criterio de salida:**
- `test/run.sh` en verde con gcc 13 y con ASan;
- las tres imágenes devel construidas;
- gepp validado de nuevo (sin regresión del 502);
- bump a **2.0b-57**.

### 4.1 Fase 1 — Correctitud HTTP: el bug del body y la seguridad

**La causa raíz del bug "del buffer".** Cada `recv ()` se pasa directo a `http_parser_execute`
(`handler.c:2273` → `http/http.c:3030`). Cuando un token cruza el límite entre dos `recv`, http_parser llama
**varias veces** a `on_url`, `on_header_field`, `on_header_value` y `on_body`, una vez por pedazo. Los callbacks de
cerver suponen que cada llamada trae el token completo:

| # | Bug | Dónde | Escenario | Efecto |
|---|---|---|---|---|
| 1 | El append del body **no escribe el NUL** | `types/string.c:246-258` (`str_append_n_from_c_string`: `strncpy` de `n` bytes sin terminador; el último byte del `realloc` queda sin inicializar), llamado desde `http/http.c:1764-1770` | un body JSON que llega en dos o más `on_body` | `request->body->str` trae basura después de los datos: `json_loads` falla o lee de más. Si el body es urlencoded, `http_url_decode` hace `strlen` fuera del buffer |
| 2 | **Un valor de cabecera partido conserva solo el último pedazo** (y el anterior se fuga) | `http/http.c:1723-1749` | un `recv` que termina dentro de `Content-Type: multipart/form-data; boundary=…` | `is_multipart` falla o el boundary queda truncado: el multipart se pierde en silencio. Un `Authorization` partido da 401 |
| 3 | **Un nombre de cabecera partido se evalúa por pedazos** | `http/http.c:1697-1720` | `"Content-Ty"` + `"pe"` | dos cabeceras custom falsas, `headers[HTTP_HEADER_CONTENT_TYPE]` en NULL. El mismo efecto que el bug 2 |
| 4 | **Una URL partida conserva solo el último pedazo** (y la anterior se fuga) | `http/http.c:1676-1693` | una URL larga o un `recv` muy corto | ruta equivocada o 404, query equivocada |
| 5 | urlencoded con la longitud **previa** a la decodificación | `http/http.c:2485-2491` | cualquier body urlencoded con `%XX` | el último valor lee más allá del NUL, aunque no haya cortes |
| 6 | El primer chunk del body se crea con `str_create ("%.*s")` | `http/http.c:1765` | un body binario que no es multipart | se trunca en el primer 0x00 |
| 7 | `query->len` cuenta el `?` | `http/http.c:1580-1586`, usado en `:2467` | cualquier query | `real_query + query->len` apunta un byte después del NUL |

**Por qué un buffer más grande "lo arregla", y por qué no hay un valor correcto.**
- Con un buffer más grande hay menos `recv`, así que se parten menos tokens. Si todo llega en un `recv`, solo corre el
  camino correcto (`str_create`).
- Pero sobre una red real `recv ()` devuelve lo que haya llegado (segmentos de ~1448 bytes), sea cual sea el buffer.
- Los clientes que escriben las cabeceras y el body por separado, los bodies chunked y las cabeceras de más de 4 KB
  (JWT y cookies grandes) parten tokens con cualquier valor.
- Por eso cada servicio "funciona" con un valor distinto: depende del tamaño típico de sus peticiones.

**Arreglos:**
- Acumular URL, nombre y valor de cabecera entre callbacks. El token se cierra cuando cambia el tipo de callback: un
  `on_header_value` cierra el nombre, y el siguiente `on_header_field` o `on_headers_complete` cierra el valor.
  - El patrón ya existe y funciona en los callbacks de multipart (`http/http.c:1941-1984`, commits `e4ec141c`,
    `b2e763ca` y `a24a7da4`).
  - Las cabeceras duplicadas se concatenan con `, ` o se rechazan, según la RFC 9110 §5.3, en lugar de fugarse.
- Body con un buffer binario de longitud explícita (sin `%.*s`) y terminador NUL siempre. Hay que corregir
  `str_append_n_from_c_string` y el test que pasa por suerte (`test/types/string.c:363`).
- Corregir la longitud de urlencoded (bug 5) y de la query (bug 7).
- **Límites configurables**, cada uno con su respuesta:

  | Límite | Respuesta |
  |---|---|
  | `max_body_size` | 413 |
  | `max_url_size` | 414 |
  | `max_header_size` | 431 |
  | `max_headers` | 431 |
  | `max_multipart_parts` | 413 |
  | `max_file_size` | 413 |

  Hoy no hay ninguno: el body crece con `realloc` sin tope, y cada cabecera custom es un `malloc` de 768 bytes
  (`include/cerver/http/custom.h:8-9`). El único límite es `HTTP_MAX_HEADER_SIZE` de 80 KB
  (`include/cerver/http/http_parser.h:50`).
- Con esos límites, `CERVER_RECEIVE_BUFFER_SIZE` queda como perilla de rendimiento. Se recomienda un valor uniforme
  (p. ej. 16384) en todos los composes.
- Responder `100 Continue` cuando llega `Expect: 100-continue`.
- **Pipelining:** si llega una segunda petición en el mismo buffer, hoy se parsea y su handler corre sobre fd -1,
  porque `on_message_complete` llama a `connection_end` (`http/http.c:3017-3019`) y http_parser sigue. Con
  `http_parser_pause` o devolviendo distinto de cero se detiene el parseo.
- `receive_status` y el log de `Bad request` deben distinguir timeout, cierre del cliente, error del parser y límite
  excedido. El struct de diagnóstico de la Fase 0 ya tiene los datos.

**Seguridad (alta prioridad):**

| Bug | Dónde | Arreglo |
|---|---|---|
| **Path traversal en estáticos.** El path se concatena sin normalizar, así que `GET /../../etc/passwd` con un socket crudo o con `curl --path-as-is` funciona. No se rechazan directorios (no hay `S_ISREG`), y `c_string_concat_safe` deja el filename anterior si el path no cabe | `http/http.c:2917-2966` (`http_receive_handle_serve_file`, con un `// TODO: handle authentication`) | `realpath` con verificación de prefijo, rechazar `..` y lo que no sea `S_ISREG`, auth en estáticos |
| **Lectura del Bearer fuera de límites.** `str + sizeof ("Bearer")` sin verificar el prefijo ni la longitud | `http/http.c:2635` | validar `"Bearer "` y la longitud |
| **Índice de tokens de ruta.** Una URL con más de 16 segmentos lee `routes_tokens[n_tokens - 1]` fuera del arreglo: un crash remoto | `http/http.c:2555` | el límite se verifica (y en la Fase 3 desaparecen las cubetas) |
| **Multipart antes del auth.** Los archivos se abren y escriben en los callbacks (`http/http.c:2091-2095`, `:2157`), antes del match de ruta y del auth, que corren en `on_message_complete`. Un cliente sin autenticar escribe en disco en cualquier ruta | `http/http.c` | resolver ruta y auth en `on_headers_complete`, antes de aceptar el body |
| Archivos de upload con `O_CREAT \| O_WRONLY`, sin `O_TRUNC` ni `O_EXCL` y con modo 0777 | `http/http.c:2093`, `include/cerver/http/http.h:27-28` | `O_EXCL` y 0644 por omisión (directorios 0755). Reintentar las escrituras cortas |
| Upload truncado reportado como éxito (el tmpfs lleno) | escritura de multipart | si falla el `write`, se marca la parte con error y el request queda en error (hoy los servicios lo detectan a mano) |
| Se ignora el retorno de `multipart_parser_execute` | `http/http.c:2213`, `:2249` | un boundary inválido se convierte en 400 |
| La búsqueda de boundary de respaldo (`http_receive_handle_mpart_body_look_for_boundary`) busca chunk por chunk (no ve un boundary partido), descarta lo anterior y lee `found + 24` más allá de `length` | `http/http.c:2219-2254` | se elimina: con el bug 2 arreglado, el boundary siempre sale de la cabecera |

**Envío:**
- `send (…, 0)` sin `MSG_NOSIGNAL` (`http/response.c:705`), y la librería no ignora `SIGPIPE`: un RST del cliente
  mata el proceso. Solo algunos servidores de test hacen `signal (SIGPIPE, SIG_IGN)`.
- Manejar `EINTR` y `EAGAIN` en `send`.
- El loop de `sendfile` (`http/response.c:842-874`) se detiene en silencio ante un error y reporta éxito.
- Cerrar con `shutdown (SHUT_WR)` y un drenado corto en lugar de un `close ()` con datos sin leer. Un `close` con
  bytes pendientes en el socket (un error del parser, una petición pipelined) manda un RST que puede destruir la
  respuesta en vuelo.

**Tests:**
- un *feeder* que pase cada petición del corpus a `http_receive_handle` partida **en cada offset de byte posible**
  (URL, nombres, valores y body), además de 1 byte por `recv`;
- pipelining, cabeceras duplicadas, Authorization corto, `..` en estáticos;
- cada límite con su código;
- multipart con el boundary partido.

**Criterio de salida:** el escenario de payloads de §6 pasa con buffers de 1024, 4096, 8192 y 16384 bytes.

### 4.2 Fase 2 — Concurrencia incremental (sobre el modelo actual)

**Estado (2026-11-05): implementada y validada.**

- La evidencia y las decisiones están en `percepthor-backend/docs/context/cerver-module-context.md` §3.5.
- Los puntos de esta sección describen el código **antes** de la fase.
- Desviaciones del plan:
  - `job_queue` (los Handlers) conserva su semáforo binario, porque cada cola tiene un solo consumidor;
  - las arenas cubren el request (structs, URL, query y cabeceras conocidas) pero no el body ni multipart;
  - el ensamblado TCP quedó en `receive.c`, compartido por el cerver y el cliente.

**El modelo actual.**
- Las cervers web **ignoran `handler_type`**: `cerver_register_new_connection_normal` (`handler.c:2852`) manda todo a
  `cerver_register_new_connection_normal_web` (`handler.c:2583-2655`).
- Cada conexión aceptada es un job bloqueante del thpool.
- **Si `thpool_is_full ()`, se crea un hilo desprendido nuevo** (`handler.c:2598-2615`). No hay tope, así que bajo
  carga (balance con handlers de 180 s) se crean hilos sin límite, con stacks de 8 MB.
- Cada petición hace unos 15 `malloc`: Connection, ConnectionStats y sus dos `packets_per_type`, CerverReceive,
  HttpReceive, `http_parser`, HttpRequest, file stats, Job, las Strings de URL, query y cabeceras, el body y la copia
  de `http_response_compile`.

**thpool (`threads/`):**

| Bug o ineficiencia | Dónde | Arreglo |
|---|---|---|
| `thpool_is_full` compara los hilos trabajando contra los vivos y no cuenta la cola | `threads/thpool.c:318-326` | cola **acotada**, con política configurable (esperar, rechazar con 503 + `Retry-After`). Se eliminan los hilos desprendidos |
| Un `malloc` y un `free` por job, aunque la cola tiene su pool de `Job` | `threads/thpool.c:339-340`, `:177`; `threads/jobs.c:302-319` | reutilizar el pool de jobs |
| Tres locks por push o pull (el `rwmutex` de la cola, el mutex de la dlist y el bsem), y el bsem binario funde los posts | `threads/jobs.c:342-358`, `:485-510` | un solo mutex con una condvar de contador, o un ring buffer acotado |
| Espera activa sin lock al arrancar, que se cuelga para siempre si falla `pthread_create` | `threads/thpool.c:248` | condvar y verificar `pool_thread_init` |
| `calloc (n, sizeof (PoolThread))` en lugar de `sizeof (PoolThread *)` | `threads/thpool.c:210` | corregir |
| `bsem_reset` vuelve a inicializar (`malloc`) el mutex y la condvar con hilos esperando | `threads/bsem.c:66-68` | reset sin realocar |
| `pthread_attr_destroy` nunca se llama | `threads/thread.c:29`, `:56` | llamarlo |
| `keep_alive` y los contadores son `volatile`, no atómicos | `threads/thpool.c` | `__atomic` |

**Accept y poll (`handler.c`):**
- `cerver->fds[i].fd == -1;` es una comparación, no una asignación, y su warning está silenciado con un pragma
  (`handler.c:3039`). Además el `realloc` se asigna sobre el mismo puntero, y `max_n_fds` se duplica antes del chequeo
  (`handler.c:3026-3031`).
- `poll ()` corre sin `poll_lock` mientras los jobs de accept hacen `realloc` del mismo arreglo (`handler.c:3350`): un
  use-after-free.
- `poll` recorre todos los slots en cada despertar y crea un `CerverReceive` (un `malloc` más búsquedas) aunque
  `revents == 0`.
- `switch (revents)` compara valores exactos, así que `POLLIN | POLLHUP` cae en el `default` (`handler.c:3258`).
- **Un `EINTR` en `poll` o en `accept` detiene el servidor** (`handler.c:3357-3366`, `:2961-2965`).
- `EMFILE` / `ENFILE` hacen un busy-loop con `perror`: hace falta un fd de reserva y backoff. `ECONNABORTED` no se
  maneja. `new_fd > 0` trata el fd 0 como error.
- Si falla `cerver_connection_create`, el fd aceptado se fuga (`handler.c:2930`, que tiene un FIXME).
- El backlog por omisión es 10 (`include/cerver/cerver.h:32`). Usar `SOMAXCONN` o un valor configurable.
- `SO_REUSEADDR` también activa `SO_REUSEPORT` (`network.c:194`), así que dos servidores pueden hacer bind al mismo
  puerto sin error. Hay que separarlos.
- `sock_set_blocking` está invertido (`network.c:46`), y `cerver.c:1424` depende de esa inversión.

**Ciclo de vida de conexiones y sockets:**
- `connection_delete` llama a `socket_delete ()` y después usa el socket en `connection_end ()`
  (`connection.c:190-196`).
- El pool de sockets reutiliza `Socket*` (`connection.c:583-599`), lo que crea un problema ABA: un puntero viejo en
  otro hilo actúa sobre el fd de una conexión nueva. El pool nunca se encoge y cada Socket carga dos mutex del heap.
- **La conexión se cierra al terminar el handler, no al responder** (`http/http.c:3017-3019`). Un handler que difiere
  su respuesta a otro hilo escribe sobre fd -1 o sobre un fd reutilizado. Se necesita un ciclo de vida explícito: el
  handler termina, la respuesta se envía, y entonces se cierra.
- `cerver_inactive_check` es recursivo y no verifica NULL (`cerver.c:2107`): segfault en cuanto se activa
  `inactive_clients`. Además recorre el AVL sin lock.
- `cerver_update` calcula el tiempo de frame solo con diferencias de `tv_nsec`, lo que falla al cruzar cada segundo
  (`cerver.c:2466`, `:2476`).
- Las carreras de tipo `dlist_end` que quedan: `queue_push` (`collections/queue.c:82`), `dlist_insert_at`
  (`collections/dlist.c:605`), `client_connection_add` (`client.c:588`) y `connection_register_to_client`
  (`connection.c:688`).
- `connect ()` con `sizeof (struct sockaddr)` rompe IPv6 (`connection.c:533-535`).

**Apagado:**
- `cerver_shutdown` solo hace `close (listen_fd)` (`cerver.c:2559-2588`). En Linux eso no despierta a un hilo
  bloqueado en `accept`; se necesita `shutdown ()` o un fd de despertar.
- `cerver_clean` libera las rutas del HTTP cerver (`cerver.c:2664`) y los clientes (`:2684`) **antes** de destruir el
  thpool (`:2726`). Los workers que siguen corriendo usan memoria liberada.
- `thpool_destroy` (`threads/thpool.c:370-396`) libera los jobs en cola sin correrlos: fuga sus `CerverReceive` y
  nunca cierra sus fds.
- **Drenado con deadline:** dejar de aceptar, terminar las peticiones en curso hasta N segundos y después cerrar. Es lo
  que necesita el rolling deploy del backend.

**Estado compartido y timeouts:**
- `isRunning` (`include/cerver/cerver.h:244`) y las estadísticas de `cerver->stats` (`handler.c:2268-2269`,
  `http/response.c:744`, `:866`) se actualizan desde muchos hilos sin atómicos: pasar a `__atomic`.
- `SO_RCVTIMEO` de 5 s es **por `recv`, no por petición** (`include/cerver/handler.h:195`): un cliente slowloris que
  manda un byte cada 4 s ocupa un worker para siempre. Se necesita un deadline por petición (cabeceras y body).
- No hay `SO_SNDTIMEO`, así que un lector lento bloquea un worker en `send` sin límite.

**Collections (`collections/`):**

| Estructura | Bug | Dónde |
|---|---|---|
| dlist | `insert_after` en medio de la lista nunca asigna `element->next->prev = le`: corrompe los enlaces hacia atrás | `collections/dlist.c:92-96` |
| dlist | `insert_in_order` solo maneja resultados -1, 0 y 1 del comparador (con `strcmp` no inserta nada) | `collections/dlist.c:704` |
| dlist | `dlist_get_element` ignora su `compare` y usa `dlist->compare` (crash si es NULL) | `collections/dlist.c:1048` |
| dlist | `sort` no actualiza `end`, `merge_two` no asigna `prev` y `split_half` no asigna `end` | `collections/dlist.c:1175`, `:254`, `:1295` |
| dlist | `remove_at` y `get_at` leen `size` fuera del lock; `search`, `get_element` y `copy` no hacen lock | `collections/dlist.c:902`, `:1062`, `:1082` |
| AVL | la doble rotación llama a `avl_balance_fix` con el nodo equivocado (NULL en el caso simple 3, 1, 2): segfault | `collections/avl.c:380`, `:415` |
| AVL | el borrado de un nodo con dos hijos no rebalancea: los factores derivan y rotaciones posteriores desreferencian NULL | `collections/avl.c:591` |
| htab | el hash es la suma de bytes módulo el tamaño, y la tabla nunca crece (128 para el mapa de clientes, `cerver.c:1555`) | `collections/htab.c:14-24` |
| htab | `calloc` con `sizeof (HtabBucket)` en lugar del puntero; el mutex solo se crea si `size > 0` | `collections/htab.c:241`, `:252` |
| htab | los lookups se detienen en cualquier nodo con `val == NULL`; un insert duplicado falla en silencio | `collections/htab.c:326`, `:432` |
| pool | `pool_create` nunca asigna `create` | `collections/pool.c` |

La dirección general es dejar de pagar un `malloc` por nodo y un mutex del heap por colección en el camino caliente:
listas intrusivas, arenas por petición y locks solo donde de verdad se comparte.

**Tests:**
- invariantes de dlist después de cada operación (recorrido hacia adelante y hacia atrás, `size`, `start`, `end`);
- AVL aleatorio con verificador de balance;
- colisiones de htab y reutilización de fd;
- `pool` y `queue` con 8 hilos × 1 M operaciones;
- ráfaga de thpool mucho mayor que el número de hilos, y destroy con la cola llena;
- TSan en todo lo anterior;
- el escenario de saturación de balance (§6).

**Criterio de salida:**
- TSan limpio en los unit tests;
- balance saturado responde 503 en lugar de crear hilos;
- `docker stop` drena sin cortar peticiones.

### 4.3 Fase 3 — Capacidades HTTP, con el registro de rutas como eje

**Estado (2026-11-05): implementada** en cerver, pycerver, pypercepthor y libauth, y **migrados** gepp, balance,
payloads y tickets (§4.3.4). tickets ya corre en test con la imagen devel; falta validar en test los otros tres.

**Alcance real:** el router y su semántica, más las cabeceras zero-copy y el envío con un solo `sendmsg`. Chunked,
Date / ETag / 304, request id, métricas y WebSocket pasan a la Fase 3b (§4.3.5).

**Decisiones de la planeación:**

| Tema | Decisión |
|---|---|
| Compatibilidad | La API vieja se conserva en `http/compatibility.c`, obsoleta. Se quita en la 3.0 (§4.4) |
| Prioridad | Estático > parámetro > comodín para todos, compat incluida. Las 11 rutas tapadas de los servicios empiezan a responder |
| Errores de registro | Fatales en la API nueva (`cerver_start` falla); warning en compat (gana la primera) |
| Nombres de parámetro | Viven en el endpoint, así que una posición puede tener varios nombres |
| `/` final | Tolerante por default; `strict` y `redirect` (308) configurables; `//` responde 404 |
| 405 | Opt-in, con un handler configurable y `http_receive_get_allow ()` |
| OPTIONS / CORS | OPTIONS automático (204 + `Allow`); el preflight CORS por grupo / endpoint es opt-in |
| Validadores | `objectid`, `int`, `uuid` y custom; un valor rechazado no hace match (404) |
| Middlewares | `before` / `after` en C y en Python, por cerver, grupo y endpoint |
| Python | Grupos + funciones y decoradores encima, en pycerver y pypercepthor |
| Archivos | Una palabra por archivo: `tree.c`, `middleware.c`, `validator.c`, `compatibility.c` |

**El diagnóstico de abajo corregido con el código de la Fase 2:**
- los desbordes de más de 16 segmentos y de más de 8 parámetros ya estaban protegidos;
- gepp registra 256 rutas (las ~500 son de indicators-service, en 11 top-level);
- `http_route_set_handler` sí se usa (8 llamadas en 6 servicios);
- las citas de línea se movieron.

#### 4.3.1 Rutas: el diagnóstico

Fuentes: `http/route.c`, `include/cerver/http/route.h`, y `http_receive_handle_select` /
`http_receive_handle_select_children` / `http_receive_handle_match` en `http/http.c:2460-2820`.

**Estructura y registro:**
- **Un solo nivel de anidación.** `http_route_init` (`http/route.c:434-499`) solo prepara a los hijos directos de cada
  ruta top-level. Un nieto (`http_route_child_add` sobre un hijo) calcula su `route->route` pero nunca se indexa.
- **Las rutas top-level no aceptan parámetros.** Se comparan con `strcmp` exacto (`http/http.c:2757`), así que
  `/users/:id` como top-level nunca hace match.
- **Se indexan por número de tokens en 16 cubetas fijas** (`DEFAULT_ROUTES_TOKENS_SIZE`,
  `include/cerver/http/route.h:15`):
  - una URL con más de 16 segmentos lee fuera de límites (`http/http.c:2555`);
  - una ruta hija con más de 16 tokens escribe fuera de límites en `http_route_init`;
  - el `calloc` de `routes` usa `sizeof (HttpRoute)` en lugar de `sizeof (HttpRoute *)` (`http/route.c:474`).
- **El auth, el modifier y el custom data son de la ruta, no del método.** Para tener el mismo path con métodos de
  distinta configuración hay que crear dos rutas, y entonces la primera registrada tapa a la segunda. Ningún servicio
  usa `http_route_set_handler`.
  - Esto **no** es una crítica a los paths de la API: endpoints como `POST /refris/:id/delete` son diseño de la API y se
    respetan. El router nuevo debe soportar cualquier estilo de path.
- **Sin validaciones al registrar:** no se detectan duplicados ni conflictos de parámetros, no se valida el formato
  (`//`, `/` final, `:` vacío) ni hay verificación al hacer `cerver_start`. Tampoco hay un error de registro que el
  usuario pueda ver.
- **Parámetros sin nombre.** `:id` se reemplaza por `*` (`http/route.c:460-467`). El handler los obtiene solo por
  índice (`http_request_get_param_at_idx`) y sin percent-decode. `REQUEST_PARAMS_SIZE` es 8
  (`include/cerver/http/request.h:49`) y los parámetros se escriben sin chequeo de límite (`http/http.c:2587`): una
  ruta con más de 8 parámetros desborda.

**El match en cada petición:**
- **Es lineal.** Recorre las rutas top-level y, dentro de la cubeta de tokens, todos los hijos con un `strcmp` por
  token.
  - gepp registra unas 500 rutas bajo una sola top-level.
  - Cada petición hace además `c_string_split` (`utils/utils.c:324`), con un `strdup` y un `malloc` por segmento.
- **El prefijo se busca con `strstr` en cualquier posición** (`http/http.c:2539`) y luego se recorta por longitud,
  como si estuviera al inicio:
  - `/x/api/…` hace match con `/api`;
  - `/apiv2/users` hace match con `/api`, porque no se verifica el límite de segmento.
- **Gana la primera ruta registrada.** Un `*` acepta cualquier segmento y no hay prioridad de estático sobre parámetro.
  Es la trampa del orden que el backend ya documentó.
- **`/` final y segmentos vacíos.** `c_string_split` colapsa `//` y descarta el `/` final, así que `/api/` no hace
  match y `/api//users` hace match con `/api/users`.
- **Métodos:**
  - `PATCH` (28 en http_parser) es mayor o igual a `HTTP_HANDLERS_COUNT` (8, `include/cerver/http/route.h:77`) y cae en
    catch-all;
  - una ruta que existe pero no tiene handler para el método cae en catch-all (200 o 404) en lugar de **405 con
    `Allow`**;
  - `HEAD` no usa el handler de `GET`;
  - no hay `OPTIONS` automático ni preflight CORS por ruta.
- Cada 404 escribe un warning (`http/http.c:2427-2431`).
- Las estadísticas por ruta toman un mutex en cada actualización (`http/route.c`, `http_route_stats_update`).

#### 4.3.2 Rutas: el diseño propuesto

**Árbol radix de segmentos, compilado en `cerver_start`:**
- cada nodo tiene hijos estáticos (un arreglo ordenado o un hash por segmento), un hijo de parámetro (`:name`) y un
  comodín final (`*path`);
- prioridad fija **estático > parámetro > comodín**, sin importar el orden de registro;
- anidación sin límite y parámetros en cualquier nivel, también en top-level.

**Match sin asignaciones:**
- se recorre la URL en su mismo buffer, segmento por segmento, como `(ptr, len)`;
- los parámetros quedan como slices en un arreglo del request, con tamaño configurable y límite verificado;
- el percent-decode es perezoso;
- el límite de segmento se verifica siempre.

**Handlers y configuración por `(ruta, método)`:**
- cada método tiene su auth type, su authentication handler, su custom data y su modifier;
- **herencia de grupos:** un grupo define el prefijo, el auth y el custom data por omisión, y sus hijos los
  sobrescriben.

**API de registro**, en el estilo `http_route_*` del repo:
- `http_route_create (path)` + `http_route_set_method_handler (route, method, handler)`, o atajos por método;
- `http_route_group_create (prefix)` para grupos;
- el registro devuelve error ante un duplicado, un conflicto o un path inválido, con un mensaje claro;
- `http_cerver_routes_print` muestra el árbol compilado.

**Parámetros por nombre:**
- `http_request_get_param (request, "id")`, además del índice;
- validadores de tipo opcionales en la ruta (`:id<objectid>`, `:n<int>`), útiles para los ObjectId de Percepthor, que
  responden 404 o 400 sin llegar al handler.

**Semántica HTTP:**
- `PATCH` y los demás métodos de http_parser (se extiende el X-macro `REQUEST_METHOD_MAP`,
  `include/cerver/http/request.h:26`);
- 405 con `Allow`;
- `HEAD` desde `GET` sin body;
- `OPTIONS` automático y preflight CORS por ruta o grupo, con la whitelist de orígenes existente;
- política configurable para el `/` final (estricta o redirección 308).

**El resto:**
- **Middlewares por ruta o grupo**, antes y después del handler: auth, request id, métricas, `max_body` por ruta
  (además del global de la Fase 1). Reemplazan el `switch` fijo de auth en `http_receive_handle_select`.
- **Estadísticas por ruta y método** con contadores atómicos.
- **Capa de compatibilidad.** `http_route_create` + `http_route_child_add` (con `:id`) se traducen al árbol nuevo y se
  marcan como obsoletas. Los servicios migran a su ritmo, y la capa se quita en la versión mayor.
  - pycerver expone la API nueva;
  - el paquete `percepthor` (`percepthor_route_create`, `_action_route_create`, `_role_route_create`) puede registrar
    auth y custom data por método;
  - **los paths de la API no se migran.**

**Tests:**
- de match, con tabla:
  - estático contra parámetro;
  - orden de registro indiferente;
  - `/` final y `//`;
  - más de 16 segmentos y más de 8 parámetros;
  - prefijo sin límite de segmento;
  - 405, `HEAD`, `OPTIONS`, `PATCH`;
  - grupos y herencia;
  - errores de registro;
- fuzzing de paths;
- **benchmark de match con las ~500 rutas reales de gepp**, el algoritmo actual contra el árbol.

#### 4.3.3 El resto de las capacidades HTTP

Lo que entró en la Fase 3:
- **Cabeceras:**
  - lookup por hash en lugar de los 56 `strcasecmp`;
  - las cabeceras custom viven en la arena, sin un `malloc` de 776 B cada una;
  - las conocidas ya estaban en la arena desde la Fase 2, una copia por cabecera, porque el buffer de `recv` se
    reutiliza.
- **Respuestas sin copias:** un solo `sendmsg` con la cabecera y el body (un `writev` con `MSG_NOSIGNAL`), en lugar de
  `malloc` + `memcpy`. Las respuestas ya compiladas se envían de su buffer.

#### 4.3.4 La migración de gepp, balance, payloads y tickets (hecha, 2026-11-05)

- El registro de sus `service/api/*.py` pasó a grupos + `percepthor_route` (pypercepthor), y salió de `service.py`
  a `service/api/service.py`. Los handlers, los paths y las respuestas no cambiaron; los parámetros se siguen leyendo
  por índice y las rutas no usan validadores.
- **La raíz de un grupo se registra al crearlo**: `percepthor_subgroup_create (api, "refris", REQUEST_METHOD_GET,
  refris_handler, **settings)` (y lo mismo `percepthor_group_create`), sobre `http_group_root_add (group, method,
  handler)` en C y `HttpGroup.create / subgroup / root` en pycerver. Nunca un `""` en un `percepthor_route`.
- **Admin en grupos**: un cerver sin rutas de la API vieja cuelga `cerver/info`, `cerver/stats`,
  `cerver/stats/filesystems` y `cerver/stats/workers` del primer grupo top-level (`http_cerver_admin_group_init`),
  con el auth de admin y no el del grupo; `http_cerver_init_admin_routes` es idempotente (`admin_routes_ready`) y
  `http_cerver_routes_compile` también la llama.
- **El dump del árbol**: `http_cerver_routes_generate_json` / `http_cerver_routes_json_delete` (pycerver:
  `http_cerver_routes_json`), más los bindings de `http_route_auth_type_to_string` y `http_route_modifier_to_string`.
- **La equivalencia la prueba cada servicio**: `tests/routes_expected.py` (método, path, handler, auth/acción,
  multipart) se generó del registro viejo y se cruzó con el árbol que compiló la compat; `tests/unit/test_api_routes.py`
  lo compara contra el árbol real. Verificado con tres mutaciones (un path, un `public`, un duplicado).
- Las dos rutas duplicadas de payloads pasaron a `:id/status/update/internal` y `dynamic/:id/status/update/internal`,
  como decían sus comentarios.
- Las suites se construyen con `PERCEPTHOR_IMAGE=itpercepthor/percepthor:devel ./tests/build.sh` (ARG en
  `tests/Dockerfile`) hasta publicar pypercepthor 0.7.0 y reconstruir `percepthor:latest`.
- Los otros 41 servicios corren con la capa de compatibilidad.
- Contexto completo: `percepthor-backend/docs/context/cerver-services-migration-session-context.md`.

#### 4.3.5 Fase 3b — Capacidades HTTP restantes (Beta 2.0b-60)

El uso principal de cerver es detrás de nginx: cada capacidad se prueba standalone y detrás de nginx (la versión de
producción, 1.31.6, y la stable), con el upstream en `keepalive`.

- **Cabeceras por petición.** Todas las respuestas salen por un solo `sendmsg` de cuatro partes: la cabecera compilada
  sin su `\r\n` final, las cabeceras de la petición, `\r\n` y el body. Así una respuesta compilada una vez (las de los
  servicios, las de error de cerver) recibe `Date`, `X-Request-Id`, CORS, `ETag` o `Content-Encoding` sin
  recompilarse y sin asignaciones. Una cabecera que la respuesta o el cerver ya tienen no se repite.
- **`Date`** siempre (RFC 9110 §6.6.1), cacheada por hilo y por segundo; `http_cerver_set_date_header` la apaga.
- **Chunked de entrada.** http_parser ya decodifica los chunks y rechaza `Transfer-Encoding` + `Content-Length`; los
  trailers se ignoran. Se prueba partido en cada byte, con multipart y con `Expect: 100-continue`.
- **Streaming:** `http_response_stream_begin / write / end` (chunked en HTTP/1.1, delimitado por el cierre en 1.0;
  HEAD sin body). Si el handler no lo termina, cerver lo termina.
- **ETag y 304:** los estáticos con ETag débil (tamaño y mtime) y `Last-Modified`; los handlers con el setting
  heredable `etag` (FNV-1a del body). `If-None-Match` manda sobre `If-Modified-Since`.
- **Compresión gzip** opcional (`ZLIB=1` en el makefile), con el setting heredable `compression`, un tamaño mínimo y
  los tipos comprimibles. Sin zlib la API existe y no comprime.
- **Request id** opt-in por cerver: el `X-Request-Id` entrante si es válido o un UUID v4; en la respuesta, en el log y
  con `http_request_get_id`.
- **Métricas Prometheus** en `[top level]/cerver/metrics` con el auth de admin: los stats actuales, las respuestas por
  código y un histograma de latencia por endpoint.
- **CORS automático:** los endpoints con `cors` agregan `Access-Control-Allow-Origin` y `Vary: Origin` a todas sus
  respuestas.
- **La IP real del cliente** detrás de proxies: `X-Forwarded-For` (de derecha a izquierda) o `X-Real-IP`, solo desde
  un proxy de confianza (`NONE` por default, una lista CIDR, las redes privadas o todos). En el log, con
  `http_request_get_client_ip`, y libauth la reenvía al servicio de auth.
- **`Connection: close` se mantiene** hasta el keep-alive de la Fase 4.

#### 4.3.6 Los arreglos sin el event loop (Beta 2.0b-61)

Llegan a `nightly` por la rama. Cada uno tiene su test, que se vio fallar contra la 2.0b-60.

- **El 500 de un handler sin respuesta.**
  - Antes, un handler que regresaba sin responder dejaba la conexión cerrada sin respuesta, y nginx contestaba 502.
    Pasaba, por ejemplo, con una excepción de Python en pycerver.
  - Ahora `HttpReceive.responded` se marca en `http_response_send_vector`, por donde pasa todo envío, al intentarlo:
    un cliente que ya se fue no recibe un segundo intento.
  - Después del dispatch, una petición sin respuesta recibe `server_error` con `Connection: close` y un warning con
    el método y el path.
  - Se cuenta en `n_unanswered_requests` y en la métrica `cerver_http_requests_unanswered_total`.
- **`poll (POLLOUT)` en `EAGAIN`.**
  - Un socket no bloqueante con el buffer lleno ya no se reintenta 16 veces en caliente, que truncaba la respuesta.
    Ahora espera con `poll` lo que queda de un plazo por envío igual al `send_timeout` del cerver, o
    `CERVER_DEFAULT_SEND_TIMEOUT` si es 0.
  - `HTTP_RESPONSE_SEND_RETRIES` se quitó.
  - Un socket bloqueante sigue igual: `SO_SNDTIMEO`.
  - Los archivos y el video comparten el mismo envío con `sendfile`. El video antes ignoraba `EINTR`, `EAGAIN` y los
    errores.
  - De paso, `fd > -1` al abrir archivos y videos, porque un fd 0 es válido.
- **La secuencia de los uploads.**
  - Los generadores default usaban `fd-time`. Dos peticiones con el mismo fd (reusado al cerrar la conexión) en el
    mismo segundo compartían el directorio y mezclaban sus archivos; los nombres repetidos se numeraban.
  - Ahora cada petición toma un número atómico del `HttpCerver` la primera vez que un generador lo pide: archivo
    `%d-%ld-%zu-%u-%s` (fd-time-secuencia-índice-nombre) y directorio `%d-%ld-%zu`.
  - Las firmas no cambian.
- **`gcc:16.2` / `debian:trixie-slim`** en los Dockerfiles, como `core`. Los warnings nuevos de gcc 16 se arreglaron
  como en `core`:
  - `file_read_internal` leía cada pedazo al inicio del buffer;
  - sobraba el contador de fps del update del admin.

  Los workflows no cambian.

#### 4.3.7 El port del core de la 1.8 (Beta 2.0b-63)

El core de `core` (de la 1.8b-1 a la 1.8b-3) entra a `http`. Los archivos sin HTTP se toman de `core` y en `handler.c`,
`handler.h`, `cerver.c` y `cerver.h` se conservan las partes web (el receive HTTP, el contexto por hilo, el 503, el
registro de las conexiones web y `cerver_create_web`).

- **La funcionalidad de la 1.x:** el balancer (`CERVER_TYPE_BALANCER`), connect async, estados de la conexión, cola de
  envío y la reconexión real del cliente.
- **Asignaciones:** `Connection`, `Socket`, `CerverReceive` y `Client` reciclados en free-lists acotadas, con sus
  stats y sus valores de sincronización dentro. GET de 8 a **2** asignaciones y POST JSON de 28 a **20**
  (`test/performance.sh`), con el mismo throughput.
- **Envío:** `http/response.c` usa `socket_send_vector ()` y `socket_send_can_retry ()` del core, sus helpers locales
  se quitaron.
- **Métricas:** los contadores y los histogramas del thpool (`ThpoolStats`) en `/cerver/metrics`.
- **IPv6:** `cerver_set_bind_address`, `cerver_set_ipv6_only`, las IPv4 mapeadas como IPv4 en las conexiones y en
  `http/proxy.c` (`NETWORK_IPV4_MAPPED_OFFSET`).
- **Teardown:** handlers joinables, eventos y conexiones detachable contados en grupos, backoff interrumpible y la
  gracia del drenado.
- **Bugs del core que también estaban aquí:** el orden de `CERVER_EVENT_CLIENT_CONNECTED` (el data race del TSan del
  push de la 2.0b-62), los paquetes sin handler que se fugaban, el `FileHeader` sin NUL o con `../`, el info del cerver
  sin NUL, el `total_sent` de los envíos, `file_send_actual` con un solo `sendfile`, y los de la lista de
  `branches.md` §5.
- **Tests:** los del core (`routing`, `socket`, `network`, `client/{features,files}`, `cerver/lobby`, los pares
  `cerver/queue` y `balancer/*`, los fuzzers `receive`, `packet`, `client` y `file`), y las métricas del thpool en
  `test/http/features.c`.
- **Workflows:** los de `nightly` (`container: gcc:16.2`, `scheduled.yml`, nombres de release e imagen por rama), con
  `--sysctl net.ipv6.conf.all.disable_ipv6=0` en los contenedores de tests.

#### 4.3.8 El port de la 1.8b-4 y la 1.8b-5 (Beta 2.0b-64) — aprobado e implementado, sin commits

**El estado (2026-11-09):**

- **Sembrados desde `core`:** 63 archivos más el corpus `datagram`.
- **Mezclados a mano:** `cerver.{h,c}` y `handler.{h,c}`. Contra `core` solo les queda el bloque web.
- **En http:**
  - los pares `web/*` y nginx corren en las dos familias (`test/address.h`, `web_test_address ()` en
    `test/client/web/curl.c`, nginx con `listen [::]:80` y el cerver en `::`);
  - `proxy.c` ignora la zona al comparar con las redes de confianza;
  - `HTTP_CLIENT_IP_SIZE` y `HTTP_REQUEST_CLIENT_IP_SIZE` pasan a `NETWORK_ADDRESS_STRING_SIZE`.
- **Lo que no cambió:** el admin de workers y los pools de response, multipart y JWT no necesitaron cambios.
- **Medido con `test/performance.sh`:** GET pasa de 2 a **1** asignación por request y POST JSON de 20 a **19**
  (los umbrales nuevos), con 101–143k req/s en arm64.

**Los pendientes de la 1.8b-5, hechos en http primero (2026-11-09)**, para commitear la 2.0b-64 final. Después se
llevan a `core` como la 1.8b-6. El plan está en `~/.claude/plans/quiero-hacer-el-plan-refactored-cookie.md`.

- **El core:**
  - el mutex de cierre del `Socket`: `socket_close`, `socket_replace` y `socket_shutdown`;
  - el chequeo de inactivos, que hace los shutdowns fuera del lock de clientes;
  - la sincronización en `connection_connect`, sin el `sleep` de respaldo;
  - un bug que apareció de paso: los clientes con un connect fallido, o que nunca conectan, dejaban su fd abierto.
- **Los tests:**
  - el fuzzer `address`;
  - `test/allocations.sh`, que mide por conexión y por paquete de un handler;
  - los pares de proceso del balancer, `balancer/{reconnect,large,unavailable,concurrent,abandon}`;
  - `test_run ()` en los tests del servidor y de los peers, y los logs de las corridas que fallan en `repeat.sh`.
- **Los examples:** se portan los del balancer y el servicio con su `examples/app` (target `services`), el reloj
  portable de `benchmarks/bench.h`, el guard AVX2 de `base64.c` y `benchmarks/handler.c`.

El plan aprobado:

**Por qué:** antes de que `nightly` empiece su F3, `http` tiene que tener el core completo de la 1.8 hasta la b-5. Lo
que más le sirve a HTTP:
- el `Pool` sin un malloc por push: lo usan `http_response_get`, los multiparts, el JWT y los logs;
- el connect por dirección, las zonas y `sock_ip_equal` normalizado;
- el hilo de logs joinable;
- los handlers con jobs del pool.

**Las decisiones del usuario (2026-11-09):**

| Tema | Decisión |
|---|---|
| UDP | **Se porta también** (cambia la decisión de `branches.md` §5): los archivos se siembran completos y `http` no diverge del core |
| Siembra | Se copian los archivos del árbol de `core`, sin commits todavía, con la excepción de siembra de `manual-edits-only`; las mezclas y los cambios propios de http se hacen a mano |
| Validación | El gate del repo completo, sin despliegue en test (ese va con los commits) |
| `::1` | Todos los pares, también `web/*` y nginx, corren en las dos familias |
| Git | Ninguna acción: sin commits; lo que salga del gate de `core` se arregla en los dos lados |

**Las etapas:**

1. **La siembra desde `core`**, archivo completo:
   - `udp.{h,c}`, `network.{h,c}`, `connection.{h,c}`, `packets.{h,c}`, `client.{h,c}`, `auth.{h,c}`, `sessions.h`,
     `admin.c`, `socket.c`;
   - `collections/pool.{h,c}`, `threads/{jobs,worker,manager,thpool}.{h,c}`, `utils/log.c`, `version.h`;
   - lo que la diferencia contra `http` muestre que se tocó en la 1.8b-4 / b-5 sin partes web.

   Se reportan todos los archivos sembrados.
2. **Las mezclas a mano:**
   - `handler.{h,c}`: el de `core` más el bloque web (receive HTTP, contexto por hilo, 503, registro web);
   - `cerver.{h,c}`: más `cerver_create_web`, los casos `CERVER_TYPE_WEB` y `http.h`.

   El UDP y los web conviven en el switch por tipo.
3. **Lo propio de HTTP:**
   - **Buffers de ips:** cada llamada a `sock_ip_to_string_actual` en `http/` (`proxy.c`, los logs de acceso, el
     admin) pasa a buffers de `NETWORK_ADDRESS_STRING_SIZE`. Con una zona, un buffer de `INET6_ADDRSTRLEN` se
     desborda.
   - **`proxy.c`:** compara las direcciones con `sock_ip_equal`.
   - **El admin de http:** se revisa contra el `Worker` nuevo (`worker_get_state` público, hilo joinable).
   - **Los `Pool` de response, multipart y JWT:** se revisa si alguno dependía del orden FIFO.
4. **Los tests:**
   - los del core (`network`, `cerver/core`, `client/features`, `collections/pool`, `threads/{jobs,worker,manager}`,
     `utils/log`, `udp`, `cerver/peers`, el par `cerver/udp` y el fuzzer `datagram`);
   - `CERVER_TEST_ADDRESS` en los servidores y los clientes de los pares `web/*`: las URLs de IPv6 van con corchetes
     (`http://[::1]:7000`);
   - `integration.sh` en las dos familias, y nginx con su upstream en `CERVER_TEST_ADDRESS`.
5. **Los docs y la versión:**
   - `incompatibilities.md` (la entrada 2.0b-64 con las de la 1.8b-4 / b-5 que aplican);
   - `CHANGELOG.md` y la versión `2.0b-64`;
   - esta sección, `test/roadmap.md` y `CLAUDE.md`;
   - `branches.md` §5 en `core`.

**La verificación (el gate del repo):**
- gcc / g++ en los TYPEs, con los 6 warnings conocidos de `jwt/openssl.c`;
- `run.sh` normal, con ASan+UBSan, con TSan y con valgrind;
- integración × {IPv4, `::1`} × {normal, ASan, TSan, `COVERAGE=1`};
- fuzz de todos los targets × 30 s;
- nginx 1.31.6 y 1.30.6;
- `performance.sh`, con la meta de que GET siga en 2 y POST JSON baje de 20 por el `Pool`;
- `repeat.sh` de `test/bin/http`, `web/features` y `cerver/core`.

Cada test nuevo debe fallar contra la 2.0b-63.

#### 4.3.9 El cliente que se desconecta (Beta 2.0b-65) — aprobado e implementado, sin commits

**La decisión (2026-11-10):** el port de la 1.8b-7 y de las betas siguientes de la 1.8 espera hasta la beta que vaya
a salir a producción. Esta beta prueba qué hace cerver cuando un cliente HTTP se va en cualquier punto, en cuatro
formas (FIN, RST, half-close y desaparecer), también detrás de nginx con una y con dos instancias.

**La librería:**

- Las etapas (`HttpStage`: queued, request line, headers, body, upload, handler, response) se llevan en el
  `HttpReceive` y cambian en los callbacks del parser, del multipart y del envío.
- Los contadores separados de los errores del parser:
  - `n_aborted_requests[stage][reason]`: el cliente se fue, `closed` (un EOF) o `reset`;
  - `n_vanished_requests[stage]`: un timeout terminó la petición;
  - `n_failed_responses`: un envío falló. La petición queda en `RESPONSE_FAILED` y se cuenta en la etapa donde se
    notó: `handler` si no salió nada de la respuesta, `response` si ya había salido una parte.
- Las métricas `cerver_http_aborted_requests_total{stage,reason}`, `cerver_http_vanished_requests_total{stage}` y
  `cerver_http_failed_responses_total`. El JSON de admin y `http_cerver_all_stats_print ()` tienen todos los contadores.
- Los timeouts en milisegundos: `http_cerver_set_{headers,request,idle}_timeout_milliseconds ()`. Los setters de
  segundos se conservan. El de inactividad reemplaza los 5 s fijos del receive HTTP.
- El estado del cliente para el handler: `http_receive_get_client_state ()` (un `poll` con `POLLRDHUP` y un `recv`
  con `MSG_PEEK`, sin consumir datos) y `http_receive_is_client_gone ()`, con `http_cerver_set_half_close_is_gone ()`.
- El stream que falla pasa a `HTTP_STREAM_STATE_FAILED` y no manda nada más.
- Los uploads: una petición incompleta nunca deja archivos ni su directorio, y el directorio que queda vacío se
  borra.
- **Bugs de paso:**
  - el discard borraba el archivo ajeno con el nombre que una parte no pudo abrir;
  - un `recv` interrumpido por una señal terminaba la petición;
  - un nombre de upload que no cabía se truncaba;
  - un generador sin nombre guardaba en `uploads/`;
  - el `multipart_parser_init` y el `mkdir` del directorio generado no se revisaban;
  - el `100 Continue` que no salía se ignoraba.
- **Al backlog:** cancelar el handler cuando su cliente se va, y el `TODO` de autenticación de los archivos estáticos.

**Los tests:** la etapa J de `test/roadmap.md`:

- `test/http/disconnect.c`, la matriz;
- `test/disconnect.sh`, el nivel completo en las variantes;
- `test/web/disconnect.c` y `test/client/web/disconnect.c` con tres configuraciones de `test/nginx/`;
- tres tests de uploads en `test/http/receive.c`.

**La verificación:**

- la matriz completa en normal, ASan+UBSan y TSan, y la muestra con valgrind, en las dos familias;
- nginx 1.31.6 y 1.30.6 × 3 configuraciones × 2 familias;
- los mutantes de cada arreglo;
- el gate habitual;
- pycerver 1.1 contra la imagen devel;
- artemis con gepp, payloads, tickets y balance.

#### 4.3.10 El port de la 1.8 completa y los pendientes de la 2.0b-65 (Beta 2.0b-66) — implementado, sin commits

**La decisión (2026-11-11):** el port de la 1.8b-7, la 1.8b-8 y la 1.8b-9 (y los arreglos del release 1.8) se adelanta
a esta beta, que todavía no es la de producción. El core de `http` queda idéntico al de `core` salvo la parte web de
`cerver.{h,c}` y `handler.{h,c}`.

**El port:**

- El protocolo de 32 bytes en orden de red, el `route_id` con generación del balancer (cierra B9 en `http`), el
  serializador por esquema y sus objetos (`CerverSummary`, `SessionToken`, `ErrorMessage`, `FileHeader`).
- Los issues de la 1.8b-8: histogramas, `HandlerStats`, `RequestStats`, los handlers por request type, el balancer
  custom, `system.h`, los pools de paquetes por hilo, `files_copy*`.
- Game de la 1.8b-9, el `EINTR` en el receive, el thpool solo en THREADS para los cervers del protocolo (un cerver web
  lo crea con cualquier tipo de handler).
- **Cero warnings** con gcc, g++ (sin `-fpermissive`) y clang, en `test` y `development`, la librería, los tests, los
  examples, los fuzzers y los benchmarks, los vendorizados incluidos; `WERROR=1` en el CI, el job de clang, y
  `jwt/openssl.c` sin la API de `EC_KEY`.
- Los workflows con las actions de Node 24.

**Los pendientes de la 2.0b-65:**

- El valgrind del CI en `test_disconnect_responses`: las esperas del lote ×4 bajo valgrind.
- `test_disconnect_interrupted` bajo emulación amd64: el número de la llamada de `recv ()` se mide en tiempo de
  ejecución con un hilo que la espera.
- El send timeout por respuesta, en milisegundos: `http_cerver_set_send_timeout_milliseconds ()`; todos los envíos de
  una respuesta comparten un deadline, también en un socket bloqueante con un `SO_SNDTIMEO` más largo, y cada pieza de
  un stream tiene el suyo.
- La autenticación de los archivos estáticos: antes de buscar el archivo, con la ruta estática que lo tiene o con la
  primera protegida si ninguna lo tiene; `http_static_path_set_decode_data ()` y
  `http_static_path_set_authentication_handler ()`.
- `files_create_recursive_dir ()` (del core): un directorio que existe es éxito y una ruta que no cabe es error.

**Los tests:** `test_receive_static_*_authentication`, `test_server_response_send_timeout`, el thpool en
`test_server_poll`, `test/socket.c` y `test/files.c` del core, y todo lo de las tres betas de la 1.8 (game, schemas,
stress, histogram, system, los fuzzers `serializer` y `game`, los pares `cerver/game`, `cerver/schemas`,
`balancer/custom` y `examples/*`).

#### 4.3.11 La seguridad de JWT y JSON (Beta 2.0b-67) — implementado, sin commits

Con dos hallazgos del CI de la 2.0b-66 (PR #403): el `va_copy` de `cerver/config.h` que rompía `json_pack ()` /
`json_unpack ()` con clang en x86-64, y el send timeout de valgrind en `test/http/disconnect.c` (16 → 64 s).

**El diagnóstico (2026-11-11):** una auditoría del JWT (libjwt ~1.12 vendorizado, MPL-2.0) y del JSON (jansson ~2.13
vendorizado, MIT) encontró:

- **JWT:**
  - un desborde de 1 byte en `jwt_encode` con un token sin valores;
  - `http_cerver_auth_validate_jwt` lee fuera del string y crashea sin llave;
  - `bearer_len` / `json_len` reportan más bytes de los que hay;
  - el HMAC se compara con `strcmp`;
  - `jwt_decode` devuelve éxito sin verificar si falla la copia de la llave;
  - una llave vacía apaga la verificación;
  - nada revisa que la llave corresponda al algoritmo;
  - el PEM se re-parsea en cada petición.
- **JSON:**
  - el seed del hash es `time (NULL)` y se escribe sin sincronizar (los `HAVE_*` de upstream nunca se definen);
  - las respuestas JSON de cerver se arman con `printf` sin escapar;
  - cuatro helpers liberan el buffer que la respuesta todavía usa;
  - `json_dumpfd` nunca funciona;
  - el parser no tiene límite de memoria (50–70× el body);
  - el `strtod` depende del locale (un `assert` alcanzable);
  - toda la librería se exporta sin visibilidad.

**Las decisiones:** tres betas, una por sesión: **2.0b-67 la seguridad**, 2.0b-68 los objetos calientes (§4.3.12),
2.0b-69 el estilo (§4.3.13). Los archivos compartidos con `core` (`utils/base64.c`) se llevan a `core` en la 1.9; el
JSON se muda al core (issue #207) en la 1.9.

**El JWT:**

- El encode con los tamaños de `base64_encode_len ()`, sin `alloca`.
- La API de `auth.c`:
  - el prefijo `Bearer ` validado;
  - las llaves que faltan dan error;
  - un bearer o un JSON que no caben son un error;
  - el JSON con valor se arma con la librería.
- **La verificación:**
  - el HMAC se compara en tiempo constante (`CRYPTO_memcmp`) sobre la firma decodificada;
  - `jwt_decode` rechaza un algoritmo distinto al esperado antes de verificar;
  - el PEM se parsea una vez a un `EVP_PKEY` al cargar la llave;
  - la llave tiene que corresponder al algoritmo (HS con un secreto del tamaño del hash, RS con 2048 bits o más, ES con
    su curva), o `cerver_start` falla;
  - las llaves se borran con `OPENSSL_cleanse`;
  - `JWT_ALG_NONE` no se acepta en HTTP: `http_cerver_auth_set_jwt_algorithm ()` devuelve `u8`.
- **El parseo estricto:**
  - base64url canónico (`base64url_encode` / `base64url_decode`);
  - un tamaño máximo de token (2^13, configurable);
  - los nombres de algoritmo sensibles a mayúsculas;
  - el header y el payload son objetos sin llaves repetidas;
  - las firmas ECDSA salen con S bajo.
- **La expiración** (por default igual que antes):
  - `exp` requerido opcional;
  - un leeway en segundos;
  - `iss` / `aud` esperados opcionales;
  - un `nbf` que no es entero falla;
  - los valores enteros del token son `int64_t`.

**El JSON:**

- El seed con `RAND_bytes ()` al iniciar HTTP, con la versión atómica de `json_object_seed ()`.
- Las respuestas JSON de cerver con `json_pack ()` (con escape, los reales sin locale, `NaN` es error), dueñas de su
  buffer.
- Un presupuesto de memoria por hilo durante el parseo, proporcional a la entrada.
- El `strtod` y los reales con un locale "C" propio.
- `json_dumpfd ()`.
- Menores: la profundidad en el dump, el underflow, el error de UTF-8, el `i` de unpack fuera de rango, los overflows
  de tamaño, `json_print ()`.

**Los símbolos:**

- `CERVER_PRIVATE` es oculto (`-fvisibility=hidden`); los internos del JSON y del JWT pasan a `CERVER_PRIVATE` o
  `static`.
- Los `json_*` siguen públicos: libauth los usa.
- Los avisos de licencia de jansson y libjwt.

**Los tests:** cada arreglo con su test visto fallar contra la 2.0b-66; los fuzzers de JWT con HMAC, ES y el round
trip, y uno de la API de `auth.c`; el de JSON con los flags y pack / unpack.

#### 4.3.12 Los objetos calientes de HTTP (Beta 2.0b-68) — implementado

Las 19 asignaciones de POST JSON de `performance.sh`:
- 1 `HttpRouteFileStats` en cada petición (la única de GET);
- 2 del body (`String` + su reserva);
- 1 `HttpResponse` de ~16.7 KB que se limpia con `memset`;
- 1 `vasprintf` de la cabecera;
- 14 del `json_loads ()` del handler.

**El plan:**

- Los stats de archivos dentro de `HttpReceive` o en la arena (GET 1 → 0).
- El body en la arena hasta un tope, o en una caché por hilo de clases potencia de 2, con el patrón de los paquetes
  (`packets.c`): un puntero `__thread`, un destructor de `pthread_key` que vacía a la `FreeList` global, y límite 0
  con `CERVER_RECYCLE_DISABLED`.
- `HttpResponse` en una caché por hilo + `FreeList`, midiendo si la tabla de cabeceras (64 × 256 B) se puede achicar.
- La cabecera con `snprintf` en la arena.
- **Un A/B:** una caché por hilo por clases detrás de `jsonp_malloc` / `jsonp_free`. Se queda solo si mejora las req/s
  más que la dispersión, medido en x86 y aarch64.
- Los umbrales nuevos de `performance.sh`.
- Las issues #182, #328 y #145.

**Lo que se hizo (2026-11-11):**

- `http/recycle.{h,c}` (nuevo, privado): una caché por hilo de `HttpResponse` y de búferes en clases de 2^8 a 2^16,
  con el patrón de `packets.c` (`__thread`, `pthread_key`, `FreeList` compartidas, límite 0 con
  `CERVER_RECYCLE_DISABLED`).
- `HttpResponse` se recicla y lleva un `header_buffer` de 2^8 donde se escribe su cabecera cuando cabe (la sencilla, la
  de JSON y la de varias cabeceras): sin `calloc` ni `vasprintf`. La tabla de 64 cabeceras solo se limpia cuando se
  usó (`n_headers`), así que una respuesta reciclada no hace el `memset` de ~16 KB; achicar la tabla cambiaría la API
  de los tipos de cabecera y no hace falta.
- El body: su `String` en la arena de la petición y su búfer de una clase reciclada, reservado una vez desde el
  `Content-Length`.
- Los stats de archivos de la petición en la arena del hilo.
- **Resultado:** GET 1 → 0 y POST JSON 19 → 14 asignaciones por petición (las 14 son del `json_loads ()` del
  handler).
- **Las issues:** #182 ya lo resolvía la arena del hilo (`http_receive_create_arena`), #328 el pool de `MultiPart`
  (`multi_parts_pool`, con sus campos en búferes fijos) y #145 la `FreeList` de `CerverReceive` (`handler.c`).
- **El A/B de `jsonp_malloc` no se hizo:** el búfer de `json_dumps ()` sale de `jsonp_strdup ()` y quien lo recibe lo
  libera con `free ()` (las respuestas, admin, el árbol, auth, libauth y los handlers), así que una caché por clases
  necesita un prefijo de tamaño que rompería esos `free ()`. Queda para el allocator propio del JSON (backlog,
  «Implementaciones propias en lugar de los vendorizados»). Además la Mac estaba cargada por otra sesión: x86 y la
  dispersión de las req/s quedan pendientes.

#### 4.3.13 El estilo de JWT y JSON (Beta 2.0b-69) — implementado

Las convenciones de cerver completas en el código vendorizado:

- **Los nombres:** palabras completas (`strbuff`, `new_thing`, `js`, `hdr`, …).
- **El flujo:** helpers estáticos en lugar de los `goto` y de los macros con `return` (`SIGN_ERROR`, `VERIFY_ERROR`,
  `APPEND_STR`).
- **El formato:** constantes nombradas, `#pragma region`, la línea en blanco después de `{`, los guards
  `_CERVER_HTTP_JSON_*_H_`.
- **El JWT:**
  - `JWT_ALG_MAP` con el digest y el tipo de llave en lugar de los seis `switch`;
  - sin `jwt/config.h`, sin los `pfn_*` y sin el bloque de OpenSSL < 1.1.
- **Lo que se conserva:** los headers de licencia y el lookup3 de Bob Jenkins.
- **La validación:** objetos idénticos donde el estilo no cambia código.

**Lo que se hizo (2026-11-11):**

- **El JWT completo:** `jwt.c` reescrito con el estilo de cerver (nombres completos, regiones, constantes con nombre
  para los claims y los headers, helpers de los valores de JSON que comparten los grants, los headers y la
  validación, `jwt_dup ()` sin `goto`, `jwt_validate ()` en tres helpers); sin `jwt/config.h` ni los `pfn_*` (nunca se
  podían fijar: no había `jwt_set_alloc ()`); `jwt_t.key_length`, `jwt_valid_t.headers` y `required_grants`. Los
  `switch` del algoritmo ya los había quitado la tabla de `jwt/key.c` (2.0b-67); el de `alg.c` es el del X-macro.
  El bloque de OpenSSL < 1.1 ya no existía desde la 2.0b-66.
- **El JSON:** `value.c` y la tabla de `hashtable.c` reescritos (el lookup3 igual); **ningún `goto` queda en el
  JSON**: los 43 de `json.c` (el lexer, el parser, `pack_*` y `unpack_object`) pasaron a helpers; sin los
  `JSON_HAVE_*` ni `JANSSON_USING_CMAKE`; los parámetros de `value.h` y `private.h` con nombres completos.
- **La validación:** los símbolos exportados son idénticos a los de la 2.0b-68 y `internal.o` (solo macros) compila
  al mismo código; los demás archivos cambian código y los cubren los tests de JSON y JWT y su fuzz de 300 s.
- **Para la 2.0b-70:** los nombres del resto de `json.c` (`s`, `c`, `ap`, `fmt`, `strbuff`, `pos`, `str`, `len`, …:
  unos 800) y los de `json.h`, que van juntos.

### 4.4 Fase 4 — Núcleo event loop (versión mayor 3.0, para después)

Es la implementación mayor. Llega cuando las Fases 0–3 estén estables en producción.

- **Un epoll no bloqueante por worker**, con `SO_REUSEPORT` y `accept4 (SOCK_NONBLOCK | SOCK_CLOEXEC)`. Reemplaza el
  accept bloqueante, el modo `POLL` y el hilo por conexión.
- **Una máquina de estados por conexión** con un buffer reutilizable: leer cabeceras, leer body, despachar, escribir,
  idle.
- **Rueda de timers**: timeouts de cabeceras, body, idle y petición.
- **Keep-alive real.** Entonces `HTTP_RESPONSE_CONNECTION_CLOSE_HEADER` pasa a ser condicional: `Connection: close`
  solo cuando se va a cerrar. Hay que reiniciar `HttpReceive` / `HttpRequest` entre peticiones (hoy no se reinician:
  `HttpReceive.keep_alive`, `include/cerver/http/http.h:609`, nunca se activa).
- **Un pool acotado de handlers bloqueantes, separado del I/O.** Es indispensable para Python, donde el GIL serializa
  los handlers. Tiene backpressure: si el pool y su cola están llenos, se responde 503 sin ocupar un hilo de I/O.
- `writev` y `sendfile` no bloqueantes. Respuestas diferidas desde otros hilos, con un ciclo de vida seguro.
- TLS en proceso (OpenSSL ya se enlaza, hoy solo para JWT) y, más adelante, HTTP/2.
- **WebSocket** (movido de la Fase 3b): en el modelo de un hilo por conexión, cada WebSocket ocuparía un hilo del
  thpool toda su vida. Con el event loop es una conexión más. Las rutas con ese modifier responden 501 hasta entonces.
- **Una ruta de healthcheck fuera del pool** (movida del backlog): hoy, con la política `wait`, `/version` espera en
  la cola y el contenedor queda `unhealthy` mientras dura la saturación.
- **pycerver 1.0** alineado con la API 3.0.
- **La migración completa, sin retrocompatibilidad:**
  - los 47 servicios registran sus rutas con grupos y endpoints;
  - se quitan `http/compatibility.c` y la API `http_route_*` vieja (`HttpRoute`, `http_cerver_route_register`,
    `http_route_child_add`, los params solo por índice);
  - pycerver y pypercepthor quitan sus bindings viejos (`percepthor_*_route_create`).

  En la 3.0 también se puede rediseñar la firma del handler (un contexto único en lugar de `(HttpReceive *,
  HttpRequest *)`), porque todos los servicios se migran a la vez.

### 4.5 Fase 5 — En cada fase

- **pycerver:** cada cambio de API, struct o firma lleva su binding en el módulo espejo (`py-cerver/cerver/…`), con
  `argtypes` y `restype` verificados contra el header. Se verifica con `import` en la imagen devel.
- **Versión:** el commit dedicado "Updated version to Beta 2.0b-N" actualiza `version.txt`,
  `include/cerver/version.h`, `ARG CERVER_VERSION` del `Dockerfile` y `cerver-install.sh`, y reemplaza
  `CHANGELOG.md`.
- **Validación devel** en test (§6).
- **Documento de contexto** en `percepthor-backend/docs/context/`.

### 4.6 Resumen de prioridades

| Prioridad | Qué | Por qué |
|---|---|---|
| 1 | Fase 0 | sin tests confiables, ni sanitizers, ni la cadena devel, ninguna otra fase se puede validar |
| 2 | Fase 1 (body y seguridad) | es el bug que todos los servicios pagan con el buffer, y hay crashes y escrituras remotas |
| 3 | Fase 2 (thpool, apagado, collections) | balance y los handlers síncronos agotan el pool; el rolling deploy necesita drenado |
| 4 | Fase 3 (rutas y HTTP) | las ~500 rutas de indicators y las 256 de gepp en match lineal, la trampa del orden, 405 / `PATCH` / `HEAD` |
| 4b | Fase 3b (chunked, streaming, `Date`, ETag, compresión, request id, métricas, CORS, la IP real) | lo que completa HTTP/1.1 detrás de nginx antes del event loop |
| 5 | Fase 4 (event loop) | el techo de rendimiento, cuando lo anterior esté estable |

---

## 5. Plan de tests

### Lo que hay

- **Unit:** `types`, `cerver/test`, `client/test`, `connection`, `collections`, `files`, `packets`, `receive`,
  `system`, `threads`, `utils`, `http`, `json`, `jwt`, `version`. No hay framework: `test/test.h` define macros
  `test_check_*`.
- **`test/app`:** una librería compartida de handlers, rutas, usuarios y auth.
- **Integración:** pares servidor / cliente:
  - TCP: `test/cerver/*` contra `test/client/*`;
  - HTTP: `test/web/*` contra `test/client/web/*`, con libcurl.

  Los corre `test.sh` en la imagen `ermiry/cerver:test`.
- **`benchmarks/`:** `http-parser.c` (el parser con callbacks vacíos), `base64.c` y `web.c` (libcurl contra
  `localhost.com:8080`).

### Estado (la suite de tests)

Las Fases 0–3 y la suite de tests cubrieron casi todo lo que esta sección listaba como hueco: el parseo HTTP de
punta a punta, el árbol de rutas, multipart, el cerver TCP en proceso, el ciclo de vida del cliente, los sanitizers en
la integración, el fuzzing, la regresión de rendimiento y nginx delante. El detalle, lo que quedó pendiente y los bugs
que encontró están en [`test/roadmap.md`](test/roadmap.md).

### Tests nuevos

**Unit:**

| Test | Fase |
|---|---|
| `pool` concurrente (el arreglo de `pool_push`) | 0 |
| Feeder split-buffer del parser en todos los offsets, y 1 byte por `recv` | 1 |
| Pipelining, cabeceras duplicadas, Authorization corto, `..` en estáticos, cada límite | 1 |
| Invariantes de dlist, AVL aleatorio con verificador, colisiones de htab y reutilización de fd | 2 |
| `pool` / `queue` con 8 hilos × 1 M operaciones; ráfaga de thpool; destroy con la cola llena | 2 |
| Tabla de match de rutas, errores de registro, 405 / `HEAD` / `OPTIONS` / `PATCH` | 3 |

**Integración y estrés** (en Docker, contra `test/web/*` y los ejemplos):
- wrk o h2load con 1 k conexiones: 0 respuestas no 2xx y 0 RST;
- **nginx delante** como proxy, la regresión del 502 (con la configuración de `nginx/` del repo);
- slowloris (1 byte cada 4 s), lector lento (nunca lee la respuesta), RST del cliente a mitad de la respuesta
  (`SIGPIPE`);
- body de 100 MB y cada límite;
- multipart: sin boundary, boundary partido entre lecturas, filename vacío, filename con `../`, archivo de destino
  existente;
- `ulimit -n 64` (`EMFILE`), SIGINT bajo carga (drenado).

**Fuzzing:** harnesses de libFuzzer para http_parser con los callbacks de cerver, multipart, `http_url_decode`, el
loader JSON, el decoder JWT y el match de rutas.

**Benchmarks:**
- ampliar `benchmarks/` con wrk contra `examples/web/web.c`;
- una línea base por fase (req/s, p50 / p99, `malloc` por petición);
- h2o y drogon con el mismo hardware como referencia.

**Convenciones adoptadas del backend:**
- cada bug arreglado lleva su test;
- cada suite documenta "los bugs que la suite fijó" y "los huecos de cobertura";
- nunca se finge el framework nativo.

---

## 6. Workflow de validación devel

Es la generalización del workflow con el que se encontró y validó el 502 de gepp.

### 6.1 Imágenes

1. **Base devel, solo local:** `percepthor-devops/devel.sh` → `itpercepthor/percepthor:devel`.
   - `docker build --platform=linux/amd64` con build contexts `cerver=/Users/ermiry/Documents/cerver`,
     `auth=/Users/ermiry/Documents/Work/libauth` y (desde la Fase 0) `pycerver=/Users/ermiry/Documents/py-cerver`.
   - `Dockerfile.devel` compila cerver con `make TYPE=${CERVER_TYPE} DEFINES="${CERVER_DEFINES}"` (por omisión `beta` y
     `-D _GNU_SOURCE`; los defines de debug solo para depurar) y libauth con `TYPE=production`, sobre `gcc:13.4`. El
     runtime es `python:3.12.10-slim-bookworm`.
2. **Imágenes de servicio:** `FROM itpercepthor/percepthor:devel`, construidas y empujadas desde la Mac con
   `devel.sh` como `…/<servicio>:devel` en GCP Artifact Registry.
   - gepp-service y balance-service ya tienen su `Dockerfile.devel` y `devel.sh`.
   - payloads-service los recibe en la Fase 0.

### 6.2 Gate local

- Unit tests de cerver en Docker: `make TYPE=test && make TYPE=test test && bash test/run.sh`. Desde la Fase 0, también
  con `SANITIZER=address` y `SANITIZER=thread`.
- Las suites de cada servicio en su imagen: `tests/build.sh`, luego `./run.sh` (`unit`, `integration`).
- Para gepp, además: `GEPP_E2E_PHOTOS_DIR=/Users/ermiry/Documents/Work/gepp-service/tests/stress/images ./run.sh e2e`.
- `python -c "import cerver, cerver.http"` en la imagen base.

### 6.3 Despliegue en test (`test.percepthor.com`, host `artemis`)

- SSH `erick_salas@test.percepthor.com`, `sudo -n` disponible.
- Autorizado: diagnóstico (logs, contadores, tcpdump, py-spy) y desplegar gepp y balance. **Desplegar payloads
  requiere autorización explícita.** nginx, la restauración de test y lo demás los decide el usuario.
- Por cada réplica, una por una y **sin `sudo`**:
  1. `docker compose -f <compose> [-f <override devel>] pull <réplicas>`
  2. `docker compose -f <compose> [-f <override devel>] up -d --no-deps --wait --wait-timeout 600 <réplica>`
- Composes:
  - gepp: `~/percepthor/clients/docker-compose.test.clients.yml`;
  - balance: `~/percepthor/balance/docker-compose.test.balance.yml` con `docker-compose.test.balance.devel.yml`;
  - payloads: su compose de test con un override `devel` nuevo, que solo cambia `image`.
- Confirmar en el log de arranque de cada réplica la configuración de cerver y, en gepp, `STRESS_MODE: True`.

### 6.4 Escenarios por servicio

| Servicio | Escenario | Criterio |
|---|---|---|
| **gepp** | El reproductor (`burst.py`: 8 hilos con `threading.Barrier`, una `requests.Session` por hilo, `POST /api/gepp/refris/<24 hex>/delete`; 404 = atendida, 502 = reset), por nginx y directo a `gepp-1:5060` | 0 resets |
| | `tcpdump -i any` en el namespace de la réplica | SYN = POST (una conexión por petición), 0 RST de gepp |
| | `gepp-service/tests/stress/run.sh peak` (y `headroom`, `breakpoint`, `window` cuando se decida cómo medir la latencia) | 0 errores. p95 del servidor (`server.py` → `peak.performance.json`) ≤ 3 s en refris y ≤ 5 s en censos. Limpieza con `failed 0` |
| **payloads** | Un cliente que fuerce cortes de `recv`: cabeceras y body en escrituras separadas, `send` de pocos bytes, cabeceras de más de 4 KB, `Transfer-Encoding: chunked`, el flujo `/upload/start` → `/chunk` → `/end` con imágenes reales | antes de la Fase 1 reproduce el bug; después de la Fase 1 pasa **con cualquier `CERVER_RECEIVE_BUFFER_SIZE`** (1024 / 4096 / 8192 / 16384) |
| **balance** | Saturar el pool con peticiones de larga duración (más concurrencia que hilos más cola) | antes de la Fase 2: medir los hilos desprendidos (`/proc/<pid>/status` `Threads`) y la latencia. Después: el número de hilos queda acotado y el exceso recibe 503 |
| Todos | `docker stop` de una réplica bajo carga | después de la Fase 2: 0 peticiones cortadas, `Done!` en el log |

### 6.5 Evidencia

- Restar dos tomas de `/proc/net/netstat` y `/proc/net/snmp` (`TCPAbortOnData`, `ListenOverflows`, `OutRsts`).
- `grep -c 'Connection reset by peer'` en el error log de nginx (`entry-nginx-1`).
- `docker stats` y `top -H`.
- py-spy desde el host (el contenedor no tiene `CAP_SYS_PTRACE`):
  - instalación: `docker exec <réplica> pip install py-spy`;
  - ejecución: `sudo -n /proc/<pid>/root/usr/local/bin/py-spy record --pid <pid del host> --duration 60 --rate 100 --nonblocking --gil --format raw`.
- Las capturas y los perfiles se lanzan con `setsid timeout N` y se borran de `/tmp` al terminar.

### 6.6 Restauración

- gepp sin modo estrés: `percepthor-backend/deploy/clients.test.sh gepp`.
- balance sin el override devel.
- payloads a `:test`.

Las decisiones de restauración son del usuario.

### 6.7 Sutilezas operativas conocidas

- La Mac no tiene `timeout`: ponerlo solo del lado remoto.
- La shell remota es zsh: una variable con espacios no se separa en palabras.
- Separar el despliegue de los pasos posteriores y comprobar el estado con `docker inspect` (un `ssh` de despliegue se
  quedó colgado una vez).
- No filtrar contenedores con `grep gepp`: el registry se llama `gepp-300009`.
- Usar siempre `tcpdump -i any`: el orden de las interfaces cambia al redesplegar.
- Para contar conexiones reutilizadas, comparar SYN contra POST, no agrupar por puerto.
- El p95 de Locust desde la Mac incluye la subida por su red (~13 s contra ~1 s en el servidor): juzgar con
  `server.py`, o correr desde una VM cercana.
- `smoke` casi no tiene concurrencia: no prueba nada del manejo de conexiones.

---

## 7. Convenciones

### Las de cerver (se conservan)

- **Estilo:**
  - tabs, espacio antes de los paréntesis de llamada (`foo (bar)`);
  - bloques `#pragma region` / `#pragma endregion`;
  - casts `(void)` en los retornos ignorados;
  - comentarios `//` cortos;
  - el espacio antes de los paréntesis aplica en todos los archivos, vendorizados incluidos; las excepciones son el
    nombre de una macro con parámetros en su `#define NAME(x)` y las tablas X-macro (`XX(...)`, `*_MAP(XX)`), cuyo
    texto se convierte en string con `#`.
- **Constantes y funciones (2026-11-04, código nuevo; el resto del codebase se migra después):**
  - nada de valores literales dentro de las funciones: tamaños, longitudes, prefijos, separadores y timeouts son
    constantes con nombre (`#define` en el header o `static const` al inicio del archivo), por ejemplo
    `HTTP_ERROR_BUFFER_SIZE` en lugar de `char error_buffer[64]`;
  - los cuerpos de los `if` y los `case` quedan cortos: si una rama necesita sus propias variables o más de un par de
    sentencias, pasa a una función `static` dedicada.
- **Headers:**
  - `include/cerver/` es espejo uno a uno de `src/cerver/`;
  - funciones públicas con `CERVER_EXPORT` / `CERVER_PUBLIC` y declaraciones dentro de `extern "C"`.
- **Enums con X-macro `*_MAP (XX)`:** se extiende el mapa, no el enum.
- **Retornos:**
  - `u8` / `int` con 0 = éxito y 1 = error;
  - NULL en los punteros.
- **Nombres:**
  - `x_new` reserva y pone defaults;
  - `x_create` es new más configuración;
  - `x_delete (void *)` tiene la forma de un callback de destrucción;
  - `x_init` / `x_end` arrancan y terminan;
  - `_unsafe` significa sin lock;
  - `_internal` / `_actual` son internas;
  - `_drop` cierra y borra;
  - `_reference` evita copias.
- **Logs y debug:**
  - `cerver_log*`;
  - defines de debug y estadísticas por módulo (`CERVER_DEBUG`, `HANDLER_DEBUG`, `CONNECTION_DEBUG`, `SOCKET_DEBUG`,
    `HTTP_*_DEBUG`, …), que activa `TYPE=development`;
  - los errores reales se loguean siempre.
- **Compilación:**
  - compila como C11 (`-Wpedantic -pedantic-errors`) y como C++11 (`-fpermissive`);
  - los atómicos se escriben con `__atomic_*`.
- **Vendorizados** (`http_parser.c`, `http/json/`, `http/jwt/`): no se reestilizan.

### Las adoptadas del backend (`percepthor-backend/docs/conventions/`)

- **Nombres:**
  - palabras completas, sin abreviaturas, en defines, logs, nombres y paths (`SOCKET_DEBUG`, `error_buffer`). Los
    nombres upstream existentes (`dlist`, `cr`) se dejan;
  - enums nuevos que empiezan en `*_NONE = 0` y terminan en `*_MAX` (el máximo real + 1);
  - singular para un elemento y plural para colecciones.
- **Tests:** un test por cada bug arreglado, y cada suite documenta "los bugs que fijó" y "los huecos de cobertura".
- **Commits y versiones:**
  - commits con verbo en pasado, sin prefijos, uno por cambio ("Added …", "Fixed …", "Refactored …"), con la línea
    correspondiente en `CHANGELOG.md` **solo** en el commit de versión;
  - nunca se sube la versión sin una instrucción explícita.
- **Documentación de sesión:** un documento de contexto por sesión en `percepthor-backend/docs/context/` (goal,
  motivación, decisiones, lo encontrado, lo trabajado, cómo seguir, sutilezas y referencias), y este roadmap como plan
  vivo: cada fase actualiza su estado aquí.
