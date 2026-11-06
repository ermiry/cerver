# cerver — Roadmap

> El plan de trabajo principal para llevar cerver de la versión `2.0b-56` a un framework capaz de competir en las
> grandes ligas.
>
> | | |
> |---|---|
> | **Fecha** | 2026-11-04 |
> | **Rama base** | `http`: idéntica al tag `2.0b-56` más los arreglos sin commitear de §3 |
> | **Contexto** | `percepthor-backend/docs/context/cerver-roadmap-session-context.md` (el handoff de esta sesión) y `percepthor-backend/docs/context/gepp-cerver-connection-reset-session-context.md` (el 502 de gepp) |
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

**2026-11-05: la Fase 3 está implementada en cerver, pycerver, pypercepthor y libauth** sobre la Beta 2.0b-58 (Fase 2
commiteada), sin commitear y sin bump. Falta la migración de gepp, balance y payloads y la validación en test. El
detalle está en `percepthor-backend/docs/context/cerver-phase-3-session-context.md`.

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
- la conexión web sin `ConnectionStats` / `PacketsPerType`, que son las 11 asignaciones restantes por GET.

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
| Archivos de upload con `O_CREAT \| O_WRONLY`, sin `O_TRUNC` ni `O_EXCL` y con modo 0777 | `http/http.c:2093`, `include/cerver/http/http.h:27-28` | `O_EXCL` y 0640 por omisión. Reintentar las escrituras cortas |
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

- La evidencia y las decisiones están en `percepthor-backend/docs/context/cerver-phase-2-session-context.md`.
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

#### 4.3.5 Fase 3b — Capacidades HTTP restantes

- Bodies chunked de entrada (los callbacks de chunk siguen en NULL), y respuestas chunked y en streaming.
- Cabecera `Date`, ETag, `Last-Modified` y 304, compresión opcional. Las cabeceras de esos conditionals ya están en el
  mapa.
- Request id (que se propaga a los logs) y métricas exportables (formato Prometheus) además de las del admin.
- WebSocket: las rutas con ese modifier responden 501 hasta entonces.
- Las cabeceras CORS automáticas en las respuestas (hoy solo el preflight es automático).

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
| 4b | Fase 3b (chunked, ETag, request id, métricas, WebSocket) | lo que completa HTTP/1.1 antes del event loop |
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
