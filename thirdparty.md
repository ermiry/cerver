# Third-party code

The code of other projects that cerver includes, with its license. Each file keeps its copyright and permission
notice.

| Code | Files | Version | License | Changes |
|---|---|---|---|---|
| [sha-2](https://github.com/amosnier/sha-2) | `src/cerver/utils/sha256.c` | Imported in 2019 | Unlicense or Zero Clause BSD, Alain Mosnier | The SHA-256 of the session ids and of the lobbies, behind `cerver/utils/sha256.h` |
| [fastbase64](https://github.com/lemire/fastbase64) | `src/cerver/utils/base64.c` | Imported in 2020 | BSD 2-Clause, Copyright (c) 2015-2016, Wojciech Muła, Alfred Klomp, Daniel Lemire; the scalar code of [stringencoders](https://github.com/client9/stringencoders) (modp_b64), BSD, Copyright (c) 2005-2007, Nick Galbreath; the AVX2 code of [base64simd](https://github.com/WojciechMula/base64simd), BSD 2-Clause, Copyright (c) 2015-2018, Wojciech Muła, and of [base64](https://github.com/aklomp/base64), BSD 2-Clause, Copyright (c) 2013-2022, Alfred Klomp | The canonical base64url encode and decode of cerver (1.8.2), the AVX2 code only compiled with `__AVX2__` |

The BSD licenses of the base64 code ask that their copyright notices, their conditions and their disclaimer are kept
with the code:

```
Copyright (c) 2005-2007, Nick Galbreath
Copyright (c) 2013-2022, Alfred Klomp
Copyright (c) 2015-2018, Wojciech Muła
Copyright (c) 2015-2016, Wojciech Muła, Alfred Klomp, Daniel Lemire
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
