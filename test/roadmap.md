# cerver — Roadmap de la suite de tests

> El plan para que la suite detecte los problemas de cerver core y de HTTP antes de los desarrollos y las fases
> siguientes (`roadmap.md` §4.3–4.4).
>
> | | |
> |---|---|
> | **Fecha** | 2026-11-05 |
> | **Rama base** | `http`: Beta 2.0b-58 + la Fase 3 sin commitear + el arreglo del ciclo de vida del cliente (§2) |
> | **Origen** | El cuelgue del par TCP `threads` (1 de 55 corridas). Se debía a un auto-deadlock del cliente que ningún test ejercitaba, y motivó el inventario de §1 |
> | **Estado** | **Implementado** (etapas A–I), con las funcionalidades nuevas de §7. El resultado, los bugs que encontró y lo pendiente están en §7 |

---

## 1. Diagnóstico: lo que la suite no detecta hoy

### 1.1 El harness de integración

| Problema | Dónde |
|---|---|
| Clientes que siempre salen con 0 | `test/handler/client.c` (imprime y retorna 0), `test/client/web/admin.c:101,105` (ignora sus errores) |
| Un resultado ignorado | `test.sh:64` corre `client/requests` sin `\|\| exit 1` |
| CI y `test.sh` corren pares distintos | `test.sh` no corre `handler`; CI no corre `web/multi` |
| Esperas fijas | cada par espera `sleep 2` al servidor; los clientes TCP duermen 2–8 s para esperar respuestas |
| Sin timeouts | ningún cliente web pone `CURLOPT_TIMEOUT`; un servidor colgado bloquea el job |
| Sin sanitizers | ASan / UBSan / TSan solo corren los unit tests (`build.yml`), nunca los pares |
| Asserts fuera del proceso que falla | las verificaciones en los callbacks de los servidores TCP corren dentro del contenedor, y una falla solo se ve como un cliente que falla |
| Fugas previas | con `detect_leaks=1` fallan 5 clientes de test (`packets`, `sessions`, `json`, `auth`, `api`), ya desde 2.0b-58 |
| Carreras en los propios tests | `responses` se escribe en el hilo de recepción y se lee en main sin sincronizar (`client/{auth,threads}.c`, `handler/client.c`); `test/app/handler.c:95-103` compara memoria sin inicializar |

### 1.2 Cerver core (TCP y cliente)

- **No hay un cerver TCP en proceso** (como `test/http/server.c` para HTTP). Los flujos del core solo existen en los
  pares de Docker.
- **Nunca se prueban:**
  - el cierre o el drop de un cliente por el cerver;
  - la autenticación fallida, `max tries` y `on hold`;
  - `bad_packets`, `max_received_packet_size`, los clientes inactivos;
  - los hilos `update` / `interval` y el teardown con ellos.
- **El handler POLL** solo se usa en pares TCP simples; ningún test HTTP lo usa.
- **Cliente:** solo el camino feliz. No hay tests de `client_connect_async`, `client_connect_and_start_async`,
  `client_request_to_cerver_async`, la reconexión, varios clientes a la vez, ni los `CLIENT_EVENT_*` (salvo el nuevo
  `client/lifecycle`, §2).
- **Paquetes:** `packet_send_split`, `_pieces`, `_to_socket`, `packet_check` y `packet_route_between_connections` nunca se
  llaman.
- **Módulos con 0 %:**
  - admin cerver (34 funciones públicas);
  - game (lobby, player, score, gametype: 71 funciones);
  - timer, serializer, input.

### 1.3 HTTP

- **No hay fuzzing** de nada que reciba entrada externa: el parser, multipart, la URL, JSON, JWT ni el registro de rutas.
- **Huecos que el roadmap §5 ya pedía:**
  - multipart con filename vacío o con `../`, un destino que ya existe, un body de 100 MB;
  - un RST del cliente a mitad de la respuesta;
  - nginx delante (el 502).
- **Bearer JWT con llaves reales** a través de `receive` (es un hueco declarado en `test/http/receive.c:43`).
- **HTTP con el handler POLL.**
- **El JSON de admin** (`/cerver/stats`, ahora con `endpoints`) no se valida.
- **Sin protección contra regresiones de rendimiento:** asignaciones por petición, throughput, match del árbol.

## 2. Ya hecho en esta sesión: el ciclo de vida de las conexiones del cliente

Se hizo antes de este roadmap, con aprobación.

**La causa del cuelgue:** el hilo de recepción llamaba `client_connection_end` y esperaba a que él mismo terminara. Lo
mismo pasaba con el cierre por el cerver y con el paquete `CLOSE_CONNECTION`.

**Arreglado en `client.c` / `connection.c`:**
- cada conexión tiene un solo dueño de su cierre;
- el `recv` se despierta con `shutdown` y se espera al hilo antes de cerrar y borrar;
- el cierre que se pide desde el propio hilo es diferido;
- `updating` se marca antes de crear el hilo;
- `client_teardown` espera a todos los hilos, sin el `sleep (4)`;
- se quita la conexión por su dirección (antes por fd, después de cerrarlo);
- el `else if` de `client_receive_data` era siempre verdadero;
- los stats del cliente son atómicos;
- `running` es atómico;
- se libera el paquete `CLIENT`;
- `handlers_lock` ya no se fuga al reiniciar.

**Test nuevo `client/lifecycle`** (en `run.sh`), con un cerver falso en el proceso:
- el fin desde la app con `recv` activo y antes de que exista el hilo (×64);
- el cierre remoto;
- `CLOSE_CONNECTION` (×64);
- varias conexiones, `client_disconnect` y `client_teardown` (×64, < 2 s).

**Contra 2.0b-58:** falla las 3 corridas (abort / segfault). **Con el arreglo:**
- 50 / 50 normal, 20 / 20 con ASan, 40 / 40 con TSan, valgrind limpio;
- el par `threads` pasa 300 corridas sin colgarse (antes se colgaba en la 55).

## 3. Decisiones

| Tema | Decisión |
|---|---|
| Alcance | Las 12 propuestas de §4 |
| CI | **PR ligero + nightly pesado.** En cada PR: harness de integración, integración con ASan/UBSan, valgrind de unit tests, fuzzing corto (30 s por target). Nightly (`schedule`) y manual (`workflow_dispatch`): TSan de integración, flaky hunter, fuzzing largo (5 min por target), nginx, rendimiento |
| Bugs que encuentren los tests | **Se arreglan todos** en la misma sesión, cada uno con su test, y se reportan al final |
| Umbrales de rendimiento | Asignaciones exactas, `load` con 0 errores, el árbol ≥ 2× el matcher viejo, **más un mínimo de req/s** relativo a una línea base guardada en el repo, con tolerancia amplia |
| Game | Mínimo: unit tests sin red |
| git | Ninguna acción; los commits los decide el usuario |

## 4. El plan

Cada etapa cierra con:
- `run.sh` en verde con y sin sanitizers;
- los pares de integración en verde;
- gcc y g++ sin warnings nuevos.

### Etapa A — El harness (propuestas 1, 3, 4)

| # | Trabajo | Archivos |
|---|---|---|
| A1 | `test/integration.sh`: el runner único de los pares, el mismo para `test.sh` y CI | `test/integration.sh` (nuevo), `test.sh`, `.github/workflows/test.yml` |
| A2 | El servidor de cada par arranca en el mismo contenedor que el cliente, así los sanitizers y los logs ven a los dos | `test/integration.sh` |
| A3 | Readiness probe: esperar a que el puerto acepte (con deadline) en lugar de `sleep 2` | `test/integration.sh` |
| A4 | Todo cliente corre con `timeout`, y todo request de curl con `CURLOPT_TIMEOUT` / `CONNECTTIMEOUT` | `test/client/web/curl.c` |
| A5 | Exit codes reales | `test/handler/client.c`, `test/client/web/admin.c`, `test.sh` |
| A6 | Esperas por condición (con deadline) en lugar de `sleep` fijos, y contadores atómicos en los clientes TCP | `test/client/{packets,auth,sessions,threads,ping}.c`, `test/handler/client.c` |
| A7 | Corregir `app_handler_message` (lee memoria sin inicializar) | `test/app/handler.c` |
| A8 | Flaky hunter: `test/repeat.sh <binario o par> <N>` que guarda los stacks (gdb) si algo se cuelga | `test/repeat.sh` (nuevo) |
| A9 | Job de valgrind sobre `run.sh` (`--error-exitcode`, `--leak-check=full`) | `.github/workflows/build.yml` |
| A10 | Todos los pares, incluidos `handler`, `multi` y `router`, en `test.sh` y en CI | `test.sh`, `test.yml` |

### Etapa B — Integración con sanitizers (propuesta 2)

| # | Trabajo |
|---|---|
| B1 | Arreglar las 5 fugas previas de los clientes de test (`packets`, `sessions`, `json`, `auth`, `api`). Por ejemplo, `curl_slist` sin `curl_slist_free_all`, o la segunda conexión de `sessions` sin terminar. Si una fuga es de cerver, se arregla en cerver |
| B2 | Job de PR: los pares con `SANITIZER=address,undefined` y `detect_leaks=1` |
| B3 | Job nightly: los pares con `SANITIZER=thread` (`seccomp=unconfined` en Docker) |

### Etapa C — Cerver TCP en proceso (propuesta 5)

`test/cerver/core.c` (binario `cerver/core`): un `CERVER_TYPE_CUSTOM` en un hilo, como `http/server.c`, con
clientes de cerver o sockets crudos.

| Caso | Verifica |
|---|---|
| POLL y THREADS | el mismo flujo de paquetes con los dos handler types |
| Dispatch | `app_packet_handler`, `app_error_packet_handler`, `custom_packet_handler`, handlers múltiples (`cerver_set_multiple_handlers`) |
| Cierre por el cerver | `client_drop` / `connection_drop` desde un handler; el cliente ve el cierre remoto (§2) |
| Auth | éxito; falla; `max tries` que desconecta; `on hold` que expira; sesiones con dos conexiones |
| Paquetes malos | `bad_packets` hasta desconectar, `max_received_packet_size`, `check_packets` con una versión equivocada |
| Clientes inactivos | `cerver_set_inactive_clients` los cierra |
| Hilos `update` / `interval` | corren y se detienen en el teardown sin cuelgues |
| Teardown con clientes conectados | drena y libera (ASan / TSan limpios) |
| Eventos del cerver | cada `CERVER_EVENT_*` relevante se dispara una vez |

### Etapa D — Cliente extendido y envío de paquetes (propuestas 6, 7)

| # | Trabajo | Archivo |
|---|---|---|
| D1 | `client_connect_async`, `client_connect_and_start_async` y `client_request_to_cerver_async`, con eventos `CONNECTED` / `CONNECTION_FAILED` | `test/client/lifecycle.c` (amplía) |
| D2 | Reconexión: el mismo `Client` después de terminar todas sus conexiones, y con una conexión nueva | `test/client/lifecycle.c` |
| D3 | Varios clientes a la vez contra el mismo cerver falso, con TSan | `test/client/lifecycle.c` |
| D4 | Cada `CLIENT_EVENT_*` se dispara una vez, y su data se libera | `test/client/lifecycle.c` |
| D5 | `packet_send_split`, `_pieces`, `_to_socket`, `packet_send_request` sobre `socketpair`, con el receptor de `receive.c`: los bytes llegan completos y en orden | `test/packets.c` (amplía) |
| D6 | `packet_check` (versión y tamaño) y `packet_route_between_connections` | `test/packets.c` |

### Etapa E — Admin y game (propuesta 8)

| # | Trabajo | Archivo |
|---|---|---|
| E1 | Admin cerver: habilitarlo, conectar un admin con auth, `admin_cerver_broadcast_to_admins`, `admin_send_packet`, que se desconecte | `test/cerver/admin.c` (nuevo binario `cerver/admin`) |
| E2 | Game, mínimo y sin red: lobbies (crear, buscar, unir, dejar, borrar), players, scores, gametypes | `test/game/*.c` (nuevo binario `game`) |
| E3 | Timer y serializer: lo básico, por cobertura | `test/utils/timer.c`, `test/serializer.c` |

### Etapa F — Fuzzing (propuesta 9)

`test/fuzz/*.c` con libFuzzer (`clang -fsanitize=fuzzer,address,undefined`), un target por entrada:

| Target | Entrada |
|---|---|
| `fuzz/request` | bytes crudos a `http_receive_handle` (parser + callbacks de cerver: cabeceras, límites, multipart, el árbol), partidos en puntos derivados del input |
| `fuzz/multipart` | el parser multipart con un boundary fijo |
| `fuzz/url` | `http_url_decode`, `http_url_decode_path`, el parseo de la query |
| `fuzz/json` | `json_loadb` |
| `fuzz/jwt` | `jwt_decode` con una llave fija |
| `fuzz/tree` | el registro de paths arbitrarios (`http_tree_path_validate`, `http_group_route_add`) y el match de URLs arbitrarias contra un árbol fijo |

- **Corpus semilla:** `test/fuzz/corpus/<target>`, con las peticiones de `test/http/receive.c` y las rutas reales.
- **Makefile:** un target `fuzz` (`CC=clang`).
- **CI:** 30 s por target en el PR, 5 min en nightly. Un crash sube el input como artifact.

### Etapa G — Casos borde HTTP (propuesta 10)

En `test/http/receive.c` / `server.c` y un par nuevo cuando haga falta.

| Caso | Esperado |
|---|---|
| Multipart con filename vacío | se trata como valor o se rechaza, sin crear archivo |
| Multipart con filename `../x` | el nombre se sanea y nunca se escribe fuera de uploads |
| Multipart a un destino que ya existe | un nombre numerado (como hoy), nunca se sobrescribe |
| Body de 100 MB | se recibe completo con el límite alto; 413 con el default |
| RST del cliente a mitad de la respuesta | el hilo no muere (sin SIGPIPE), la conexión se libera |
| Bearer JWT real | RS256 con `test/http/keys`: válido 200, expirado / firma mala / sin llave 401 |
| HTTP con POLL | los casos básicos de `server.c` con `CERVER_HANDLER_TYPE_POLL` |
| JSON de admin | `/cerver/stats` contiene las rutas, los endpoints y los contadores esperados |

### Etapa H — Regresión de rendimiento (propuesta 11)

`test/performance.sh` (nightly):
- `benchmarks/allocations.sh` con umbrales: GET ≤ 11 y POST JSON ≤ 35 asignaciones por petición;
- `load` con concurrencia 8 y 64: 0 errores, 0 respuestas 503 y req/s ≥ 50 % de la línea base de
  `benchmarks/baseline.txt` (se guarda por arquitectura);
- `benchmarks/bin/tree` sobre tablas sintéticas: el árbol ≥ 2× el matcher viejo en la misma corrida.

Las tablas de rutas son sintéticas: `benchmarks/generate.py` (con una semilla fija) escribe
`benchmarks/routes/{small,medium,large}.txt` (64 / 256 / 1024 rutas). Mezclan estáticos, parámetros, varios niveles,
varios métodos por path y prefijos compartidos.

### Etapa I — nginx delante (propuesta 12)

`test/nginx/` con `docker-compose.yml` (la imagen oficial de nginx con versión fija + el servidor `test/web/router`)
y una configuración de reverse proxy genérica: HTTP/1.1 y keepalive hacia upstream, con un `keepalive_timeout` menor
que el timeout de inactividad de cerver.

- **El reproductor del 502 (`test/nginx/run.sh`):** rondas de 8 hilos × 160 peticiones, con pausas que cruzan los
  timeouts de keepalive. Pasa con 0 respuestas 502 y 0 `Connection reset by peer` en el error log de nginx.
- **CI:** nightly.

## 5. Orden y estimación

| Orden | Etapa | Por qué en ese orden | Tamaño |
|---|---|---|---|
| 1 | A — harness | el resto depende de un runner confiable y sin sleeps | medio |
| 2 | B — sanitizers en integración | convierte los 18 pares en detectores de memoria y carreras | medio |
| 3 | C — cerver TCP en proceso | el hueco más grande del core | alto |
| 4 | D — cliente y paquetes | completa lo de §2 | medio |
| 5 | G — casos borde HTTP | huecos conocidos del roadmap §5 | medio |
| 6 | F — fuzzing | necesita los harness de C/G para reutilizar el feeder | medio |
| 7 | E — admin y game | cobertura de módulos sin uso en los servicios | medio |
| 8 | H — rendimiento | necesita las fixtures y la línea base | bajo |
| 9 | I — nginx | la regresión de extremo a extremo | medio |

## 6. Verificación de esta etapa de trabajo

- Cada test nuevo **falla contra el código que tenía el bug** (como `client/lifecycle` contra 2.0b-58) o, si cubre algo
  sin bug, se valida rompiendo a propósito el comportamiento que protege.
- `run.sh` limpio con gcc, ASan + UBSan (`detect_leaks=1`) y TSan, y valgrind sin errores.
- Los 18 pares limpios, normales y con sanitizers.
- El flaky hunter: 200 repeticiones de `threads`, `lifecycle` y `http/server` sin fallas.
- El roadmap principal (`roadmap.md` §5) apunta a este archivo.

## 7. Resultado

### Lo que se agregó

| Etapa | Archivos |
|---|---|
| A | `test/integration.sh`, `test/pair.sh`, `test/process.sh`, `test/repeat.sh`, `Dockerfile.test` (un contenedor por par, `ARG SANITIZER`), `.dockerignore`; esperas por condición en `test/test.h`; exit codes reales; timeouts de curl; `RUNNER` en `run.sh` y el job de valgrind |
| B | jobs de integración con ASan + UBSan (PR) y TSan (`nightly.yml`); los 18 servidores de test ya no liberan nada en el handler de señal |
| C | `test/cerver/core.c` |
| D | `test/client/lifecycle.c` (async, reconexión, varios clientes, eventos), `test/packets.c` (envíos sobre `socketpair`, `packet_check`, el ruteo entre conexiones) |
| G | `test/http/receive.c` (nombres de archivo peligrosos, Bearer RS256, un atributo multipart sin valor), `test/http/server.c` (POLL, RST a mitad de la respuesta, body de 100 MB), el JSON de admin en el par `web/admin` |
| F | `test/fuzz/` (6 targets, corpus semilla), `Dockerfile.fuzz`, `test/fuzz.sh`, el target `fuzz` del makefile |
| H | `test/performance.sh`, `benchmarks/generate.py`, `benchmarks/routes/`, `benchmarks/baseline.txt` (aarch64) |
| I | `test/nginx/` |

### Los bugs que encontró la suite (todos arreglados, cada uno con su test)

- **Auth / on hold:** el hilo `on_hold_poll` era detached y su arreglo de fds se liberaba mientras hacía `poll`
  (use-after-free); el registro y el poll compartían el arreglo sin lock (y `current_on_hold_nfds` sin
  sincronizar); `max tries` de `cerver_set_auth` nunca llegaba a la conexión; el session id generado se fugaba.
- **Handler POLL:** con auth, el hilo `on_hold` registraba conexiones (y hacía `realloc`) en el arreglo que el hilo
  principal estaba usando en `poll`.
- **Handlers múltiples:** el arranque esperaba con un busy-wait sobre un entero no atómico (se colgaba con `-O2`) y
  contaba también los handlers sin hilo; un paquete sin handler se fugaba.
- **Clientes inactivos:** `cerver_set_inactive_clients` no cerraba nada y `last_activity` nunca se actualizaba.
- **Cliente:** `client_connect_async` no disparaba `CONNECTED` / `CONNECTION_FAILED`; `CONNECTED` se disparaba antes
  de marcar la conexión activa; los hilos async usaban el cliente durante el teardown; `cerver_report` se escribía
  sin atomics; el id de los clientes era un contador sin atomics.
- **Paquetes:** `packet_generate` liberaba el header de un paquete de `packet_create_request`; `packet_send_pieces`
  ignoraba los errores de las piezas; `packet_route_between_connections` podía perder bytes en el pipe.
- **Jobs:** el hilo de la cola era detached y `job_queue_delete` la liberaba con el hilo vivo; `running` sin atomics.
- **HTTP:** un filename vacío, `.` o `..` apuntaba al directorio de uploads; los contadores de `HttpCerver` y los
  stats se leían sin atomics.
- **Encontrados por el fuzzing:** un atributo multipart sin valor y un JWT sin `alg` desreferenciaban NULL (los dos
  tumbaban el proceso con una sola petición); `json_equal` comparaba reales como `float` con tolerancia.
- **Socket:** el mutex de escritura se destruía sin esperar un envío en curso.
- **Encontrado por valgrind:** el paquete de error (`error_packet_generate`) enviaba por la red bytes del heap sin
  inicializar (campos del header y padding).

### Funcionalidades nuevas (decididas después de la primera corrida)

| Tema | Decisión | Test |
|---|---|---|
| `packet_check` | El mismo protocol id y `major`, y un `minor` que no sea más nuevo que el local | `test/packets.c` |
| El on hold expira | `cerver_set_on_hold_timeout (cerver, secs)`, 16 s por default, 0 lo desactiva; se dropea con `CERVER_EVENT_ON_HOLD_DROPPED` | `test/cerver/core.c` (vence, y con 0 no vence) |
| Dropear desde un handler | `connection_drop_request (connection)`: un `shutdown` diferido; el loop de recepción la quita cuando el handler retorna | `test/cerver/core.c` (POLL y THREADS: el eco anterior llega, el cerver sigue atendiendo) |
| `client_request_to_cerver_async` | Dispara `CLIENT_EVENT_CONNECTION_DATA` al terminar; `client_requests_wait (client)` espera a todos los hilos async | `test/client/lifecycle.c` |

### Etapa E

- **Admin cerver** (en `test/cerver/core.c`, para reutilizar el harness): auth de admin, los handlers del admin,
  `admin_cerver_broadcast_to_admins`, la desconexión.
- **Game** (`test/game/game.c`, binario `game`, sin red): scores, gametypes, players y lobbies.
- **Timer y serializer** (en el binario `utils`): `test/utils/timer.c` y `test/utils/serializer.c`.

**Los bugs que encontraron:**
- **Registro on hold:** el hilo de accept usaba la conexión después de entregarla al hilo on hold, que la podía
  dropear; el fd se registraba en el poll antes de estar en los mapas.
- **Admin cerver:**
  - su poll compartía el arreglo de fds con el registro, sin lock;
  - su hilo era detached y se liberaba lo que usaba;
  - la lista de admins se recorría sin su mutex.
- **Scores:** el módulo estaba roto (se reescribió con el mismo API):
  - claves con el tamaño de un puntero;
  - valores en la pila;
  - `update` sumaba dos veces;
  - `add_player` siempre fallaba;
  - `add_scoreType` y `remove_scoreType` no funcionaban;
  - fugas.
- **Game types:** `game_type_get_by_name` devolvía el último tipo si no encontraba el nombre;
  `game_type_unregister` usaba el elemento que acababa de liberar.
- **Player:** un segundo `player_set_id` fugaba el id.
- **Timer:** `clock_gettime (4, ...)` en lugar de `CLOCK_MONOTONIC_RAW`.

### Versión de los paquetes y bad packets

**La versión en el wire.** `PacketHeader` termina con un `PacketVersion` (protocol id + versión). Todo header sale con
`packet_version_local ()`, y al recibir se copia a `packet->version`, que es lo que valida `packet_check`. Es un
cambio de protocolo: el `PacketHeader` de pycerver se actualizó igual.

**Bugs que se encontraron en este camino:**
- con `check_packets`, la versión se esperaba al inicio de los datos (nadie la mandaba ahí) y la condición estaba
  invertida, así que se rechazaban todos los paquetes válidos;
- un paquete rechazado se fugaba (cerver, cliente y admin).

**Bad packets.** Cuentan como bad packet:
- uno que falla `packet_check`;
- uno de tipo desconocido;
- uno más grande que `max_received_packet_size`.

| Dónde | Qué pasa | Límite |
|---|---|---|
| Cerver (conexión autenticada) | `CERVER_ERROR_PACKET_ERROR` al emisor con el motivo; al llegar al límite, `connection_drop_request` | `cerver_set_max_bad_packets`, 16 por default, 0 sin límite |
| Admin | lo mismo | `admin_cerver_set_bad_packets_limit`, 4 por default |
| On hold | el error con el motivo; al llegar al límite se dropea | `cerver_set_on_hold_max_bad_packets`, 4 por default |
| Cliente | cuenta y, al llegar al límite, termina la conexión (sin avisarle al cerver) | `client_set_max_bad_packets`, 16 por default, 0 sin límite |
| Paquete demasiado grande | el error y el cierre inmediato: el stream no se puede resincronizar | — |

**Tests** (en `test/cerver/core.c`, salvo el cliente, que está en `test/client/lifecycle.c`):
- el límite con POLL y THREADS: el error llega en cada paquete y la conexión sigue funcionando por debajo del
  límite;
- el tamaño;
- on hold;
- admin;
- el cliente, por debajo y en el límite;
- `check_packets` rechaza otro `major`;
- la versión llega en cada header (`test/packets.c`).

### Admin y ejemplos

- **Admin cerver:**
  - los hilos de update e interval son joinables y se esperan en `admin_cerver_end`;
  - el interval duerme con `cerver_sleep`, que se despierta al detener el cerver;
  - los broadcasts recorren la lista de admins con su mutex.
- **Ejemplos:** los 26 de `examples/` ya no liberan nada en el handler de señal. `end ()` solo llama
  `cerver_shutdown`, y el teardown corre cuando `cerver_start` retorna.

### 2.0b-61

Cada test se vio fallar contra la 2.0b-60.

| Test | Dónde | Qué fija |
|---|---|---|
| `test_server_unanswered` | `test/http/server.c` | Un handler sin respuesta recibe 500 con `Connection: close`, se cuenta y aparece en `cerver_http_requests_unanswered_total` |
| `test_server_unanswered_gone` | `test/http/server.c` | Una respuesta a un cliente que ya se fue cuenta como respuesta: no hay un 500 después |
| `test_server_nonblocking` | `test/http/server.c` | Una respuesta, un archivo y un video en un socket no bloqueante lleno llegan completos y en orden |
| `test_server_nonblocking_timeout` | `test/http/server.c` | Un socket no bloqueante que nunca se vacía falla al vencer el `send_timeout`, no al instante |
| `test_server_zero_fd` | `test/http/server.c` (proceso hijo) | Un archivo y un video abiertos como fd 0 se envían (el video no se enviaba) |
| `test_receive_default_dirnames` | `test/http/receive.c` | Dos peticiones con el mismo fd en el mismo segundo tienen directorios distintos |
| `/features/unanswered` | par `web/features` y `test/nginx/run.sh` | El 500 por curl, y detrás de nginx (en lugar de un 502) |

**El test de señal era flaky desde la 2.0b-60** (`repeat.sh test/bin/http`, ~1 de cada 200 rondas en la línea base):
- `test_server_signal` mandaba un solo `SIGUSR1`, y si llegaba entre dos `accept ()` se perdía. Esa limitación ya
  está aceptada y `cerver_run` la cubre en los servicios.
- Ahora repite la señal hasta que el cerver se detiene: 1000 rondas limpias, y limpio con TSan.

### 2.0b-63, el port del core de la 1.8

Los tests del core vienen de `core` (cada uno se vio fallar allí contra la versión anterior de la 1.x):

| Test | Qué fija |
|---|---|
| `test/routing.c` | El balancer en proceso con dos servicios reales: round robin, handler id, tipo desconocido, tamaño inválido, 1 MB, un servicio que cae y vuelve, sin servicios |
| `test/socket.c` | Los envíos con deadline a sockets llenos y cerrados, `sendfile` en loop |
| `test/network.c` | `network_parse_address`, `network_resolve_address`, las IPv4 mapeadas |
| `test/client/features.c` | Estados, connect async, cola de envío, reconexión, `packet_init_*`, sessions, paquete grande |
| `test/client/files.c` | Archivos en los dos sentidos, uno inexistente y uno fuera de los paths (`../`) |
| `test/cerver/lobby.c` | Lobbies con jugadores conectados |
| `test/cerver/core.c` | Además: el teardown de handlers con su propio hilo, un evento lento en su hilo, los hilos detachable, un update con fps bajo, la gracia del drenado, paquetes sin handler (LSan), y el orden de `CERVER_EVENT_CLIENT_CONNECTED` (el data race del TSan del push de la 2.0b-62) |
| `test/client/lifecycle.c` | Además: el teardown durante el backoff de un connect, los handlers con su hilo, `client_connection_get_next_packet ()` con paquetes en pedazos y mayores que un datagrama |
| `test/fuzz/{receive,packet,client,file}.c` | El ensamblado de paquetes, el header, el stream que lee un cliente y el `FileHeader` |
| pares `cerver/queue`, `balancer/round_robin`, `balancer/handler_id` | El cliente con cola de envío y el balancer con dos servicios |

Del lado HTTP, `test_features_metrics` revisa las métricas del thpool y `test/performance.sh` baja a GET 2 / POST JSON 20
asignaciones.

### Pendiente

- **La línea base x86_64:** se llena con la salida de un `workflow_dispatch` de `scheduled.yml`.
- **Los workflows** no se corrieron en GitHub (no hay `act`).
