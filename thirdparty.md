# Third-party code

The code of other projects that cerver includes, with its license. Each file keeps its copyright and permission
notice.

| Code | Files | Version | License | Changes |
|---|---|---|---|---|
| [http_parser](https://github.com/nodejs/http-parser) | `include/cerver/http/http_parser.h`, `src/cerver/http/http_parser.c` | 2.9 | MIT, Copyright Joyent, Inc. and other Node contributors | The methods of `cerver/http/method.h` |
| [Jansson](https://github.com/akheron/jansson) | `include/cerver/http/json/`, `src/cerver/http/json/` | 2.13 | MIT, Copyright (c) 2009-2016 Petri Lehtinen | The style of cerver; the hash seed from the entropy of the kernel; a memory limit for each parse; the numbers without the locale; its internal methods are not exported |
| [libjwt](https://github.com/benmcollins/libjwt) | `include/cerver/http/jwt/`, `src/cerver/http/jwt/` | 1.12 | MPL 2.0, Copyright (C) 2015-2018 Ben Collins | The style of cerver, OpenSSL 3 only; the signatures with the keys of `jwt/key.{h,c}` (cerver's own), the canonical base64url of `utils/base64.c`, the HMAC compared in constant time, the names of the algorithms of RFC 7518 |

The MIT license of each project asks that its copyright and permission notice is kept with the code; the MPL 2.0 of
libjwt applies to its files, `jwt.c` and `jwt.h`, and to the changes made to them.
