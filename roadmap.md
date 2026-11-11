# cerver — Roadmap de la línea 2.1 (`nightly`)

> El plan de trabajo de la línea experimental de cerver: el event loop con epoll, el keep-alive real y los pools de
> handlers separados del I/O. Después, llhttp, WebSocket, la auth asíncrona, TLS y HTTP/2.
>
> | | |
> |---|---|
> | **Fecha** | 2026-11-08 (F0); 2026-11-11 (F2 adelantada, WebSocket sobre hilos) |
> | **Rama base** | `http` en la 2.0b-61; mergeada hasta la 2.0b-65 |
> | **Ramas** | `nightly` (desarrollo) y `nightly-beta` (las betas 2.1b-N) |
> | **Estado** | F0 publicada (2.1b-7); F2, llhttp, implementada y validada como la 2.1b-8 (§4.2), sin publicar |

**Las citas `archivo:línea`:**

- son relativas a `src/cerver/` salvo que se indique otra cosa;
- corresponden a `http` en la 2.0b-60;
- cada fase mueve las líneas, así que se re-verifican antes de tocar cada zona.

---

## 1. Visión y principios

**`http` / `http-beta` es la línea estable.** Recibe arreglos de bugs y de rendimiento, sin breaking changes.

**`nightly` es un desarrollo aparte** para funcionalidad más radical: cambia el modelo de I/O del core HTTP. Por eso
pasa un gate más estricto (§5), y llega a la línea estable solo cuando se integra a `http` como la 2.1.

Los principios de cerver se conservan: ligero, enfocado al rendimiento y personalizable. Se suman:

1. **El mejor framework antes que la compatibilidad.** `nightly` puede romper la API y la ABI cuando el cambio lleva
   a un mejor diseño; cada ruptura va a `incompatibilities.md` con la beta que la trae, y los bindings se actualizan
   en la misma fase. Lo que sí se conserva:
   - el modelo nuevo es un handler type nuevo (`CERVER_HANDLER_TYPE_EVENTS`), opt-in y solo para `CERVER_TYPE_WEB`;
   - se conservan THREADS, la API vieja (`http_route_*` y la capa de compatibilidad), la firma del handler
     `(const HttpReceive *, const HttpRequest *)` y el protocolo TCP;
   - la API vieja y THREADS para web quedan **obsoletos en 2.1** (warning al registrar) y se quitan en una 2.x
     posterior.
2. **Un hilo solo para trabajo.** Ningún hilo espera por la red: ni por cabeceras, ni por un body lento, ni por una
   conexión ociosa. Los hilos del pool solo corren la auth y los handlers.
3. **Cada salto reutiliza su conexión:** proxy → aplicación y aplicación → aplicación.
4. **La saturación es explícita.** Esperar no cuesta un hilo, y cada espera tiene un timeout y un 503 con
   `Retry-After`.
5. **Probado en los dos modelos.** Cada test de HTTP corre en THREADS y en EVENTS.
6. **La 3.0 es otro cerver**, fuera de este roadmap. Todo HTTP es 2.x.

## 2. Lo que fija la prioridad

- **La saturación con THREADS:** cada conexión en espera necesita un hilo o un lugar en el backlog del kernel. Con
  handlers lentos, el backlog se desborda (`ListenOverflows`) aunque el proceso tenga CPU libre.
- **El healthcheck** espera en la misma cola que los handlers más lentos: bajo saturación, una aplicación sana se ve
  caída.
- **Una conexión TCP nueva por petición en cada salto:** el `keepalive` de un proxy no sirve si cada respuesta dice
  `Connection: close`.
- **Las rutas lentas y las rápidas comparten slots:** un handler de minutos ocupa los mismos hilos que una consulta de
  milisegundos.
- **Un handler que no responde** cerraba la conexión vacía y el proxy daba 502. Se arregló en la 2.0b-61 en `http`
  (500 + close).

## 3. Estado actual: el modelo de I/O de un cerver web (`http`, 2.0b-60)

**Una conexión = un job del thpool, de principio a fin:**

- `cerver_register_new_connection_normal_web` (`handler.c:2581-2615`) encola `cerver_receive_http_work` con
  `thpool_add_work_with_reject`. Con la cola llena: 503 no bloqueante y cierre.
- **Ninguna conexión web entra al `poll`:** POLL y THREADS solo difieren en el accept. POLL acepta en su loop
  (`handler.c:3341-3349`) y THREADS en `cerver_threads` (`:3574`). En los dos, `cerver_accept_wait_space` bloquea el
  accept con la política `wait`.
- **`cerver_receive_http` (`handler.c:2174-2366`):**
  - toma el `HandlerThreadContext` del hilo (arena + buffers de tokens, `handler.c:2374-2462`);
  - pone `SO_RCVTIMEO` de 5 s, ajustado por la deadline de cabeceras (`:1907-1932`);
  - hace `recv` bloqueante (`:1979`) y alimenta `http_receive_handle` (`http/http.c:4571-4594`).
- **El handler corre dentro del parser:** `on_message_complete` (`http/http.c:4427-4449`) despacha. La endpoint corre
  middlewares before, handler, middlewares after y el fin de stream (`:3683-3700`), y después `http_parser_pause`.
- **El cierre llega al terminar el handler, no al responder:** `HPE_PAUSED` → `http_receive_close` (`:4488-4507`):
  `shutdown (SHUT_WR)`, drenado de hasta 64 KiB, `connection_end`. Después, el log y los stats, `connection_drop`,
  `http_receive_delete` y `arena_reset`, una vez por conexión (`handler.c:2281-2353`).
- **`Connection: close` va compilado en cada respuesta:** en `http/response.c`, en el 503 estático de
  `include/cerver/http/response.h` y en las respuestas compartidas de cerver.
- **`HttpReceive.keep_alive` siempre es falso** (`http/http.c:3325`).
- **La escritura es bloqueante, desde el hilo del handler:** `http_response_send_vector` hace un
  `sendmsg (MSG_NOSIGNAL)` en loop. Desde la 2.0b-61, un socket lleno espera con `poll (POLLOUT)` hasta la deadline
  de `send_timeout`.
- **La auth custom también bloquea:** corre en `on_headers_complete` (`http/http.c:4182-4189`), antes del body, en el
  hilo de la conexión.
- **Multipart:** se escribe a disco dentro del parser (`O_EXCL`, `http/http.c:2882-2943`). Desde la 2.0b-61, los
  generadores default de nombres llevan una secuencia por petición, que sigue siendo única con keep-alive.
- **Asignaciones por conexión:** `Connection`, `Socket`, `ConnectionStats` + 2 `PacketsPerType`, `CerverReceive` y
  `file_stats`. Son 8 asignaciones por GET (`test/performance.sh`).
- **Lo que ya sirve para el loop:**
  - el parser ya es incremental y push-based (`test/http/receive.c` lo alimenta partido en cada byte);
  - `sendmsg` ya maneja escrituras parciales (`http_response_parts_advance`);
  - el thpool ya tiene cola acotada, `reject` y `drain` (`threads/thpool.c`).
- **El protocolo TCP no comparte nada de esto:** la web no crea `Client` ni usa el poll, `ReceiveHandle` ni los
  `Handler`. Los puntos de corte son `cerver_register_new_connection_normal` (`handler.c:2783`) y `cerver_start_tcp`
  (`cerver.c:2404`). El enum está en `include/cerver/cerver.h:130-141`.
- **Lo que trajeron las ramas viejas (F0):**
  - de `http-optimizations`, llhttp (`src/cerver/http/parser/`), fuera del build hasta F2;
  - de `ws`, solo la historia: su handshake y `http_web_sockets_send_compile_frame` sirven de referencia para F6a
    (`git show ws:src/cerver/http/websockets.c`). Su parser de frames lee un frame por `recv`, sin límites.

## 4. Fases

**Cada fase es una beta 2.1b-N** de `nightly` → `nightly-beta`, y cierra con:

1. gcc con `TYPE=development|test|beta|production` y `CC=g++` sin warnings nuevos, sobre `gcc:16.2` /
   `debian:trixie-slim`;
2. el gate de `nightly` en verde (§5);
3. los bindings al día cuando la fase cambia la API;
4. la validación con aplicaciones reales fuera del repositorio (§6);
5. el commit dedicado «Updated version to Beta 2.1b-N» con su `CHANGELOG.md`;
6. el estado al día en este roadmap.

El orden de las betas no es el de los números de las fases: F2 (llhttp) se adelantó porque casi solo toca el parser y
`src/cerver/http/`, y WebSocket entra en dos partes, primero sobre el modelo de hilos (F6a) y después sobre el loop
(F6b).

| Fase | Beta | Contenido | Prerrequisito |
|---|---|---|---|
| **F0** | 2.1b-7 | Ramas, merges de `ws` y `http-optimizations`, `.gitattributes`, CI por rama en `gcc:16.2`, este roadmap | 2.0b-61 publicada en `http` |
| **F2** | 2.1b-8 | llhttp 9.4.3: modo estricto, todos los métodos HTTP, lenient por cerver | F0 |
| **F6a** | 2.1b-9 | WebSocket sobre hilos: una conexión en un hilo propio, con tope | F2 |
| **F1** | 2.1b-10 | Preparación sin cambio de comportamiento | F2 |
| **F3** | 2.1b-11 | Event loop, cerrando por petición | F1, F2 + la beta de `http` con el port de la 1.8, ya mergeada en `nightly` |
| **F4** | 2.1b-12 | Keep-alive real | F3 |
| **F5** | 2.1b-13 | Pools nombrados, health / ready, métricas | F4 |
| **F6b** | 2.1b-14 | WebSocket sobre el loop | F4, F6a |
| **F7** | 2.1b-15 | Auth asíncrona | F5 |
| **F8** | 2.1b-16 | TLS en proceso | F4 |
| **F9** | 2.1b-17+ | HTTP/2 | F8 |
| **Merge a `http`** | → 2.1 | Lo terminado, como mínimo F0–F5 (§4.11) | 2.0 en `master` |

### 4.0 F0 — La línea (2.1b-7)

**Antes, en la línea estable (`http`, 2.0b-61):** los tres arreglos que no necesitan el loop, con su test: el 500
cuando el handler no responde, `poll (POLLOUT)` en `EAGAIN` con la deadline de `send_timeout`, y la secuencia por
petición en los nombres default de uploads.

**F0 en `nightly`:**

1. **La rama:** `nightly` nace de `http` en la 2.0b-61.
2. **El merge de `ws`** (la línea 2.1b-1…6 de 2020), con `-s ours`: registra su historia sin traer contenido. Su
   código no compila con la API actual y su contenido entraría en silencio (el `HttpCerver` cambiaría de ABI). Se
   mergea la punta de `ws-beta` (`707e4133`, 2.1b-6), que contiene a `ws` y a los merges de las betas 2.1b-5 y 2.1b-6.
3. **llhttp:**
   - un commit siembra los seis archivos de `http-optimizations` (`src/cerver/http/parser/{llhttp,api,http}.c` y
     `include/cerver/http/parser/{llhttp,api,helpers}.h`);
   - el `makefile` los deja fuera de `SOURCES` (`LLHTTPDIR`) hasta F2;
   - después, el merge de `http-optimizations` con `-s ours`. Así no entran el borrado de `http_parser.h`, el
     `-Wpedantic` comentado ni el test y el benchmark del parser nuevo.
4. **`.gitattributes` con `merge=ours`,** solo en `nightly`, para `version.txt`, `include/cerver/version.h`,
   `Dockerfile`, `cerver-install.sh`, `CHANGELOG.md` y `roadmap.md`. Necesita `git config merge.ours.driver true` en
   cada clon (`CLAUDE.md`).
5. **El CI:** los jobs que compilan en el runner corren en `container: gcc:16.2`; los sanitizers corren en un
   `docker run` de esa imagen (TSan necesita el `sysctl` del host). La integración y el fuzz ya corren en
   `Dockerfile.test` y `Dockerfile.fuzz`, sobre `gcc:16.2` desde la 2.0b-61.
6. **Los workflows por rama** (§7): `scheduled.yml`, los nombres y las imágenes de cada beta, la imagen `nightly`.
7. **Este roadmap** reemplaza al de `http` en `nightly`.
8. **«Updated version to Beta 2.1b-7»,** que continúa los tags `2.1b-1…6` de `ws`.
9. **`nightly-beta`** nace de la 2.0b-61 de `http-beta`, y el PR `nightly` → `nightly-beta` crea `2.1b-7`
   («Nightly Beta 2.1b-7»).
10. **Sin tags `archive/*`:** `2.1b-6` marca la punta de `ws`, y `ws`, `ws-beta` y `http-optimizations` quedan como
    ancestros de `nightly`. Esas ramas ya no reciben trabajo.

### 4.1 F1 — Preparación (2.1b-10)

No cambia el comportamiento en THREADS. Separa lo que hoy es «por conexión» de lo que es «por petición».

- **`Connection: close` fuera de las cabeceras compiladas** de `response.c`. Se agrega en la parte por petición de
  `http_response_send_message` cuando se va a cerrar, y en THREADS siempre. El 503 estático lo conserva.
  `test/http/responses.c` se actualiza (el orden de la cabecera cambia).
- **`HttpReceive.framed`:** si la respuesta va delimitada (Content-Length o chunked completo). Una respuesta cruda sin
  `\r\n\r\n` (`send_raw`) o un stream de HTTP/1.0 no lo está.
- **`http_receive_reset`,** que reinicia todo lo de una petición (lista en §4.4).
- **`HttpReceive` independiente de `HandlerThreadContext`:** la arena y los buffers de tokens se inyectan.
  `http_receive_create_arena` los recibe en lugar de tomarlos del hilo.
- **El log y los stats por petición,** fuera de `cerver_receive_http`: `start_time` y las deadlines van por petición.
- **La conexión web sin `ConnectionStats` / `PacketsPerType`:** menos asignaciones por conexión.

### 4.2 F2 — llhttp (2.1b-8)

**El goal:** cambiar http_parser 2.x (Joyent, sin mantenimiento) por **llhttp 9.4.3**, el parser de Node.js, en modo
estricto, con todos los métodos HTTP que reconoce. La pausa por petición y la posición de los bytes sin parsear son lo
que usan F3 (auth y dispatch desde el loop) y F4 (pipelining). Se adelantó a F1 porque casi solo toca el parser y
`src/cerver/http/`; corre sobre el handler de hoy (thpool).

**Las decisiones:**

| Tema | Decisión |
|---|---|
| Versión | llhttp **v9.4.3** (`release/v9.4.3`, 2026-07-30), la última estable al implementar. Reemplaza los sembrados de 6.0.6 de F0 |
| Integración | Un split de cerver en `include/cerver/http/parser/{llhttp,helpers,api}.h` y `src/cerver/http/parser/{llhttp,api,http}.c`, con el estilo de cerver. `llhttp_t` va **embebido** en `HttpReceive` (sin `malloc` / `arena_alloc` del parser) |
| Licencia | llhttp es MIT: cada archivo conserva el aviso de copyright y de permiso, y `THIRD-PARTY.md` lista el código de terceros |
| Restyle | `llhttp.c` es generado por llparse (~10 mil líneas): lo reformatea un script verificado fuera del repo (idempotente y el mismo `objdump -d` del `.o` sin `-g` antes y después). Los headers, los renombres y el diseño se hacen a mano; las tablas `XX(...)` no se tocan |
| Choques de nombres | El `llhttp.h` de upstream define `HTTP_METHOD_MAP`, `HTTP_STATUS_MAP` y `HTTP_STATUS_*`, que chocan con cerver. En el split, `method.h` es la única fuente de los métodos; `HTTP_STATUS_MAP` / `llhttp_status` salen (cerver solo parsea peticiones); los mapas que el generado necesita llevan el prefijo `LLHTTP_` |
| Métodos | **Una sola tabla:** `method.h` guarda `REQUEST_METHOD_MAP` con los números de llhttp, los 34 métodos HTTP (0–33) y `QUERY` (46), todos registrables. Se quitan `enum http_method`, `HTTP_METHOD_*`, `HTTP_METHOD_NONE` y `http_method_str ()`: queda `http_request_method_str ()`. `REQUEST_METHOD_SIZE` = 47, `REQUEST_METHOD_UNDEFINED` = 255 (34 es `PRI` en llhttp) |
| Respuestas por método | RTSP y `PRI` (el preface de HTTP/2) los rechaza el parser: 400. Un método que el parser acepta y cerver no mapea: 501 (RFC 9110 §9.1). Uno válido que la ruta no registra: 405 |
| Tokens | Los callbacks `*_complete` de llhttp 9 (`on_url_complete`, `on_header_field_complete`, `on_header_value_complete`) cierran cada pieza; se quita la máquina `HTTP_RECEIVE_TOKEN_*` |
| Estricto | Default estricto: LF sin CR, espacio antes de `:`, `Transfer-Encoding` con `Content-Length`, chunk extensions inválidas y versiones inválidas → 400 |
| Lenient | `HttpParserLenient`, un flag por cada lenient de llhttp 9.4.3 salvo `HEADER_VALUE_RELAXED` (abajo), y `http_cerver_set_parser_lenient ()` por cerver, default 0, con un warning por cada flag que se activa |
| Límites | `max_header_size` y `max_url_size` **siempre activos:** 0 vuelve al default (ya no desactiva el límite). Con http_parser, el 0 seguía topado por sus 80 KiB internos; llhttp no tiene ese tope |
| Etapas | Las de la 2.0b-65 no cambian: `REQUEST_LINE` con el primer byte, `HEADERS` con el primer byte de un field, `BODY` con `on_headers_complete`, `HANDLER` con `on_message_complete`. `test/http/disconnect.c` es el oráculo |
| WebSocket | Fuera de F2: una ruta WebSocket y un `Upgrade` siguen respondiendo 501 hasta F6a |

**Lo que cambia en `http.c`:**

- `on_url`, `on_header_field` y `on_header_value` solo acumulan y cuentan los límites 414 y 431; la url se cierra en
  `on_url_complete` y cada cabecera en `on_header_value_complete`. Un rechazo devuelve `HPE_USER`.
- `on_headers_complete`: el método, el content length con `F_CONTENT_LENGTH` (en llhttp vale 0 sin la cabecera, ya no
  `(uint64_t) -1`; también en la reserva del body), `F_CHUNKED`, el rechazo temprano (413), la auth antes del body y
  `Expect: 100-continue`.
- `on_message_complete` **devuelve `HPE_PAUSED`** (llhttp no permite llamar a `llhttp_pause` desde un callback). Un
  CONNECT o un `Upgrade` llegan aquí antes que `HPE_PAUSED_UPGRADE`, que también es una pausa.
- `llhttp_execute` devuelve el errno: los bytes parseados son `llhttp_get_error_pos ()` menos el buffer. En la pausa se
  guardan cuántos bytes del último buffer quedaron sin parsear (`HttpReceive.unparsed`, una petición en pipeline) y
  `llhttp_should_keep_alive ()` (`HttpReceive.keep_alive_requested`), sin cambiar el cierre: los usa F4.
- Un método que llhttp acepta y cerver no conoce responde 501 desde `on_headers_complete`. Con la tabla única no
  queda ninguno (llhttp en modo petición solo acepta los 35 de la tabla); es una defensa para una versión futura.
- El log de un error usa `llhttp_errno_name ()` y `llhttp_get_error_reason ()`.

**Las diferencias con http_parser,** caso por caso. Se midieron con los dos parsers sobre las mismas peticiones
(36 casos; los iguales no se listan: header sin espacio después de `:`, nombre de header inválido, NUL o un carácter
de control en un valor, `Content-Length` repetido o con signo, versiones 0.9 y 2.0, chunk extension válida, trailers,
dos espacios en la línea, espacio en la url, método en minúsculas, PROPFIND, CONNECT, `Upgrade`, datos después de
`Connection: close`, HTTP/1.0). `test/http/receive.c` prueba cada fila con la respuesta de cerver:

| Caso | http_parser 2.x | llhttp 9.4.3 estricto | Con lenient |
|---|---|---|---|
| Líneas que terminan con LF sin CR | Aceptada | 400 (`HPE_INVALID_VERSION`, «Expected CRLF after version») | `OPTIONAL_CR_BEFORE_LF` **y** `OPTIONAL_LF_AFTER_CR` (uno solo no basta) |
| CR sin LF | 400 (`HPE_LF_EXPECTED`) | 400 (`HPE_STRICT`) | 400 |
| Espacio antes de `:` | 400 | 400 | 400 con cualquier flag |
| Obsolete line folding (un valor que sigue en la línea siguiente) | Aceptada | 400 (`HPE_INVALID_HEADER_TOKEN`) | `HEADERS` |
| `Transfer-Encoding` con `Content-Length` | 400 (`HPE_UNEXPECTED_CONTENT_LENGTH`) | 400 (`HPE_INVALID_CONTENT_LENGTH`) | `CHUNKED_LENGTH` |
| `Transfer-Encoding` sin chunked al final (`gzip`, `chunked, gzip`) | 400 | 400 (`HPE_INVALID_TRANSFER_ENCODING`) | `TRANSFER_ENCODING`: el body se lee hasta el EOF, así que la petición nunca se completa |
| Versión HTTP/1.5 | Aceptada | 400 (`HPE_INVALID_VERSION`) | `VERSION` |
| Espacios después del tamaño de un chunk | Aceptada | 400 (`HPE_INVALID_CHUNK_SIZE`) | `SPACES_AFTER_CHUNK_SIZE` |
| Un chunk sin CRLF después de sus datos | 400 (`HPE_STRICT`) | 400 (`HPE_STRICT`) | `OPTIONAL_CRLF_AFTER_CHUNK` |
| Chunk extension con comillas sin cerrar | Aceptada | 400 (`HPE_STRICT`) | 400 con cualquier flag |
| Un carácter de control en un valor | 400 | 400 | `HEADERS` |
| `QUERY` | 400 (`HPE_INVALID_METHOD`) | Aceptada | — |
| Un método RTSP (`DESCRIBE`) | 400 (`HPE_INVALID_METHOD`) | 400 (`HPE_INVALID_CONSTANT`, «Invalid method for HTTP/x.x request») | 400 |
| El preface de HTTP/2 (`PRI * HTTP/2.0`) | 400 (`HPE_INVALID_METHOD`) | `HPE_PAUSED_H2_UPGRADE` → 400 de cerver | 400 |
| Cabeceras de más de 80 KiB | 400 (`HPE_HEADER_OVERFLOW`) | Aceptada por el parser → 431 por el límite de cerver (64 KiB, siempre activo) | — |

**El lenient que cerver no expone:** el flag `HEADER_VALUE_RELAXED` (0x400) de llhttp 9.4.3 deja a `llhttp_execute ()`
en un bucle infinito con un NUL en el valor de un header (reproducido con los archivos de upstream sin cambios, en
arm64 y amd64, con `-O2` y `-O0`). No está en `HttpParserLenient` ni en la API del split, y
`http_cerver_set_parser_lenient ()` lo descarta. Se revisa en cada versión nueva de llhttp; el fuzzer `lenient`
varía los flags.

**El gate** (además del de §5):

- los cortes en cada byte de `test/http/receive.c` y la matriz completa de `test/http/disconnect.c` dan los mismos
  resultados;
- el fuzz pasa con su corpus actual más las semillas nuevas;
- `benchmarks/http-parser.c` queda igual o mejor que http_parser en la misma máquina: http_parser ≈ 1050 MB/s, llhttp
  2233–2497 MB/s (arm64, gcc 16.2, `TYPE=production`);
- `nm -D bin/libcerver.so` sin `http_parser_*` ni `http_method_str`.

### 4.3 F3 — El event loop (2.1b-11)

**El prerrequisito.** `http` tiene que recibir el port de la 1.8 completa (hasta la 1.8b-9: protocolo portable,
serializador por esquema, game, el deadline total de envío), que cambia `cerver.c`, `handler.c`, `client.c` y
`connection.c`. Llega con la 2.0b-66; F3 empieza después de mergear esa beta a `nightly`, para resolver esos
conflictos una sola vez.

**La arquitectura:**

```
             SO_REUSEPORT (1 listen fd por loop)
                       │
   ┌───────────────────▼────────────────────┐        ┌─────────────── pools (F5: nombrados) ─────────┐
   │ loop de I/O (1..N hilos, epoll LT)     │  job   │ "default" (CERVER_POOL_THREADS)               │
   │  accept4 · leer · parsear · timers     ├───────►│  etapa auth · handler · estáticos             │
   │  respuestas cortas de cerver           │        │  el handler escribe al fd (poll en EAGAIN)    │
   │  health / ready (F5) · WebSocket (F6b) │◄───────┤                                               │
   └────────────────────────────────────────┘ done   └───────────────────────────────────────────────┘
          (cola MPSC + eventfd por loop)
```

**Estados de la conexión:**

```
READ_HEADERS ─(auth custom)─► AUTH [pool, sin EPOLLIN] ─ok─► READ_BODY ─► (PARKED) ─► DISPATCHED [pool dueño]
   ▲  └─(400/404/405/408/413/414/431)──────┴─rechazo─► WRITE_LOOP ─┐                    │ el handler regresa
   └──── IDLE (idle timer; pending = pipelining) ◄─────────────────┴── keep-alive ? IDLE : CLOSING ◄┘
```

- **El handler type `EVENTS`:**
  - es una entrada nueva en `CERVER_HANDLER_TYPE_MAP` (`include/cerver/cerver.h:130-141`), solo para
    `CERVER_TYPE_WEB`; con otro tipo de cerver, `cerver_start` falla;
  - `cerver_start_tcp` (`cerver.c:2404`) arranca N loops, `CERVER_IO_THREADS=1` por default.
- **Los loops:**
  - cada uno tiene su `epoll` (level-triggered), su listen fd con `SO_REUSEPORT` y su `eventfd` de despertar;
  - aceptan con `accept4 (SOCK_NONBLOCK | SOCK_CLOEXEC)`, reutilizando el fd de reserva y el backoff de
    `EMFILE` / `ENFILE` y los errores que se ignoran hoy en `handler.c`.
- **El hilo principal** no corre un loop. Espera con `poll` sobre un `eventfd` de stop, con las señales desbloqueadas,
  así que `EINTR` = `cerver_stop_request`, como hoy en `accept`. Así `cerver_start` regresa y el signal handler de la
  aplicación corre sin cambios.
- **La lectura:**
  - `recv` no bloqueante sobre un buffer por loop (2^16);
  - los tokens se acumulan en los buffers de la conexión;
  - lo que sobra tras una pausa va a `pending`.
- **El traspaso al pool:**
  1. el loop deja de vigilar el fd (`EPOLLONESHOT` sin re-armar);
  2. la conexión pasa al pool por la cola del thpool, cuyo mutex hace de barrera de memoria;
  3. el hilo del pool, al terminar, la empuja a la cola de completados del loop dueño y escribe su `eventfd`;
  4. el loop decide (en F3, siempre cierre) y re-arma con `EPOLL_CTL_MOD`.

  **Nunca dos hilos tocan la misma conexión.**
- **La escritura (decisión: el handler escribe).** Mientras el handler corre, la conexión es suya: escribe directo al
  fd no bloqueante, con `poll (POLLOUT)` en `EAGAIN` hasta `send_timeout`. Por eso:
  - «regresa 0 = enviado» sigue siendo cierto;
  - `sendfile`, las respuestas compiladas compartidas, las cabeceras en la pila y `http_response_delete` justo después
    de enviar siguen igual.
- **Lo que escribe el loop:** solo las respuestas cortas de cerver (400, 404, 405, 408, 413, 414, 431, 503,
  100-continue, OPTIONS automático, 308). Son precompiladas, con buffer de salida y `EPOLLOUT` si hay `EAGAIN`. Los
  estáticos (`sendfile`), admin y métricas van al pool.
- **La auth custom en el pool (decisión).** Al completar las cabeceras de una ruta con auth custom:
  1. llhttp se pausa y el loop quita `EPOLLIN`;
  2. un job de auth corre en el pool;
  3. si rechaza, la respuesta sale antes del body y la conexión se cierra (se conserva el rechazo temprano de la
     Fase 1); si acepta, el loop retoma la lectura del body y despacha el handler.

  La auth Bearer JWT es CPU y se queda en el loop. Con `authentication_with_body`, la auth corre como primera etapa del
  job del handler.
- **El backpressure (las políticas de hoy, sin ocupar hilos):**
  - `reject`: con la cola del pool llena, 503 desde el loop;
  - `wait`: la conexión **se estaciona en el loop** (FIFO acotada, estado `PARKED`) hasta que un completado libere
    lugar o venza `wait_timeout`, que responde 503 + `Retry-After`;
  - el accept nunca se bloquea; el límite global es `CERVER_MAX_CONNECTIONS` (2^14).
- **Los timers:** una rueda por loop con ticks de 2^8 ms y una deadline activa por conexión: cabeceras (16 s, 408),
  request total (opcional), idle (F4), escritura del loop y espera estacionada.
- **El apagado:**
  1. stop;
  2. los loops cierran sus listen fds;
  3. las conexiones que leen terminan su petición (con su deadline);
  4. las DISPATCHED responden;
  5. `thpool_drain` con la política y el timeout de hoy;
  6. join de los loops y teardown.
- **Multipart en el loop (decisión).** La escritura a disco se queda en el loop: un proxy delante bufferea los bodies
  y las escrituras van a page cache o tmpfs. Se mide el tiempo de escritura en las métricas; si un percentil supera el
  umbral, pasa al pool en una fase posterior.

**El código central (borrador):**

```c
// include/cerver/events/loop.h
typedef enum HttpConnectionState {
	HTTP_CONNECTION_STATE_READ_HEADERS = 0, HTTP_CONNECTION_STATE_AUTH, HTTP_CONNECTION_STATE_READ_BODY,
	HTTP_CONNECTION_STATE_PARKED, HTTP_CONNECTION_STATE_DISPATCHED, HTTP_CONNECTION_STATE_WRITE_LOOP,
	HTTP_CONNECTION_STATE_IDLE, HTTP_CONNECTION_STATE_CLOSING
} HttpConnectionState;

typedef struct HttpConnection {
	int fd; struct EventLoop *loop; HttpConnectionState state;
	char ip[46]; struct sockaddr_storage address;
	HttpReceive *receive;                 // reutilizado entre peticiones (http_receive_reset)
	struct Arena *arena;                  // por conexión, de un pool del loop; arena_reset por petición
	String *url_buffer, *field_buffer, *value_buffer;
	char *pending; size_t pending_len;    // bytes sin parsear tras la pausa (auth / pipelining)
	char *out; size_t out_len, out_sent;  // respuestas del loop (y frames de WebSocket en F6b)
	unsigned int n_requests; struct HttpPool *pool;
	struct TimerEntry timer; struct HttpConnection *next;   // lista intrusiva (estacionadas / completadas)
} HttpConnection;

typedef struct EventLoop {
	int epoll_fd, wake_fd, listen_fd; pthread_t thread; struct Cerver *cerver;
	char *read_buffer; size_t read_buffer_size;   // uno por loop, no por conexión
	pthread_mutex_t done_mutex; HttpConnection *done_head, *done_tail;
	struct TimerWheel *timers; struct ArenaPool *arenas;
	unsigned int n_connections;
} EventLoop;
```

```c
static void http_connection_readable (EventLoop *loop, HttpConnection *connection) {

	ssize_t rc = 0;
	while ((rc = recv (connection->fd, loop->read_buffer, loop->read_buffer_size, 0)) > 0) {
		http_receive_handle (connection->receive, rc, loop->read_buffer);
		if ((connection->state != HTTP_CONNECTION_STATE_READ_HEADERS)
			&& (connection->state != HTTP_CONNECTION_STATE_READ_BODY)) break;    // pausado: auth, dispatch o error
	}

	if (rc == 0) http_connection_close (loop, connection, false);
	else if ((rc < 0) && (errno != EAGAIN) && (errno != EINTR)) http_connection_close (loop, connection, true);

}
```

**Los archivos:**

- Nuevos: `src/cerver/events/{loop,timers,dispatch}.c`, `src/cerver/http/connection.c` y sus headers espejo.
- Cambian:
  - `cerver.h`, y `cerver.c` (start, stop, teardown);
  - `http/http.c` (las pausas, la entrada, la auth y el dispatch);
  - `http/response.c`, `http/stream.c`, `handler.c`.

**La API:**

```c
CERVER_EXPORT void cerver_set_io_threads (Cerver *cerver, unsigned int n_threads);            // 1
CERVER_EXPORT void cerver_set_max_connections (Cerver *cerver, unsigned int max);              // 2^14
```

### 4.4 F4 — Keep-alive real (2.1b-12)

**`http_receive_reset`, entre peticiones de una conexión:**

- reinicia `receive_status`, `type`, `dispatch`, `node`, `endpoint`, `route`, `served_file`, `head`,
  `max_body_size`, `status`, `sent`, `is_multi_part`, `header_size`, `n_headers`, `body_size`, `n_parts`,
  `responded`, `framed` y `stream`;
- `settings.on_body` vuelve a `http_receive_handle_body`;
- libera `mpart_parser` y `file_stats`;
- destruye el `HttpRequest`: los archivos, y los deleters de `decoded_data` / `custom_data`;
- crea uno nuevo en la arena reiniciada.

**La decisión:**

```c
static bool http_receive_keep_alive (const HttpConnection *connection, const HttpReceive *receive) {

	return (receive->http_cerver->keep_alive)                                    // default: true en EVENTS
		&& (receive->keep_alive_requested)                                        // llhttp: 1.1 sin close, 1.0 con keep-alive
		&& (connection->n_requests < receive->http_cerver->keep_alive_requests)  // 2^10
		&& (receive->responded) && (receive->framed)                             // Content-Length o chunked completo
		&& (receive->receive_status == HTTP_RECEIVE_STATUS_COMPLETED)            // body completo (un rechazo cierra)
		&& (!cerver_is_stopping (receive->http_cerver->cerver));

}
```

- **La cabecera:** lo que se conoce antes de enviar la decide. Va `Connection: close` cuando se cierra; nada en 1.1 al
  mantener; `Connection: keep-alive` en 1.0.
- **Una respuesta sin delimitar** fuerza el cierre.
- **Un stop durante el handler** cierra después de la respuesta completa.
- **Pipelining:** las peticiones se responden en orden desde `pending`. Hoy se descarta todo después de la primera
  (`test/http/receive.c` lo prueba con «pipelined requests», que pasa a esperar las dos respuestas).
- **La regla anti-502 detrás de un proxy:**
  - el idle de cerver (`CERVER_KEEP_ALIVE_TIMEOUT`, default 2^7 = 128 s) es **mayor** que el `keepalive_timeout` del
    upstream del proxy (60 s en nginx);
  - el `keepalive_requests` del proxy (1000 en nginx) es **menor** que el de cerver (2^10);
  - así el proxy siempre cierra primero una conexión ociosa;
  - si cerver llega a su máximo, lo anuncia con `Connection: close` en esa respuesta.
- **El drenado:** FIN a las conexiones ociosas. El proxy las saca de su caché al ver el EOF.
- **nginx en los tests:** `test/nginx/` pasa a `proxy_http_version 1.1` + `proxy_set_header Connection ""`. Hoy su
  `keepalive 32` solo funciona porque cada respuesta dice close.

**La API:**

```c
CERVER_EXPORT void http_cerver_set_keep_alive (HttpCerver *http_cerver, bool enable);          // true en EVENTS
CERVER_EXPORT void http_cerver_set_keep_alive_timeout (HttpCerver *http_cerver, unsigned int seconds);   // 2^7
CERVER_EXPORT void http_cerver_set_keep_alive_requests (HttpCerver *http_cerver, unsigned int max);      // 2^10
```

### 4.5 F5 — Pools nombrados, health y métricas (2.1b-13)

**Los pools nombrados (decisión).**

- `HttpPool` es un wrapper sobre el `Thpool` actual (ring buffer, `reject`, `drain`). El default es el thpool de hoy
  (`CERVER_POOL_THREADS`).
- Un grupo, un endpoint o una ruta compat elige su pool. Cada pool tiene su cola, su política y su `wait_timeout`.
- Un pool desconocido es un error de registro (`http_tree_error`), como un CIDR inválido.
- En THREADS, pools y health se ignoran con un warning.
- Uso previsto: aislar los handlers de minutos en su propio pool, para que las rutas rápidas y el healthcheck no
  compitan con ellos.

**La ruta de salud nativa (decisión).**

- `http_cerver_set_health_route` responde 200 precompilado **desde el loop**, sin pool: mide «el proceso vive».
- `http_cerver_set_ready_route` responde 503 si algún pool está lleno o estacionando: mide la saturación.

**Las métricas** (en `cerver/metrics`):

- conexiones abiertas e IDLE;
- reusos de keep-alive;
- peticiones por conexión;
- cola, estacionadas y espera (histograma) por pool;
- tiempo de escritura de uploads;
- respuestas que faltaron.

**La API:**

```c
CERVER_EXPORT HttpPool *http_cerver_pool_create (HttpCerver *http_cerver, const char *name, unsigned int n_threads);
CERVER_EXPORT void http_pool_set_queue_size (HttpPool *pool, unsigned int size);
CERVER_EXPORT void http_pool_set_overload_policy (HttpPool *pool, CerverOverloadPolicy policy);
CERVER_EXPORT void http_pool_set_wait_timeout (HttpPool *pool, unsigned int seconds);          // 2^5
CERVER_EXPORT void http_group_set_pool (HttpGroup *group, const char *name);                   // heredable
CERVER_EXPORT void http_endpoint_set_pool (HttpEndpoint *endpoint, const char *name);
CERVER_EXPORT void http_route_set_pool (HttpRoute *route, const char *name);                   // compat
CERVER_EXPORT void http_cerver_set_health_route (HttpCerver *http_cerver, const char *path);
CERVER_EXPORT void http_cerver_set_ready_route (HttpCerver *http_cerver, const char *path);
```

### 4.6 F6 — WebSocket

En `nightly` no hay código de WebSocket: el merge de `ws` en F0 fue `-s ours`, así que `websockets.c` solo vive en la
historia (`git show ws:src/cerver/http/websockets.c`). Hasta F6a, una ruta con el modifier
`HTTP_ROUTE_MODIFIER_WEB_SOCKET` y una petición con `Upgrade` responden 501. WebSocket entra en dos partes.

**De `ws` se reusan como referencia:**

- el handshake (`Sec-WebSocket-Key` + GUID, SHA-1 con la API EVP de OpenSSL 3, base64, 101);
- el encoder de frames (`http_web_sockets_send_compile_frame`).

**Se reescriben:**

- el parser de frames: incremental entre lecturas, con la máscara obligatoria del cliente, `max_frame` /
  `max_message` (cierre 1009), fragmentación y UTF-8 en los de texto;
- ping/pong automáticos con timer;
- close con código y su timeout.

#### F6a — WebSocket sobre hilos (2.1b-9)

Corre con el handler de hoy (thpool), sin el loop.

- **El hilo:** el hilo del thpool hace el handshake (después de que llhttp pausa con el `Upgrade`) y entrega la
  conexión a un hilo propio, registrado en el `ThreadGroup` del cerver y unido en el teardown. El thpool queda libre
  para HTTP.
- **El tope:** un máximo de conexiones WebSocket por cerver; arriba del tope el handshake responde 503.
- **La API:** sobre los endpoints y los grupos (`http_endpoint_set_web_socket ()`, su equivalente en los grupos), con
  los callbacks `on_open`, `on_message`, `on_close` y `on_error` y su custom data. Un `HttpWebSocket` con
  `http_web_socket_send_text ()`, `_send_binary ()` y `_close ()`, que se pueden llamar desde otros hilos. Las
  `HttpRoute` con el modifier se traducen en `compatibility.c`.
- **El alcance** (RFC 6455 completo, permessage-deflate, autobahn-testsuite) se decide al planear la fase.

#### F6b — WebSocket sobre el loop (2.1b-14)

**La conexión vive en el loop** (estado `WEBSOCKET`). Los callbacks `on_open`, `on_message` y `on_close` corren en el
pool de la ruta.

**Aquí sí escribe el loop.** `http_websocket_send` se llama desde cualquier hilo: encola en una cola de salida acotada
de la conexión y despierta al loop, que escribe con `EPOLLOUT`.

- Con la cola llena: descartar, o cerrar con 1008, según la configuración.
- Un registro de clientes por ruta permite el broadcast.

En EVENTS la conexión pasa al loop; en THREADS sigue en su hilo propio (F6a). Detrás de un proxy van `Upgrade` /
`Connection "upgrade"` y un `proxy_read_timeout` mayor que el ping.

### 4.7 F7 — Auth asíncrona (2.1b-15)

- **Una auth custom que consulta otro servicio por HTTP** deja de ocupar un hilo: un cliente `curl_multi` integrado al
  loop, con `CURLMOPT_SOCKETFUNCTION` / `CURLMOPT_TIMERFUNCTION` sobre el `epoll` y la rueda de timers.
- **La conexión espera en `AUTH` sin hilo,** con un pool de conexiones persistentes por loop.
- **El fallback:** la etapa en el pool, para una auth custom que no usa ese cliente.

### 4.8 F8 — TLS en proceso (2.1b-16)

- OpenSSL no bloqueante en la máquina de estados (`SSL_read` / `SSL_write`, `WANT_READ` / `WANT_WRITE` mapeados a
  epoll), SNI y recarga de certificados. OpenSSL ya se enlaza, hoy solo para JWT.
- Lleva su propia fase de diseño antes de implementar.

### 4.9 F9 — HTTP/2 (2.1b-17+)

- Primero h2c (prior knowledge) detrás de un proxy, después ALPN con TLS.
- Un stream = una petición al pool. El control de flujo vive en el loop.
- Lleva su propia fase de diseño antes de implementar.

### 4.10 Deprecación (2.1)

- `http_route_*`, la capa de compatibilidad y THREADS para web registran un warning de obsoleto.
- Se quitan en una 2.x posterior, cuando las aplicaciones registren con grupos y corran en EVENTS.
- Una firma nueva del handler (un contexto único) sería aditiva: otro tipo de handler, sin romper el actual.

### 4.11 La integración: `nightly` → `http` → 2.1

**Los criterios:**

1. 2.0 publicada en `master`;
2. F0–F5 publicadas como betas y en verde con el gate de `nightly`;
3. aplicaciones reales con EVENTS el tiempo acordado, sin regresiones contra THREADS;
4. el estrés comparado contra el de THREADS.

**El merge:**

1. `nightly` → `http`, con los archivos de versión y el roadmap resueltos a mano: este roadmap se integra al de `http`
   como la línea 2.1;
2. el `.gitattributes` vuelve a su forma de `http` en ese commit;
3. `http-beta` continúa la numeración (2.1b-N+1), sin release candidates: cada línea publica solo betas;
4. el PR `http` → `master` publica la 2.1 (un release nunca sale de la beta).

Las fases que sigan en `nightly` forman la 2.2 (`nightly-beta` → 2.2b-N).

## 5. Plan de tests y el gate de `nightly`

**El gate de `nightly` es el de `http` más:**

- **El de `http`:** gcc con los 4 TYPEs, g++, unit con ASan + UBSan, TSan y valgrind, los pares de integración × 4
  variantes, el fuzz y `test/performance.sh`.
- **El flaky hunter:** 200 repeticiones (`test/repeat.sh`) de las suites que tocan el loop, también en TSan.
- **Fuzz:** ≥ 30 min por target (el de `http` es de 30 s), con un target nuevo de varias peticiones por conexión.
- **Los pares web** en THREADS **y** en EVENTS (`test/web/*.c` toman el modelo de una variable de entorno). curl
  reutiliza la conexión cuando no hay close.
- **`test/http/server.c`:** sus tests (408, slowloris, idle, slow body, request y send timeout, overload
  `reject` / `wait`, drain, EINTR, sin fds, dos cervers…) parametrizados en los dos modelos. Son la suite de
  aceptación del loop.
- **`test/events/{loop,timers}.c`:** la rueda, el traspaso y la cola de completados.
- **`test/http/keepalive.c` (nuevo):**
  - N peticiones por conexión;
  - pipelining en orden;
  - 1.0 con keep-alive;
  - close del cliente;
  - el máximo de peticiones;
  - idle con FIN;
  - respuesta sin delimitar → close;
  - sin respuesta → 500;
  - el estado limpio entre peticiones (params, custom data, directorios de multipart distintos);
  - auth que rechaza antes del body;
  - `wait` estacionando sin ocupar hilos;
  - `reject` 503;
  - `wait_timeout`;
  - aislamiento de pools (uno lento lleno, el default responde);
  - el health con todo saturado;
  - el drenado con conexiones ociosas y en vuelo.
- **`test/http/receive.c`** alimentado por el loop, partido en cada byte.
- **`test/nginx/`** con keep-alive real y reinicios del servidor bajo carga: 0 resets, 0 502 y el reuso medido con
  `ss`.
- **`benchmarks/load.c`** con modo keep-alive y umbrales nuevos en `performance.sh`. La meta es GET ≤ 4 asignaciones
  (hoy 8), y keep-alive contra close por petición.
- **F2:** las diferencias de llhttp documentadas caso por caso (§4.2), y la matriz completa de `test/http/disconnect.c`
  con los mismos contadores por etapa.
- **F6a y F6b:** autobahn-testsuite (fuzzingclient), frames partidos en cada byte, envío desde otros hilos con TSan, detrás
  de nginx.
- **Un soak de 4 h** con una aplicación real, sin crecer memoria ni fds (§6).

`scheduled.yml` corre las versiones largas (TSan, el flaky hunter, el fuzz de 30 min por target, performance y
nginx); hasta que `master` tenga su despachador (§7), se lanza a mano.

## 6. Validación con aplicaciones reales

Cada fase se valida, además del gate, con aplicaciones reales en un entorno de pruebas, fuera de este repositorio:
THREADS contra EVENTS sobre la misma imagen. La evidencia (estrés, `ListenOverflows`, `TCPAbortOnData`, `TIME_WAIT`,
el healthcheck durante la saturación, el drenado al detener) se guarda fuera del repositorio. `nightly` no se usa en
producción.

## 7. Los workflows

- **`pre-release.yml` y `beta.yml`** corren en cualquier `'*beta'`. El nombre y la imagen salen de la rama:
  - siempre `ermiry/cerver:<version.txt>`;
  - el tag móvil por línea: `:beta` desde `http-beta`, `:nightly-beta` desde `nightly-beta`, `:core-beta` desde
    `beta`;
  - los nombres son «HTTP Beta», «Nightly Beta» y «Beta».

  Cada rama lleva sus workflows (el push usa los del commit empujado), así que `nightly-beta` no pisa nada de
  `http-beta`.
- **`build.yml`:** en un push a `nightly` construye `ermiry/cerver:nightly` y `:nightly-YYYYMMDD`. Como el resto de
  los workflows, construye sin publicar.
- **`scheduled.yml`, con un despachador en `master`.** Un cron solo corre desde la rama default (`master`) y hace
  checkout de ella. Por eso:
  - cada rama (`core`, `http`, `nightly`) define su gate largo en su `scheduled.yml` (`workflow_dispatch`);
  - en `master`, `scheduled.yml` es el cron que los lanza:

  ```yaml
  # master: .github/workflows/scheduled.yml
  on:
    schedule:
      - cron: '0 3 * * *'
    workflow_dispatch:
  permissions:
    actions: write
  jobs:
    dispatch:
      runs-on: ubuntu-latest
      strategy:
        matrix:
          ref: [core, http, nightly]
      steps:
        - run: gh workflow run scheduled.yml --repo "${{ github.repository }}" --ref "${{ matrix.ref }}"
          env:
            GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
  ```

## 8. Beneficios y riesgos

| Aspecto | Hoy (THREADS) | EVENTS (2.1) |
|---|---|---|
| Qué ocupa un hilo | toda la conexión: cabeceras, body lento, auth y handler | la auth custom y el handler (desde F7, solo el handler) |
| Conexiones simultáneas | ≈ hilos + cola | 2^14; una ociosa o lenta cuesta una `HttpConnection` + arena |
| Saturación con `wait` | el accept se bloquea y el backlog se desborda (`ListenOverflows`) | estacionadas en el loop, con timeout y 503 explícitos; el accept nunca se detiene |
| Rutas lentas vs rápidas | comparten slots | pools nombrados aislados |
| Healthcheck | mide si hay un hilo libre: `unhealthy` bajo carga | liveness desde el loop + readiness por saturación |
| Conexiones TCP | una nueva por salto y por petición | reutilizadas: sin handshake, sin `TIME_WAIT` acumulado, menos accepts |
| Asignaciones por GET | 8 | meta ≤ 4 |
| Slowloris / clientes lentos | un hilo hasta 16 s | un timer |
| Handler sin respuesta | 500 + close (desde la 2.0b-61) | igual |
| WebSocket | 501 hasta F6a; desde F6a, un hilo propio por conexión, con tope | una conexión más del loop (F6b) |
| Observabilidad | stats por endpoint | + pools, cola, espera, conexiones y reusos |

**Sin cambio esperado:** el CPU de los handlers. El ahorro de latencia por salto es ~0.1–0.3 ms en LAN. En C puro,
keep-alive debería multiplicar los req/s de respuestas pequeñas; se mide en `benchmarks/load.c`, no se promete.

**Riesgos:**

- **La carrera de cierre en keep-alive:** la regla de §4.4, y la suite nginx con reinicios.
- **El traspaso loop ↔ pool:** TSan y el flaky hunter de 200 repeticiones en cada fase.
- **Estado que se filtra entre peticiones:** cabeceras, `custom_data` o directorios de una petición en la siguiente.
  Lo cubren `keepalive.c` y el fuzz multi-petición.
- **llhttp más estricto:** las diferencias se documentan en F2.
- **Los conflictos `http` → `nightly`** en `http.c`, `response.c`, `handler.c`, y con el port de la 1.8 en `cerver.c`
  y `client.c`: se mergea en cada beta de `http`, no al final, y F3 espera el port.

## 9. Convenciones

- Las de `CLAUDE.md` (estilo, constantes con nombre, ramas cortas, nombres de una palabra, versión y `CHANGELOG.md`).
- Los defaults nuevos son potencias de 2: 2^14 conexiones, 2^7 s de idle, 2^10 peticiones, 2^8 ms de tick, 2^5 s de
  espera.
- **Los arreglos que sirven a la 2.0 se hacen en `http`** y llegan a `nightly` por un merge después de cada
  2.0b / 2.0rc / 2.0.N, nunca por rebase. Nunca al revés, salvo la integración de §4.11.
