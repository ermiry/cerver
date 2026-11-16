#ifndef _CERVER_TESTS_JWT_VECTORS_H_
#define _CERVER_TESTS_JWT_VECTORS_H_

// the keys and the tokens of the examples of RFC 7520: each value is the text of the document

#pragma region keys

// section 3.2: the private key of P-521
#define JWT_VECTOR_CURVE_X						\
	"AHKZLLOsCOzz5cY97ewNUajB957y-C-U88c3v13nmGZx6sYl_oJXu9A5RkTKqjqv"		\
	"jyekWF-7ytDyRXYgCF5cj0Kt"

#define JWT_VECTOR_CURVE_Y						\
	"AdymlHvOiLxXkEhayXQnNCvDX4h9htZaCJN34kfmC6pV5OhQHiraVySsUdaQkAgD"		\
	"PrwQrJmbnX9cwlGfP-HqHZR1"

#define JWT_VECTOR_CURVE_SECRET					\
	"AAhRON2r9cqXX1hg-RoI6R1tX5p2rUAYdmpHZoC1XNM56KtscrX6zbKipQrCW9CG"		\
	"ZH3T4ubpnoTKLDYJ_fF3_rJt"

#define JWT_VECTOR_CURVE_PUBLIC_JWK				\
	"{\"kty\":\"EC\",\"kid\":\"bilbo.baggins@hobbiton.example\",\"use\":\"sig\",\"crv\":\"P-521\","	\
	"\"x\":\"" JWT_VECTOR_CURVE_X "\","										\
	"\"y\":\"" JWT_VECTOR_CURVE_Y "\"}"

#define JWT_VECTOR_CURVE_PRIVATE_JWK			\
	"{\"kty\":\"EC\",\"kid\":\"bilbo.baggins@hobbiton.example\",\"use\":\"sig\",\"crv\":\"P-521\","	\
	"\"x\":\"" JWT_VECTOR_CURVE_X "\","										\
	"\"y\":\"" JWT_VECTOR_CURVE_Y "\","										\
	"\"d\":\"" JWT_VECTOR_CURVE_SECRET "\"}"

// section 3.4: the private key of RSA
#define JWT_VECTOR_RSA_MODULUS					\
	"n4EPtAOCc9AlkeQHPzHStgAbgs7bTZLwUBZdR8_KuKPEHLd4rHVTeT-O-XV2jRoj"		\
	"dNhxJWTDvNd7nqQ0VEiZQHz_AJmSCpMaJMRBSFKrKb2wqVwGU_NsYOYL-QtiWN2l"		\
	"bzcEe6XC0dApr5ydQLrHqkHHig3RBordaZ6Aj-oBHqFEHYpPe7Tpe-OfVfHd1E6c"		\
	"S6M1FZcD1NNLYD5lFHpPI9bTwJlsde3uhGqC0ZCuEHg8lhzwOHrtIQbS0FVbb9k3"		\
	"-tVTU4fg_3L_vniUFAKwuCLqKnS2BYwdq_mzSnbLY7h_qixoR7jig3__kRhuaxwU"		\
	"kRz5iaiQkqgc5gHdrNP5zw"

#define JWT_VECTOR_RSA_EXPONENT					"AQAB"

#define JWT_VECTOR_RSA_PRIVATE_EXPONENT			\
	"bWUC9B-EFRIo8kpGfh0ZuyGPvMNKvYWNtB_ikiH9k20eT-O1q_I78eiZkpXxXQ0U"		\
	"TEs2LsNRS-8uJbvQ-A1irkwMSMkK1J3XTGgdrhCku9gRldY7sNA_AKZGh-Q661_4"		\
	"2rINLRCe8W-nZ34ui_qOfkLnK9QWDDqpaIsA-bMwWWSDFu2MUBYwkHTMEzLYGqOe"		\
	"04noqeq1hExBTHBOBdkMXiuFhUq1BU6l-DqEiWxqg82sXt2h-LMnT3046AOYJoRi"		\
	"oz75tSUQfGCshWTBnP5uDjd18kKhyv07lhfSJdrPdM5Plyl21hsFf4L_mHCuoFau"		\
	"7gdsPfHPxxjVOcOpBrQzwQ"

#define JWT_VECTOR_RSA_FIRST_PRIME				\
	"3Slxg_DwTXJcb6095RoXygQCAZ5RnAvZlno1yhHtnUex_fp7AZ_9nRaO7HX_-SFf"		\
	"GQeutao2TDjDAWU4Vupk8rw9JR0AzZ0N2fvuIAmr_WCsmGpeNqQnev1T7IyEsnh8"		\
	"UMt-n5CafhkikzhEsrmndH6LxOrvRJlsPp6Zv8bUq0k"

#define JWT_VECTOR_RSA_SECOND_PRIME				\
	"uKE2dh-cTf6ERF4k4e_jy78GfPYUIaUyoSSJuBzp3Cubk3OCqs6grT8bR_cu0Dm1"		\
	"MZwWmtdqDyI95HrUeq3MP15vMMON8lHTeZu2lmKvwqW7anV5UzhM1iZ7z4yMkuUw"		\
	"FWoBvyY898EXvRD-hdqRxHlSqAZ192zB3pVFJ0s7pFc"

#define JWT_VECTOR_RSA_FIRST_EXPONENT			\
	"B8PVvXkvJrj2L-GYQ7v3y9r6Kw5g9SahXBwsWUzp19TVlgI-YV85q1NIb1rxQtD-"		\
	"IsXXR3-TanevuRPRt5OBOdiMGQp8pbt26gljYfKU_E9xn-RULHz0-ed9E9gXLKD4"		\
	"VGngpz-PfQ_q29pk5xWHoJp009Qf1HvChixRX59ehik"

#define JWT_VECTOR_RSA_SECOND_EXPONENT			\
	"CLDmDGduhylc9o7r84rEUVn7pzQ6PF83Y-iBZx5NT-TpnOZKF1pErAMVeKzFEl41"		\
	"DlHHqqBLSM0W1sOFbwTxYWZDm6sI6og5iTbwQGIC3gnJKbi_7k_vJgGHwHxgPaX2"		\
	"PnvP-zyEkDERuf-ry4c_Z11Cq9AqC2yeL6kdKT1cYF8"

#define JWT_VECTOR_RSA_COEFFICIENT				\
	"3PiqvXQN0zwMeE-sBvZgi289XP9XCQF3VWqPzMKnIgQp7_Tugo6-NZBKCQsMf3Ha"		\
	"EGBjTVJs_jcK8-TRXvaKe-7ZMaQj8VfBdYkssbu0NKDDhjJ-GtiseaDVWt7dcH0c"		\
	"fwxgFUHpQh7FoCrjFJ6h6ZEpMF6xmujs4qMpPz8aaI4"

#define JWT_VECTOR_RSA_PUBLIC_MEMBERS			\
	"\"kty\":\"RSA\",\"kid\":\"bilbo.baggins@hobbiton.example\",\"use\":\"sig\","	\
	"\"n\":\"" JWT_VECTOR_RSA_MODULUS "\","									\
	"\"e\":\"" JWT_VECTOR_RSA_EXPONENT "\""

#define JWT_VECTOR_RSA_PUBLIC_JWK				\
	"{" JWT_VECTOR_RSA_PUBLIC_MEMBERS "}"

#define JWT_VECTOR_RSA_PRIVATE_JWK				\
	"{" JWT_VECTOR_RSA_PUBLIC_MEMBERS ","									\
	"\"d\":\"" JWT_VECTOR_RSA_PRIVATE_EXPONENT "\","						\
	"\"p\":\"" JWT_VECTOR_RSA_FIRST_PRIME "\","								\
	"\"q\":\"" JWT_VECTOR_RSA_SECOND_PRIME "\","							\
	"\"dp\":\"" JWT_VECTOR_RSA_FIRST_EXPONENT "\","							\
	"\"dq\":\"" JWT_VECTOR_RSA_SECOND_EXPONENT "\","						\
	"\"qi\":\"" JWT_VECTOR_RSA_COEFFICIENT "\"}"

// section 3.5: the symmetric key
#define JWT_VECTOR_SECRET_JWK					\
	"{\"kty\":\"oct\",\"kid\":\"018c0ae5-4d9b-471b-bfd6-eef314bc7037\",\"use\":\"sig\",\"alg\":\"HS256\","	\
	"\"k\":\"hJtXIZ2uSN5kbQfbtTNWbpdmhkV8FJG-Onbc6mxCcYg\"}"

#pragma endregion

#pragma region tokens

// the payload of every example, a text that is not the JSON of some claims
#define JWT_VECTOR_PAYLOAD						\
	"SXTigJlzIGEgZGFuZ2Vyb3VzIGJ1c2luZXNzLCBGcm9kbywgZ29pbmcgb3V0IHlv"		\
	"dXIgZG9vci4gWW91IHN0ZXAgb250byB0aGUgcm9hZCwgYW5kIGlmIHlvdSBkb24n"		\
	"dCBrZWVwIHlvdXIgZmVldCwgdGhlcmXigJlzIG5vIGtub3dpbmcgd2hlcmUgeW91"		\
	"IG1pZ2h0IGJlIHN3ZXB0IG9mZiB0by4"

// section 4.1: RSASSA-PKCS1-v1_5 with SHA-256
#define JWT_VECTOR_RS256_TOKEN					\
	"eyJhbGciOiJSUzI1NiIsImtpZCI6ImJpbGJvLmJhZ2dpbnNAaG9iYml0b24uZXhh"		\
	"bXBsZSJ9"																\
	"." JWT_VECTOR_PAYLOAD "."												\
	"MRjdkly7_-oTPTS3AXP41iQIGKa80A0ZmTuV5MEaHoxnW2e5CZ5NlKtainoFmKZo"		\
	"pdHM1O2U4mwzJdQx996ivp83xuglII7PNDi84wnB-BDkoBwA78185hX-Es4JIwmD"		\
	"LJK3lfWRa-XtL0RnltuYv746iYTh_qHRD68BNt1uSNCrUCTJDt5aAE6x8wW1Kt9e"		\
	"Ro4QPocSadnHXFxnt8Is9UzpERV0ePPQdLuW3IS_de3xyIrDaLGdjluPxUAhb6L2"		\
	"aXic1U12podGU0KLUQSE_oI-ZnmKJ3F4uOZDnd6QZWJushZ41Axf_fcIe8u9ipH8"		\
	"4ogoree7vjbU5y18kDquDg"

// section 4.2: RSASSA-PSS with SHA-384
#define JWT_VECTOR_PS384_TOKEN					\
	"eyJhbGciOiJQUzM4NCIsImtpZCI6ImJpbGJvLmJhZ2dpbnNAaG9iYml0b24uZXhh"		\
	"bXBsZSJ9"																\
	"." JWT_VECTOR_PAYLOAD "."												\
	"cu22eBqkYDKgIlTpzDXGvaFfz6WGoz7fUDcfT0kkOy42miAh2qyBzk1xEsnk2IpN"		\
	"6-tPid6VrklHkqsGqDqHCdP6O8TTB5dDDItllVo6_1OLPpcbUrhiUSMxbbXUvdvW"		\
	"Xzg-UD8biiReQFlfz28zGWVsdiNAUf8ZnyPEgVFn442ZdNqiVJRmBqrYRXe8P_ij"		\
	"Q7p8Vdz0TTrxUeT3lm8d9shnr2lfJT8ImUjvAA2Xez2Mlp8cBE5awDzT0qI0n6ui"		\
	"P1aCN_2_jLAeQTlqRHtfa64QQSUmFAAjVKPbByi7xho0uTOcbH510a6GYmJUAfmW"		\
	"jwZ6oD4ifKo8DYM-X72Eaw"

// section 4.3: ECDSA with P-521 and SHA-512
#define JWT_VECTOR_ES512_TOKEN					\
	"eyJhbGciOiJFUzUxMiIsImtpZCI6ImJpbGJvLmJhZ2dpbnNAaG9iYml0b24uZXhh"		\
	"bXBsZSJ9"																\
	"." JWT_VECTOR_PAYLOAD "."												\
	"AE_R_YZCChjn4791jSQCrdPZCNYqHXCTZH0-JZGYNlaAjP2kqaluUIIUnC9qvbu9"		\
	"Plon7KRTzoNEuT4Va2cmL1eJAQy3mtPBu_u_sDDyYjnAMDxXPn7XrT0lw-kvAD89"		\
	"0jl8e2puQens_IEKBpHABlsbEPX6sFY8OcGDqoRuBomu9xQ2"

// section 4.4: HMAC with SHA-256
#define JWT_VECTOR_HS256_TOKEN					\
	"eyJhbGciOiJIUzI1NiIsImtpZCI6IjAxOGMwYWU1LTRkOWItNDcxYi1iZmQ2LWVl"		\
	"ZjMxNGJjNzAzNyJ9"														\
	"." JWT_VECTOR_PAYLOAD "."												\
	"s0h6KThzkfBBBkLspW1h84VsJZFTsPPqMDA7g1Md7p0"

#pragma endregion

#endif
