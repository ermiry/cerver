# cerver — Roadmap de la línea 2.1 (`nightly`)

> El plan de trabajo de la línea experimental de cerver: el event loop con epoll, el keep-alive real y los pools de
> handlers separados del I/O. Después, llhttp, WebSocket, la auth asíncrona, TLS y HTTP/2.
>
> | | |
> |---|---|
> | **Fecha** | 2026-11-08 (F0); 2026-11-11 (F2 adelantada; F6a, WebSocket completo sobre hilos); 2026-11-12 (el diseño de F8 y F9; el merge de la 2.0b-70); 2026-11-13 (F1; los arreglos de la revisión, §4.12) |
> | **Rama base** | `http` en la 2.0b-61; mergeada hasta la 2.0b-70 |
> | **Ramas** | `nightly` (desarrollo) y `nightly-beta` (las betas 2.1b-N) |
> | **Estado** | F0 publicada (2.1b-7); F2, llhttp, la 2.1b-8 (§4.2); F6a, WebSocket sobre hilos con el cliente, la 2.1b-9 (§4.6); el merge de la 2.0b-66 a la 2.0b-70, la 2.1b-10; F1, la respuesta estructurada y la recepción por petición, la 2.1b-11 (§4.1); los arreglos de la revisión de la 2.1b-10, la 2.1b-12 (§4.12) |

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
- **Asignaciones por conexión:** en la 2.0b-60, `Connection`, `Socket`, `ConnectionStats` + 2 `PacketsPerType`,
  `CerverReceive` y `file_stats` eran 8 asignaciones por GET. Desde el port de la 1.8 (2.0b-63 y 2.0b-64) las
  conexiones, los sockets y los receives se reciclan con sus stats adentro: GET 1 y POST JSON 19
  (`test/performance.sh`).
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
| **F6a** | 2.1b-9 | WebSocket completo sobre hilos (RFC 6455 y permessage-deflate), el cliente, y el port de la 2.0b-66 que necesita | F2 |
| **Merge** | 2.1b-10 | Un solo merge local de `http` desde el tag de la 2.0b-70 (trae de la 2.0b-66 a la 70: el port de la 1.8 y de la 1.8.1, la seguridad de JWT y JSON, los objetos calientes), y el diseño de F8 y F9 en este roadmap | 2.0b-70 publicada; 2.1b-9 en `nightly-beta` |
| **F1** | 2.1b-11 | Preparación: la lista de cabeceras y la respuesta estructurada, `Connection: close` por petición, `framed`, el receive fuera de la arena con `http_receive_reset`, el log y los stats por petición | F2 + el merge |
| **Arreglos** | 2.1b-12 | Lo que encontró la revisión de la 2.1b-10, primera parte (§4.12): los arreglos de la 2.0b-71 portados a mano, el stream de WebSocket que no empieza, los contadores, la URL y los subprotocolos del cliente, los símbolos internos ocultos, el benchmark de mensajes | F1 |
| **Arreglos** | 2.1b-13 | La segunda parte (§4.12): los de la 2.0b-72 portados a mano, el resto de WebSocket (servidor, cliente, deflate), el parser, la cobertura, y la API de WebSocket (structs opacos, stats, `_milliseconds`, las cabeceras del 101) | 2.1b-12 |
| **F3** | 2.1b-14 | Event loop, cerrando por petición | F1 |
| **F4** | 2.1b-15 | Keep-alive real | F3 |
| **F5** | 2.1b-16 | Pools nombrados, health / ready, métricas | F4 |
| **F6b** | 2.1b-17 | WebSocket sobre el loop | F4, F6a |
| **F8a** | 2.1b-18 | TLS sobre hilos y en los clientes (todos los cervers TCP) | F1 |
| **F8b** | 2.1b-19 | TLS no bloqueante en el loop | F4, F8a |
| **F9a** | 2.1b-20 | HTTP/2 sobre hilos: servidor h2c y h2, WebSocket sobre h2, cliente HTTP | F8a |
| **F9b** | 2.1b-21 | HTTP/2 en el loop | F8b, F9a |
| **F7** | 2.1b-22 | Auth asíncrona | F5 |
| **Merge a `http`** | → 2.1 | Lo terminado, como mínimo F0–F5 (§4.11) | 2.0 en `master` |

- **Los merges de `http` después de la 2.1b-10** (las 2.0b-71 en adelante) entran al principio de la beta de `nightly`
  que siga, como el de la 2.0b-65 en la 2.1b-8, sin una beta propia.
- **El orden pone el loop primero:** F1–F5 es lo que necesitan las aplicaciones (saturación, keep-alive, pools) y el
  mínimo para integrar a `http`. F8a y F9a podrían ir antes del loop (no lo necesitan), pero se hacen después para no
  escribir dos veces la integración con la recepción de THREADS que F1 reorganiza.

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

### 4.1 F1 — Preparación (2.1b-11)

No cambia el modelo en THREADS (una petición por conexión, cerrada después de su respuesta). Separa lo que es «por
conexión» de lo que es «por petición» y deja la respuesta descrita sin su protocolo. **Implementada.**

**La lista de cabeceras** (`http/headers.{h,c}`, `HttpHeaders`):

- las cabeceras en el orden en que se agregaron; un nombre se puede repetir (`Set-Cookie`) y puede ser cualquier token,
  no solo uno de los 63 tipos (un nombre conocido se guarda con su tipo, sin distinguir mayúsculas);
- cada campo queda escrito como su línea `Nombre: valor\r\n` en un búfer: HTTP/1.1 lo envía tal cual, y un protocolo
  que codifica sus cabeceras (HPACK) tiene el nombre y el valor de cada una sin volver a parsear;
- 32 campos y 512 bytes dentro de la lista, y memoria propia hasta 256 campos y 16 KiB;
- un valor con CR, LF o NUL y un nombre que no es token se rechazan (antes no se revisaban), y nada se corta a 256
  bytes.

**La respuesta estructurada** (`http/response.{h,c}`):

- `HttpResponse` es su status y su lista de cabeceras; `header` / `res` son la caché de su forma en HTTP/1.1, que se
  descarta cuando cambia una cabecera o el status;
- **un solo serializador**, `http_response_compile_header ()`: la línea de status, `Server`, las cabeceras de la
  respuesta, las del cerver que la respuesta no tenga (`http_cerver_add_responses_header ()`) y la línea vacía. Los
  cinco formatos escritos a mano (el header simple, el de JSON, el de varias cabeceras, el 304 y los de
  `render_text` / `render_json`) desaparecieron;
- la API: `http_response_header_set / _add / _remove / _count / _get` por tipo y sus variantes `_by_name`; las
  funciones anteriores (`http_response_add_header ()` y las demás) se conservan con su semántica (reemplazar la de su
  tipo);
- **el mensaje** que se envía (`HttpResponseMessage`) lleva el status, las cabeceras de la respuesta y las que cerver
  agrega para la petición como otra lista, en la pila: HTTP/1.1 envía el header compilado, las líneas agregadas, la
  línea vacía y el body en un `sendmsg`, sin asignaciones (GET 0, POST JSON 14);
- **`Connection: close` salió de lo compilado:** se agrega con las cabeceras de la petición cuando la conexión se va a
  cerrar (en THREADS, siempre) y la respuesta no trae su propia `Connection`. El 503 estático lo conserva; el 101 y el
  `100 Continue` no lo llevan;
- **lo que le queda a HTTP/2 (F9a):** el gzip y el 304 todavía filtran el header como bytes (quitan `Content-Length`,
  dejan solo las cabeceras que un 304 conserva); sobre h2 esos dos filtros se hacen en la lista. Un header puesto como
  bytes (`http_response_set_header ()`) solo existe en HTTP/1.1.

**`HttpReceive.framed`:** la respuesta enviada tiene un final que el cliente encuentra sin el cierre de la conexión:
lleva `Content-Length`, su status no tiene body (1xx, 204, 304), contesta un HEAD, es un stream chunked que envió su
último chunk, o un archivo que se envió completo. No lo tienen un stream a un cliente HTTP/1.0, un body sin su
longitud, un header que cerver no puede completar, el 101 ni una respuesta cuyo envío falló. En F1 nadie decide con
él; es la condición de F4 (§4.4).

**La recepción por petición** (`http/http.{h,c}`, `handler.{h,c}`):

- **el receive ya no vive en la arena de su petición:** su dueño le da la memoria y le presta los recursos
  (`HttpReceiveResources`: la arena y los búferes de tokens) con `http_receive_prepare ()`. En THREADS el dueño es el
  hilo (`HandlerThreadContext.http_receive`, 0 asignaciones por conexión); en F3 será la conexión del loop.
  `http_receive_create ()` sigue creando un receive con su propia memoria;
- **`http_receive_reset ()`:** termina la petición (sus uploads, el lugar de su upgrade, el request con sus deleters,
  el parser de sus partes), libera la arena, reinicia el parser (`llhttp_reset ()` y los callbacks que una petición
  cambió) y todos los campos de una petición, y crea el request siguiente. Conserva lo de la conexión: su cerver, los
  recursos, `keep_alive`, `upgraded` y el estado del cliente. En F1 nadie la llama en producción: la usa F4;
- **por petición, dentro del receive:** el reloj (`start_time`), los bytes recibidos, los diagnósticos de `recv ()` y
  las deadlines de cabeceras y de petición. El timeout de inactividad y el `SO_RCVTIMEO` aplicado son de la conexión;
- **el log y los contadores** de una petición que termina están en `http/access.{h,c}` (`http_access_request ()`), y
  `cerver_receive_http ()` es el ciclo de la conexión: prepara el receive, recibe, cierra la petición, entrega la
  conexión a su WebSocket o la suelta.

**Lo que una petición todavía asigna** (medido con valgrind, en régimen): nada en un GET, un JSON, un stream, un
archivo, un POST de texto o un 404; 15 asignaciones con dos parámetros de query (la lista y sus pares) y 45 en un
multipart con un campo y un archivo. Pasarlos a la arena cambia la API de `http_request_get_query_params ()` y de las
partes: queda fuera de F1.

**La validación:** `test/http/headers.c` (la lista), `test/http/responses.c` (la API, los envoltorios, los defaults, la
caché), `test/http/features.c` (`Connection: close` exactamente una vez en cada tipo de respuesta, dos `Set-Cookie`
en el cable y en orden), `test/http/receive.c` (`framed` por tipo de respuesta; muchas peticiones por un mismo
receive con reset, con arena y sin ella, la segunda cortada en cada byte; el cierre de petición por cada estado; un
preflight con cabeceras más largas que las que cabían en una respuesta) y el fuzzer `pipeline` (varias peticiones por
conexión con reset).

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
| Licencia | llhttp es MIT: cada archivo conserva el aviso de copyright y de permiso, y `thirdparty.md` lista el código de terceros |
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

### 4.3 F3 — El event loop (2.1b-14)

**El prerrequisito.** `http` tiene que recibir el port de la 1.8 completa (hasta la 1.8b-9: protocolo portable,
serializador por esquema, game, el deadline total de envío), que cambia `cerver.c`, `handler.c`, `client.c` y
`connection.c`. Llegó con la 2.0b-66, mergeada a `nightly` en la 2.1b-10 (con la 2.0b-70), así que esos conflictos
ya se resolvieron una sola vez.

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

### 4.4 F4 — Keep-alive real (2.1b-15)

**`http_receive_reset ()`, entre peticiones de una conexión** (existe desde F1, §4.1; F4 es quien la llama):

- reinicia `receive_status`, `type`, `dispatch`, `node`, `endpoint`, `route`, `served_file`, `head`,
  `max_body_size`, `status`, `sent`, `is_multi_part`, `header_size`, `n_headers`, `body_size`, `n_parts`,
  `responded`, `framed`, `stream`, `stage`, el reloj, los bytes recibidos, los diagnósticos y las deadlines;
- `settings.on_body` vuelve a `http_receive_handle_body` y el parser se reinicia con `llhttp_reset ()`;
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

### 4.5 F5 — Pools nombrados, health y métricas (2.1b-16)

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

El merge de `ws` en F0 fue `-s ours`, así que su `websockets.c` solo vive en la historia
(`git show ws:src/cerver/http/websockets.c`); F6a escribió WebSocket de nuevo, y hasta ella una ruta con el modifier
`HTTP_ROUTE_MODIFIER_WEB_SOCKET` y una petición con `Upgrade` respondían 501. WebSocket entra en dos partes.

**De `ws` se reusan como referencia:**

- el handshake (`Sec-WebSocket-Key` + GUID, SHA-1 con la API EVP de OpenSSL 3, base64, 101);
- el encoder de frames (`http_web_sockets_send_compile_frame`).

**Se reescriben:**

- el parser de frames: incremental entre lecturas, con la máscara obligatoria del cliente, `max_frame` /
  `max_message` (cierre 1009), fragmentación y UTF-8 en los de texto;
- ping/pong automáticos con timer;
- close con código y su timeout.

#### F6a — WebSocket sobre hilos (2.1b-9)

Corre con el handler de hoy (thpool), sin el loop. **Implementada y validada.** El código está en
`src/cerver/http/websockets/` (headers espejo en `include/cerver/http/websockets/`):

| Módulo | Contenido |
|---|---|
| `text.c` | UTF-8 incremental (estado entre fragmentos, sin overlong, surrogates ni > U+10FFFF, falla en el primer byte malo) |
| `frame.c` | El encoder (la longitud mínima, la máscara) y el parser incremental (los bytes de un frame en muchas lecturas, muchos frames por lectura, desenmascarado por tramos) |
| `message.c` | Las reglas de los mensajes sin sockets: fragmentos y controles intercalados, el close (1002 / 1007), `max_message` (1009, también descomprimido), streaming |
| `handshake.c` | El accept (SHA-1 de EVP), la llave de 16 bytes, los tokens en listas, los subprotocolos |
| `deflate.c` | permessage-deflate (RFC 7692): la negociación en las dos puntas, context takeover y ventanas, el inflate acotado; sin zlib no se negocia. zlib no comprime raw con ventana 2^8: una oferta que la exige se declina |
| `settings.c` | Los handlers y las opciones, heredables por los grupos uno por uno |
| `websocket.c` | `HttpWebSocket`: referencias, su hilo (`poll` del socket y un `eventfd`), los envíos, el cierre, ping / pong, la entrega desde el pool |
| `registry.c` | Las conexiones de cada cerver, el tope (con reservas antes del 101), salas, broadcast, `for_each`, stats |
| `client.c` | `HttpWebSocketClient`: URL, un connect por dirección resuelta, el handshake verificado (RFC 6455 §4.1), headers, subprotocolos, deflate, connect síncrono y asíncrono, reconexión con backoff y jitter |

- **El upgrade:** se valida antes del body en `on_headers_complete` (después de la auth del endpoint): 400, 426 con
  `Sec-WebSocket-Version: 13`, 403 (Origin) y 503 (el tope). En `on_message_complete` corren los middlewares before,
  `on_upgrade ()` (rechaza con su status, elige el subprotocolo, pone los datos y adopta el `decoded_data`) y el 101
  crudo (sin `Content-Length` ni `Connection: close`). Los bytes que llegaron pegados a la petición pasan a la
  conexión. Una petición sin upgrade va al handler del endpoint, o 426 sin él.
- **El hilo:** después del 101, `cerver_receive_http` entrega la conexión a un hilo propio de `connection_threads`
  (el drenado lo espera); el hilo del pool queda libre.
- **El envío (decisión):** escritura directa con un `send_mutex` por frame y un `message_mutex` por mensaje (los de
  streaming lo sostienen de `send_begin` a `send_end`), con deadline (`send_timeout`); un envío que vence falla la
  conexión. Los controles pasan entre los frames de un mensaje. El broadcast usa un timeout corto por conexión. La
  cola de salida escrita por el loop queda para F6b.
- **El apagado:** el drenado manda 1001 a cada conexión y corta las que no terminan antes de la gracia.
- **La API vieja:** una `HttpRoute` con el modifier sigue en 501, con un warning (decisión: WebSocket solo con
  endpoints y grupos).
- **Las métricas:** `cerver_websocket_*` en `cerver/metrics` y `websockets` en el admin.

**La validación:** `test/bin/websockets` (37 unidades, 17 de servidor y 7 de cliente) con gcc, ASan + UBSan, TSan,
valgrind y `::1`; la región del upgrade de `test/http/receive.c` partida en cada byte; los fuzzers `frame` y
`handshake` (y `request` con un endpoint WebSocket); autobahn-testsuite (`test/autobahn/run.sh`) en el servidor y en el
cliente: 517 casos cada uno, 514 OK y 3 INFORMATIONAL, sin NON-STRICT, FAILED ni UNIMPLEMENTED; el par `web/websocket`
en las dos familias; nginx con `Upgrade`; `performance.sh` con 0 asignaciones por mensaje y ~1.9 M mensajes/s
(`benchmarks/messages.c`); `benchmarks/frames.c`.

#### F6b — WebSocket sobre el loop (2.1b-17)

**La conexión vive en el loop** (estado `WEBSOCKET`). Los callbacks `on_open`, `on_message` y `on_close` corren en el
pool de la ruta.

**Aquí sí escribe el loop.** `http_web_socket_send` se llama desde cualquier hilo: encola en una cola de salida acotada
de la conexión y despierta al loop, que escribe con `EPOLLOUT`.

- Con la cola llena: descartar, o cerrar con 1008, según la configuración.
- Un registro de clientes por ruta permite el broadcast.

En EVENTS la conexión pasa al loop; en THREADS sigue en su hilo propio (F6a). Detrás de un proxy van `Upgrade` /
`Connection "upgrade"` y un `proxy_read_timeout` mayor que el ping.

### 4.7 F7 — Auth asíncrona (2.1b-22)

- **Una auth custom que consulta otro servicio por HTTP** deja de ocupar un hilo: un cliente `curl_multi` integrado al
  loop, con `CURLMOPT_SOCKETFUNCTION` / `CURLMOPT_TIMERFUNCTION` sobre el `epoll` y la rueda de timers.
- **La conexión espera en `AUTH` sin hilo,** con un pool de conexiones persistentes por loop.
- **El fallback:** la etapa en el pool, para una auth custom que no usa ese cliente.

### 4.8 F8 — TLS

TLS en proceso para **todos los cervers TCP** (`CUSTOM`, `GAME`, `WEB`, `FILES`, `BALANCER`) y para los clientes (el
cliente TCP, el cliente WebSocket con `wss://` y el cliente HTTP de F9a). UDP queda fuera (DTLS no está en el alcance).
Se parte como WebSocket:

- **F8a (2.1b-18): sobre hilos y en los clientes.** No necesita el loop: el handshake y los registros corren en el hilo
  que hoy hace `recv`, con las deadlines que ya existen.
- **F8b (2.1b-19): en el loop.** El mismo `SSL *` en la máquina de estados de EVENTS, no bloqueante.

#### 4.8.1 Los requisitos (las RFC)

| RFC | Qué exige o recomienda | Cómo lo cumple cerver |
|---|---|---|
| 8446 (TLS 1.3) | El handshake, los registros, `close_notify`, `KeyUpdate`, sin renegociación | OpenSSL 3; `close_notify` en el cierre ordenado; `KeyUpdate` lo maneja OpenSSL |
| 9325 (BCP 195) | TLS 1.2 como mínimo, 1.3 preferido; solo AEAD con forward secrecy; sin compresión ni renegociación insegura; extended master secret en 1.2 | El default de §4.8.3 |
| 8996 | Sin TLS 1.0 ni 1.1 | `SSL_CTX_set_min_proto_version (TLS1_2_VERSION)`, sin opción para bajarlo |
| 6066 (SNI) | El nombre del servidor en el ClientHello | Varios certificados por cerver, elegidos en el `client_hello_cb` |
| 7301 (ALPN) | La negociación del protocolo | `http/1.1` y, con F9a, `h2`; sin ALPN se asume `http/1.1`; un ALPN que no coincide cierra con `no_application_protocol` |
| 7627 | Extended master secret en 1.2 | Exigido (`SSL_OP_NO_EXTENDED_MASTER_SECRET` nunca) |
| 5746 | La indicación de renegociación | Renegociación desactivada (`SSL_OP_NO_RENEGOTIATION`) |
| 6960 / 6066 §8 | OCSP stapling | Opcional, §4.8.6 |
| 8446 §8 | El replay de 0-RTT | 0-RTT apagado por default; opt-in solo para métodos idempotentes, con `425 Too Early` (RFC 8470) |
| 5280, 9525 (antes 6125) | La validación del certificado del servidor en un cliente y su identidad (DNS / IP en el SAN, sin el CN) | `SSL_set1_host` / `X509_VERIFY_PARAM_set1_ip_asc`; la cadena contra el almacén del sistema o un CA propio |
| 8740 | TLS 1.3 con HTTP/2: sin autenticación post-handshake | No se usa la post-handshake auth en conexiones h2 |
| 9113 §9.2 | Las reglas de TLS para HTTP/2 (1.2+, SNI, sin compresión ni renegociación, la lista negra de cifrados en 1.2) | El default de §4.8.3 ya las cumple; una conexión h2 con un cifrado prohibido cierra con `INADEQUATE_SECURITY` |

#### 4.8.2 La arquitectura

**Un transporte por `Socket`.** Hoy la E/S está repartida: `recv` en `handler.c` (TCP y HTTP), `client.c`,
`http/websockets/{websocket,client}.c`; los envíos pasan por `socket_send_*` (`socket.c`); los archivos por `sendfile`
y `splice` (`files.c`). F8a los junta detrás de un transporte:

```c
// include/cerver/tls/transport.h
typedef struct SocketTransport {
	ssize_t (*receive) (struct _Socket *socket, void *buffer, size_t size);           // como recv (), EAGAIN / 0 / -1
	u8 (*send_vector) (struct _Socket *socket, struct iovec *parts, int n_parts, const struct timespec *deadline);
	u8 (*send_file) (struct _Socket *socket, int file_fd, off_t offset, size_t size, const struct timespec *deadline);
	bool (*pending) (const struct _Socket *socket);                                    // bytes ya descifrados
	void (*shutdown) (struct _Socket *socket);                                         // close_notify + shutdown
} SocketTransport;
```

- `Socket` gana un `const SocketTransport *transport` (el plano por default, sin costo) y un `struct TlsConnection *tls`.
- **Cada `recv` directo pasa a `socket_receive ()`** y cada espera con `poll` consulta antes `transport->pending ()`:
  con TLS puede haber un registro ya descifrado en OpenSSL y el fd no estar legible (el hilo de WebSocket y la
  recepción de HTTP lo necesitan).
- **Los envíos** siguen en `socket_send_*_until`: con TLS, `SSL_write_ex` sobre los iovec copiados a un búfer por hilo
  (TLS no tiene `writev`), con `SSL_MODE_ENABLE_PARTIAL_WRITE` y `SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER`, y el mismo
  `poll (POLLOUT)` / `POLLIN` hasta la deadline (`SSL_ERROR_WANT_READ` en un envío espera lectura).
- **Los archivos:** con kTLS, `SSL_sendfile ()`; sin kTLS, lectura a un búfer y `SSL_write_ex` (sin `sendfile` ni
  `splice` de cero copias; los uploads por `splice` pasan a lectura con `SSL_read_ex`).
- **El handshake** corre en el hilo que hoy recibe, antes de la primera lectura, con su propia deadline
  (`CERVER_TLS_HANDSHAKE_TIMEOUT`, 2^3 s): en THREADS el socket se queda no bloqueante y se espera con `poll` como los
  envíos; un handshake que falla o vence se cuenta y se cierra sin respuesta.
- **El modelo POLL** (el `poll` de un hilo del core): el handshake se hace en el thpool de recepción la primera vez que
  el fd está legible, para no bloquear el `poll` principal; hasta que termina el fd no vuelve al conjunto.
- **El cierre:** `close_notify` en el cierre ordenado (respuesta completa, drenado, 1000 / 1001 de WebSocket), sin
  esperar el del otro lado; un cierre sin `close_notify` del cliente en medio de una petición cuenta como hoy (vanished).
- **Los clientes:** `client.c` (TCP), `http/websockets/client.c` (`wss://`) y el cliente HTTP de F9a comparten
  `tls_client_*`: SNI con el host de la URL, ALPN, la verificación de §4.8.1 y una caché de sesiones por destino.

#### 4.8.3 La configuración (el default seguro)

```c
// include/cerver/tls/tls.h
typedef struct TlsSettings TlsSettings;            // opaco, uno por cerver o por cliente

CERVER_EXPORT TlsSettings *tls_settings_create (void);
CERVER_EXPORT void tls_settings_delete (TlsSettings *settings);

// el certificado y la llave: archivos PEM o una URI de un provider de OpenSSL 3 ("pkcs11:...", §4.8.7);
// varios pares por settings, elegidos por SNI; el primero es el default
CERVER_EXPORT u8 tls_settings_add_certificate (
	TlsSettings *settings, const char *certificate_chain, const char *private_key
);

CERVER_EXPORT void tls_settings_set_min_version (TlsSettings *settings, TlsVersion version);   // TLS_VERSION_1_2 (default) | _1_3
CERVER_EXPORT u8 tls_settings_set_ciphers (TlsSettings *settings, const char *tls12, const char *tls13);
CERVER_EXPORT u8 tls_settings_set_groups (TlsSettings *settings, const char *groups);           // X25519MLKEM768:X25519:P-256 …
CERVER_EXPORT u8 tls_settings_set_client_ca (TlsSettings *settings, const char *ca_file, const char *ca_path);
CERVER_EXPORT void tls_settings_set_client_verify (TlsSettings *settings, TlsClientVerify mode);  // NONE | OPTIONAL | REQUIRED

CERVER_EXPORT u8 cerver_set_tls (Cerver *cerver, TlsSettings *settings);   // el cerver toma los settings
```

- **El default** (RFC 9325): TLS 1.2 y 1.3; en 1.2 solo ECDHE con AES-GCM o ChaCha20-Poly1305; en 1.3 los tres
  cifrados de la RFC; grupos `X25519MLKEM768` (si OpenSSL lo trae), `X25519`, `P-256`, `P-384`; sin compresión, sin
  renegociación, extended master secret, `SSL_OP_CIPHER_SERVER_PREFERENCE` en 1.2, `SSL_OP_NO_TICKET` solo si se
  apagan los tickets. Todo cambio del default con un warning al arrancar si baja la seguridad.
- **Un cerver con TLS** escucha solo TLS en su puerto (sin detección de texto plano en el mismo puerto); un cerver puede
  tener un segundo listener plano en otro puerto si la aplicación lo crea aparte.
- **Validación al arrancar** (como las rutas): el certificado contra su llave, la cadena, la fecha (un certificado
  vencido detiene el arranque; uno que vence en menos de 2^4 días deja un warning), cada SNI duplicado; un error detiene
  `cerver_start`.

#### 4.8.4 SNI y mTLS

- **SNI:** `SSL_CTX_set_client_hello_cb` elige el `SSL_CTX` del certificado cuyo SAN coincide (exacto, luego comodín de
  un nivel); sin coincidencia, el default. El nombre queda en la conexión y en el `HttpRequest` (`request->tls.server_name`).
- **mTLS** (`TlsClientVerify`): `NONE` (default), `OPTIONAL` (pide el certificado y lo verifica si llega), `REQUIRED`
  (sin certificado válido el handshake falla). Por cerver, y en un cerver web también **por grupo y por endpoint**
  (`http_group_set_client_certificate (group, HTTP_CLIENT_CERTIFICATE_REQUIRED)`): como TLS 1.3 no renegocia, un
  endpoint `REQUIRED` en un cerver `OPTIONAL` responde 403 si la conexión no trae certificado (sin post-handshake auth,
  RFC 8740).
- **El certificado verificado** queda en la petición: sujeto, SAN, emisor, serie, huella SHA-256 y el PEM bajo demanda
  (`http_request_get_client_certificate ()`), y para la auth de un endpoint un nuevo `HTTP_AUTH_TYPE_CERTIFICATE`.

#### 4.8.5 La recarga sin reinicio

Los tres mecanismos son **opcionales y se eligen por configuración**; ninguno está activo por default:

```c
CERVER_EXPORT u8 cerver_tls_reload (Cerver *cerver);                                       // API explícita
CERVER_EXPORT void cerver_tls_set_reload_signal (Cerver *cerver, int signal_number);        // p. ej. SIGHUP; 0 lo apaga
CERVER_EXPORT void cerver_tls_set_reload_watch (Cerver *cerver, bool enable, unsigned int debounce_ms);  // inotify
```

- **La recarga** lee y valida todo de nuevo (§4.8.3) en un `SSL_CTX` nuevo; si falla, se queda el anterior y se
  loguea. Si pasa, se cambia con un puntero atómico y una referencia (`SSL_CTX_up_ref`): las conexiones nuevas usan el
  nuevo, las vivas terminan con el viejo, que se libera con la última.
- **La señal:** el hilo principal ya desbloquea las señales en `accept` / `poll`; la señal configurada marca una recarga
  pendiente que hace el mismo hilo (no el signal handler). Los hilos de cerver siguen bloqueando SIGINT / SIGTERM y
  ahora también la de recarga.
- **La vigilancia:** un hilo con `inotify` sobre los archivos y sus directorios (las renovaciones reemplazan con
  `rename`), con un debounce (default 2^10 ms) para esperar a que estén los dos archivos; en macOS, con `kqueue`.
- **Las métricas** de la recarga: intentos, fallas, la fecha de vencimiento de cada certificado
  (`cerver_tls_certificate_expiry_seconds`).

#### 4.8.6 Reanudación y OCSP

- **Session tickets** (default activo): las llaves de los tickets las genera cerver en memoria y rotan cada 2^12 s
  (la anterior sigue válida para descifrar un periodo más) con `SSL_CTX_set_tlsext_ticket_key_evp_cb`; varios procesos
  detrás de un balanceador pueden compartir las llaves con `tls_settings_set_ticket_keys ()` (archivo, rotado por la
  aplicación). Caché de sesiones del lado del servidor para TLS 1.2 sin tickets, acotada (2^12).
- **0-RTT** apagado. `tls_settings_set_early_data (settings, max_bytes)` lo activa; en un cerver web solo se acepta para
  GET / HEAD / OPTIONS sin body, y cualquier otra petición recibida como early data responde `425 Too Early`
  (RFC 8470) con `Early-Data: 1` disponible para el handler.
- **OCSP stapling** (`tls_settings_set_ocsp_stapling (settings, true)`): un hilo pide la respuesta OCSP al responder del
  AIA del certificado (o a una URL configurada), la valida y la renueva a la mitad de su validez; la respuesta se
  adjunta en `SSL_CTX_set_tlsext_status_cb`. Si el responder no contesta, el handshake sigue sin staple (no se rompe).
  `must-staple` en el certificado hace obligatorio tenerla: sin ella, un warning al arrancar.

#### 4.8.7 Llaves en un HSM o un provider

- La llave privada puede ser una URI que entienda un provider de OpenSSL 3 cargado por la aplicación
  (`tls_settings_load_provider (settings, "pkcs11", properties)`), abierta con `OSSL_STORE_open` (p. ej.
  `pkcs11:token=...;object=...`). cerver nunca lee la llave: el provider firma.
- Los tests usan SoftHSM2 con el provider PKCS#11 de OpenSSL dentro de la imagen de pruebas.

#### 4.8.8 kTLS

- **Opt-in** (`tls_settings_set_ktls (settings, true)`, Linux con el módulo `tls`): `SSL_OP_ENABLE_KTLS`; si el kernel
  lo acepta para la sesión, los archivos salen con `SSL_sendfile ()` (cero copias) y las lecturas siguen por OpenSSL.
  Si no está disponible, se loguea una vez y se sigue en espacio de usuario. Métrica: conexiones con kTLS en
  transmisión y en recepción.

#### 4.8.9 F8b: TLS en el loop

- El handshake es un estado de la conexión (`TLS_HANDSHAKE`) antes de `READ_HEADERS`: el loop llama `SSL_do_handshake`
  y re-arma `EPOLLIN` / `EPOLLOUT` según `SSL_ERROR_WANT_READ` / `WANT_WRITE`, con su timer en la rueda.
- La lectura usa `SSL_read_ex` en lugar de `recv`, y se repite mientras `SSL_pending` > 0 (con level-triggered el fd
  puede no volver a estar listo).
- Cuando el handler escribe desde el pool (F3), la conexión es suya: usa el mismo transporte con `poll` como en F8a.
  Lo que escribe el loop (respuestas cortas, frames de F6b) usa el búfer de salida y `EPOLLOUT`.
- El apagado manda `close_notify` en el drenado.

#### 4.8.10 Pruebas y gate

- **Unidades** con BIOs en memoria (un cliente y un servidor de cerver sin sockets): handshakes de 1.2 y 1.3, cada
  versión y cifrado prohibido rechazado, SNI con varios certificados, mTLS en los tres modos, tickets y su rotación,
  0-RTT y el 425, la recarga atómica bajo handshakes concurrentes, OCSP con un responder local (`openssl ocsp`), el
  provider PKCS#11 con SoftHSM2, kTLS cuando el kernel de la imagen lo tiene.
- **Interoperabilidad:** `curl`, `openssl s_client`, el cliente de cerver contra nginx con TLS, y **testssl.sh** contra
  un cerver con el default (cero hallazgos de severidad media o mayor).
- **Fuzz:** los registros y el handshake a través del par de BIOs (`test/fuzz/tls.c`), y la lectura de los PEM / URIs.
- **Los pares de integración** con TLS en las dos familias y los sanitizers; `disconnect.c` con TLS (cortes en medio del
  handshake y de un registro); `performance.sh` con handshakes por segundo (con y sin reanudación) y req/s con TLS.
- **Gate:** el de §5 más testssl.sh, la recarga bajo carga (0 errores en las conexiones vivas) y los handshakes que
  fallan contados por motivo.

### 4.9 F9 — HTTP/2

HTTP/2 (RFC 9113) con HPACK (RFC 7541) **escritos por cerver**, como WebSocket: frames, HPACK con el Huffman de la
RFC 7541 (Apéndice B), los streams y el control de flujo. Sin server push. También partido:

- **F9a (2.1b-20): sobre hilos.** Servidor h2c (prior knowledge) y h2 (ALPN con F8a), WebSocket sobre h2 (RFC 8441) y
  un **cliente HTTP público** que habla HTTP/1.1 y HTTP/2.
- **F9b (2.1b-21): en el loop.** Los frames los lee y escribe el loop; los streams van al pool.

#### 4.9.1 Los requisitos (las RFC)

| RFC | Qué exige | Cómo lo cumple cerver |
|---|---|---|
| 9113 §3 | El arranque: el preface del cliente, `SETTINGS` primero; h2 por ALPN, h2c **solo con prior knowledge** (el `Upgrade: h2c` de la RFC 7540 está obsoleto) | El preface se detecta en los primeros 24 bytes de una conexión sin TLS; con TLS, por ALPN `h2` |
| 9113 §4–§6 | Los 10 tipos de frame, el tamaño máximo (2^14 mínimo, configurable hasta 2^24 − 1), los flags, los tipos desconocidos se ignoran | `frame.c` de `http2/` con el mapa X de tipos y errores; parser incremental partido en cada byte, como WebSocket |
| 9113 §5.1 | La máquina de estados de un stream (idle, open, half-closed, closed, reserved) y los ids impares / pares crecientes | Tabla de estados probada transición por transición |
| 9113 §5.2, §6.9 | El control de flujo de la conexión y de cada stream (ventana inicial 65 535, `WINDOW_UPDATE`, el desborde de 2^31 − 1 es error) | Ventanas por stream y por conexión; el handler que envía espera ventana con su deadline |
| 9113 §5.4 | Los errores de conexión (`GOAWAY`) y de stream (`RST_STREAM`) con sus códigos | El mapa X de los 14 códigos |
| 9113 §5.5 | Las extensiones: frames y settings desconocidos se ignoran | Ídem |
| 9113 §6.5 | `SETTINGS` y su ACK, con timeout | El timeout de ACK cierra con `SETTINGS_TIMEOUT` |
| 9113 §6.7, §6.8 | `PING` con ACK y `GOAWAY` con el último stream procesado para un apagado ordenado | El drenado manda dos `GOAWAY` (2^31 − 1 y luego el último id) |
| 9113 §8 | La semántica: pseudo-cabeceras (`:method`, `:scheme`, `:authority`, `:path`, `:status`), nombres en minúsculas, sin cabeceras de conexión (`Connection`, `Keep-Alive`, `Transfer-Encoding`, `Upgrade`, `TE` salvo `trailers`), `Content-Length` contra los DATA, trailers, `CONNECT` | Validación estricta antes de crear la petición; un error es `PROTOCOL_ERROR` del stream |
| 9113 §9.2 | Las reglas de TLS | §4.8.1 |
| 9113 §10 | Las consideraciones de seguridad: límites de cabeceras, de streams, el abuso de frames baratos | §4.9.4 |
| 7541 | HPACK: la tabla estática, la dinámica (4096 por default, `SETTINGS_HEADER_TABLE_SIZE`), los enteros con prefijo, Huffman, los literales nunca indexados para valores sensibles | `hpack.c` probado con los ejemplos del Apéndice C; `Authorization` y `Cookie` cortas como never-indexed |
| 9218 | Las prioridades extensibles: la cabecera `Priority` y el frame `PRIORITY_UPDATE`; `SETTINGS_NO_RFC7540_PRIORITIES` = 1 | Se envía el setting; `urgency` / `incremental` ordenan los envíos de la conexión; los frames `PRIORITY` de la RFC 7540 se leen y se ignoran |
| 8441 | WebSocket sobre h2: `SETTINGS_ENABLE_CONNECT_PROTOCOL` = 1 y el CONNECT extendido con `:protocol: websocket` | El stream entra al mismo código de `http/websockets/` (`message.c`, el registro, los handlers del endpoint) |
| 8470 | `425 Too Early` con 0-RTT | §4.8.6 |

#### 4.9.2 La arquitectura (F9a, sobre hilos)

- **Archivos:** `src/cerver/http/http2/{frame,hpack,huffman,stream,connection,settings,priority,client}.c` y sus
  headers espejo (carpeta en plural no aplica: `http2` es el nombre del protocolo).
- **Un hilo por conexión h2** (el de la recepción de hoy, como el de WebSocket): lee frames, mantiene HPACK, la tabla de
  streams y las ventanas; cada stream con sus cabeceras completas (y su body, hasta `max_body` o en streaming) se
  convierte en un `HttpReceive` / `HttpRequest` y va al thpool como una petición de HTTP/1.1.
- **El handler no cambia:** `(const HttpReceive *, const HttpRequest *)`. Su respuesta pasa por
  `http_response_send_message ()`, que hoy compila los bytes de HTTP/1.1. **Prerrequisito (va en F1, §4.1):** la
  respuesta conserva el status y la lista de cabeceras estructurada, y se serializa por protocolo al enviar (HTTP/1.1
  como hoy, h2 con HPACK). Los streams de `stream.c` (chunked en 1.1) son DATA en h2; los trailers, un HEADERS final.
- **La escritura:** varios handlers escriben a la vez en la misma conexión; cada frame se escribe con el `send_mutex`
  de la conexión (como WebSocket), el handler espera ventana de control de flujo con su deadline, y el planificador de
  §4.9.1 (RFC 9218) reparte la ventana entre los streams listos.
- **Los límites por default** (potencias de 2): `SETTINGS_MAX_CONCURRENT_STREAMS` 2^7, `INITIAL_WINDOW_SIZE` 2^16 − 1
  (ampliable por endpoint con body grande), `MAX_FRAME_SIZE` 2^14, `MAX_HEADER_LIST_SIZE` igual al límite de cabeceras
  de HTTP/1.1, tabla HPACK 2^12.
- **WebSocket sobre h2:** un endpoint WebSocket acepta el CONNECT extendido; el stream se entrega al hilo de la
  conexión h2 (no hay un hilo por WebSocket dentro de una conexión h2): los frames de WebSocket van en DATA y usan el
  mismo `message.c`; los envíos desde otros hilos usan el `send_mutex` de la conexión h2.
- **El cliente HTTP público** (`HttpClient`, `include/cerver/http/client.h`): peticiones con URL, método, cabeceras y
  body; HTTP/1.1 (con llhttp en modo respuesta, como el cliente WebSocket) y HTTP/2 (por ALPN con TLS, o prior knowledge
  configurado); un pool de conexiones por destino (keep-alive en 1.1, multiplexado en h2); timeouts por petición;
  redirecciones opcionales; IPv4 / IPv6 con un connect por dirección. El cliente WebSocket puede usar h2 + RFC 8441.

#### 4.9.3 F9b, en el loop

- El loop lee y escribe los frames de todas las conexiones h2 (sin un hilo por conexión); los streams van al pool del
  endpoint (F5); las respuestas del handler entran como frames a la cola de salida de la conexión (la de F6b) con el
  control de flujo aplicado por el loop.
- El mismo parser incremental de F9a, ahora alimentado por el búfer de lectura del loop.

#### 4.9.4 La seguridad (los ataques conocidos de HTTP/2)

| Ataque | Defensa (default) |
|---|---|
| Rapid reset (CVE-2023-44487): streams abiertos y cancelados con `RST_STREAM` | Cuenta de streams creados y reseteados por ventana de tiempo; superar 2^9 resets por 2^5 s cierra con `GOAWAY ENHANCE_YOUR_CALM`; el trabajo de un stream reseteado se cancela antes de despachar si aún no empezó |
| CONTINUATION flood | El tamaño acumulado de un bloque de cabeceras (HEADERS + CONTINUATION) contra `MAX_HEADER_LIST_SIZE` antes de decodificar; un número máximo de CONTINUATION por bloque |
| HPACK bomb | El tamaño decodificado se limita por la lista de cabeceras, no por los bytes recibidos; la tabla dinámica nunca crece sobre el setting propio |
| Settings / ping / priority flood | Límite de frames de control por segundo; los ACK pendientes acotados |
| Frames vacíos (DATA de 0 sin END_STREAM, CONTINUATION vacíos) | Cuenta por stream y por conexión; superar el límite es `ENHANCE_YOUR_CALM` |
| Ventanas en cero (slow read) | Deadline de envío por stream y por conexión: un cliente que no abre la ventana pierde el stream y luego la conexión |
| Demasiados streams | `MAX_CONCURRENT_STREAMS` y `REFUSED_STREAM` para los de más, sin crear la petición |

#### 4.9.5 Pruebas y gate

- **Unidades:** cada frame partido en cada byte; HPACK con los ejemplos del Apéndice C de la RFC 7541 (con y sin
  Huffman) y la tabla dinámica en sus límites; la máquina de estados de los streams; el control de flujo; la
  validación de §8 de la RFC 9113 (cada cabecera prohibida); las prioridades.
- **Conformidad:** **h2spec** contra el servidor (h2c y h2), todas las secciones de la RFC 9113 y la RFC 7541 sin fallas;
  el cliente contra nghttpx / nginx y contra un cerver.
- **Fuzz:** `test/fuzz/{http2,hpack}.c` (frames y bloques de cabeceras), con corpus de las capturas de los tests.
- **Interoperabilidad:** `curl --http2` y `--http2-prior-knowledge`, `h2load` para la carga, navegadores con TLS.
  Detrás de un proxy: un proxy con h2 hacia el upstream (nginx solo lo hace con `grpc_pass`; HAProxy con `proto h2`)
  y el caso común, el proxy termina h2 y habla HTTP/1.1 con cerver (F4, keep-alive).
- **Los ataques de §4.9.4** como tests: cada uno debe terminar en el `GOAWAY` o `RST_STREAM` esperado sin pasar los
  límites de memoria y de CPU del test.
- **WebSocket sobre h2:** los mismos tests de `test/bin/websockets` sobre un stream h2, y autobahn a través de un
  cliente h2 si la herramienta lo soporta (si no, el cliente de cerver por h2 contra el servidor de cerver).
- **Rendimiento:** req/s y asignaciones por petición en h2 (meta: las mismas que HTTP/1.1 con keep-alive) y por stream.
- **Gate:** el de §5 más h2spec en verde, el fuzz de 30 min por target nuevo y los tests de §4.9.4.

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

### 4.12 Los arreglos de la revisión de la 2.1b-10 (2.1b-12 y 2.1b-13)

Una revisión de solo lectura de lo que llevaba `nightly` en la 2.1b-10 (el parser, WebSocket en el servidor y en el
cliente, los tests y el CI, la API y los símbolos) encontró defectos propios de esta línea y otros heredados de `http`.
Se arreglan en dos betas antes del event loop, cada arreglo con un test que falla sin él.

**Los arreglos de `http` se portan a mano, sin merge.** La 2.0b-71 y la 2.0b-72 son las betas de `http` con los
arreglos heredados; aquí se aplican archivo por archivo, porque el código ya es otro (llhttp, la lista de cabeceras).
El siguiente merge de `http` (al empezar F3) trae esos mismos cambios: en `request.c`, en el query de `http.c`, en
`test/client/web/api.c` y en el `makefile` de los fuzzers se queda el lado de `nightly`, que ya los tiene; en
`headers.{h,c}` y `response.c`, también (la lista de F1 quita el defecto de la 2.0b-71 por construcción).

#### 2.1b-12 — implementado

- **De la 2.0b-71:** `Range` con un parser propio (el rango de los últimos bytes y los valores sin números se
  ignoran; llegaban a `atol (NULL)`); el query y los valores de un formulario se parten en `&` y `=` antes de
  decodificar cada pieza; los fuzzers se detienen en el primer comportamiento indefinido de UBSan; el cliente de
  prueba `web/api` codifica cada valor de su formulario, no el body entero.
- **El stream de WebSocket que no empieza.** `http_web_socket_send_begin ()` regresaba 1 con `message_mutex` tomado
  cuando la conexión ya cerraba: el siguiente envío del hilo no regresaba nunca. Ahora un stream que no empieza no
  retiene nada, y una pieza que la conexión no tomó porque está cerrando es un error del envío que no la aborta.
- **El tiempo de un envío** empieza cuando el mensaje se puede escribir, no antes de esperar a los otros mensajes.
- **Los contadores:** los bytes de un mensaje que se manda en piezas y los de cada fragmento que se recibe.
- **El cliente:** una URL con espacios o caracteres de control se rechaza (escribía otras líneas en la petición); los
  subprotocolos son tokens y una lista rechazada no cambia nada (una entrada `NULL` dejaba un hueco).
- **Los símbolos internos:** la API de llhttp (`parser/api.h`, y todos los objetos de `parser/` con
  `-fvisibility=hidden`), las funciones internas de WebSocket (`websocket.h`, `registry.h`), los
  `http_web_socket_settings_*` y `http_response_send_pieces_status ()` son `CERVER_HIDDEN`: `libcerver.so` exporta
  153 símbolos menos y ninguno de llhttp.
- **Menores:** el example de WebSocket acota el largo de lo que manda; tres programas liberan un cerver que no
  arrancó con `cerver_teardown ()`; `autobahn-reports/` ignorado por git y por Docker.
- **El benchmark de mensajes** mide del primer mensaje al último eco, sin el tiempo de abrir y cerrar las
  conexiones, con un millón de mensajes y comprobando el eco entero; medía una corrida de ~20 ms.

#### 2.1b-13 — planeado

- **De la 2.0b-72:** los trailers y las extensiones de chunk contra los límites; los espacios finales del valor de
  una cabecera; la cabecera repetida que recopia su valor; el target en forma absoluta; el límite configurable de
  una cabecera que no es de las conocidas; el default de `request_timeout` en 300 s; el rango de los últimos bytes;
  `performance.sh` que falla cuando el servidor o la carga fallan; los objetos de los tests con sus headers; el fuzz
  largo en matriz.
- **WebSocket en el servidor:** un envío que pierde la carrera con un close no es un fallo; la conexión que no
  consigue su hilo; el cierre con `SHUT_WR` y un drenado acotado; el hilo que no lee mientras otro envía.
- **El cliente:** borrar o esperar desde un callback; un intento que se puede cancelar; `EINTR`; el backoff.
- **permessage-deflate:** el mensaje vacío, el bloque sin terminar, el ejemplo de RFC 7692 sección 7.2.3.4.
- **El parser:** una respuesta para cada petición con los flags lenient; solo `HTTP/1.0` y `HTTP/1.1`.
- **La API de WebSocket** (cada cambio en `incompatibilities.md`): los structs opacos, `HttpWebSocketStats`, el
  sufijo `_milliseconds` en los timeouts, las cabeceras que `on_upgrade` agrega al 101.
- **La deuda de F1:** el gzip y el 304 eligen sus cabeceras en la lista.
- **Cobertura y CI:** lo que la revisión encontró sin test.

## 5. Plan de tests y el gate de `nightly`

**El gate de `nightly` es el de `http` más:**

- **El de `http`:** gcc con los 4 TYPEs, g++, unit con ASan + UBSan, TSan y valgrind, los pares de integración × 4
  variantes, el fuzz y `test/performance.sh`.
- **El flaky hunter:** 200 repeticiones (`test/repeat.sh`) de las suites que tocan el loop, también en TSan.
- **Fuzz:** ≥ 30 min por target (el de `http` es de 30 s), con el target de varias peticiones por conexión
  (`pipeline`, desde F1).
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
- **`benchmarks/load.c`** con modo keep-alive y umbrales nuevos en `performance.sh`. La meta es no pasar de las
  asignaciones de hoy (GET 1, POST JSON 19) con keep-alive, y medir keep-alive contra close por petición.
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
| Asignaciones por GET | 1 (8 en la 2.0b-60) | 1, también con keep-alive |
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
  2.0b / 2.0.N, nunca por rebase. Nunca al revés, salvo la integración de §4.11.
