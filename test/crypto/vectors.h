#ifndef _CERVER_TESTS_CRYPTO_VECTORS_H_
#define _CERVER_TESTS_CRYPTO_VECTORS_H_

// the keys, the messages and the signatures of the examples of the RFC: each value is the text of its document

// the payload of the examples of RFC 7515 appendix A, in base64url
#define CRYPTO_VECTOR_PAYLOAD					\
	"eyJpc3MiOiJqb2UiLA0KICJleHAiOjEzMDA4MTkzODAsDQogImh0dHA6Ly9leGFt"		\
	"cGxlLmNvbS9pc19yb290Ijp0cnVlfQ"

#pragma region secret

// RFC 7515 appendix A.1: HMAC SHA-256
#define CRYPTO_VECTOR_SECRET_VALUE				\
	"AyM1SysPpbyDfgZld3umj1qzKObwVMkoqQ-EstJQLr_T-1qS0gZH75"				\
	"aKtMN3Yj0iPS4hcgUuTwjAzZr1Z9CAow"

#define CRYPTO_VECTOR_SECRET_BYTES				64

#define CRYPTO_VECTOR_SECRET_JWK				\
	"{\"kty\":\"oct\",\"k\":\"" CRYPTO_VECTOR_SECRET_VALUE "\"}"

#define CRYPTO_VECTOR_SECRET_MESSAGE			\
	"eyJ0eXAiOiJKV1QiLA0KICJhbGciOiJIUzI1NiJ9." CRYPTO_VECTOR_PAYLOAD

#define CRYPTO_VECTOR_SECRET_SIGNATURE			"dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"

#pragma endregion

#pragma region rsa

// RFC 7515 appendix A.2: RSASSA-PKCS1-v1_5 SHA-256
#define CRYPTO_VECTOR_RSA_MODULUS				\
	"ofgWCuLjybRlzo0tZWJjNiuSfb4p4fAkd_wWJcyQoTbji9k0l8W26mPddx"			\
	"HmfHQp-Vaw-4qPCJrcS2mJPMEzP1Pt0Bm4d4QlL-yRT-SFd2lZS-pCgNMs"			\
	"D1W_YpRPEwOWvG6b32690r2jZ47soMZo9wGzjb_7OMg0LOL-bSf63kpaSH"			\
	"SXndS5z5rexMdbBYUsLA9e-KXBdQOS-UTo7WTBEMa2R2CapHg665xsmtdV"			\
	"MTBQY4uDZlxvb3qCo5ZwKh9kG4LT6_I5IhlJH7aGhyxXFvUK-DWNmoudF8"			\
	"NAco9_h9iaGNj8q2ethFkMLs91kzk2PAcDTW9gb54h4FRWyuXpoQ"

#define CRYPTO_VECTOR_RSA_EXPONENT				"AQAB"

#define CRYPTO_VECTOR_RSA_PRIVATE_EXPONENT		\
	"Eq5xpGnNCivDflJsRQBXHx1hdR1k6Ulwe2JZD50LpXyWPEAeP88vLNO97I"			\
	"jlA7_GQ5sLKMgvfTeXZx9SE-7YwVol2NXOoAJe46sui395IW_GO-pWJ1O0"			\
	"BkTGoVEn2bKVRUCgu-GjBVaYLU6f3l9kJfFNS3E0QbVdxzubSu3Mkqzjkn"			\
	"439X0M_V51gfpRLI9JYanrC4D4qAdGcopV_0ZHHzQlBjudU2QvXt4ehNYT"			\
	"CBr6XCLQUShb1juUO1ZdiYoFaFQT5Tw8bGUl_x_jTj3ccPDVZFD9pIuhLh"			\
	"BOneufuBiB4cS98l2SR_RQyGWSeWjnczT0QU91p1DhOVRuOopznQ"

#define CRYPTO_VECTOR_RSA_FIRST_PRIME			\
	"4BzEEOtIpmVdVEZNCqS7baC4crd0pqnRH_5IB3jw3bcxGn6QLvnEtfdUdi"			\
	"YrqBdss1l58BQ3KhooKeQTa9AB0Hw_Py5PJdTJNPY8cQn7ouZ2KKDcmnPG"			\
	"BY5t7yLc1QlQ5xHdwW1VhvKn-nXqhJTBgIPgtldC-KDV5z-y2XDwGUc"

#define CRYPTO_VECTOR_RSA_SECOND_PRIME			\
	"uQPEfgmVtjL0Uyyx88GZFF1fOunH3-7cepKmtH4pxhtCoHqpWmT8YAmZxa"			\
	"ewHgHAjLYsp1ZSe7zFYHj7C6ul7TjeLQeZD_YwD66t62wDmpe_HlB-TnBA"			\
	"-njbglfIsRLtXlnDzQkv5dTltRJ11BKBBypeeF6689rjcJIDEz9RWdc"

#define CRYPTO_VECTOR_RSA_FIRST_EXPONENT		\
	"BwKfV3Akq5_MFZDFZCnW-wzl-CCo83WoZvnLQwCTeDv8uzluRSnm71I3Q"			\
	"CLdhrqE2e9YkxvuxdBfpT_PI7Yz-FOKnu1R6HsJeDCjn12Sk3vmAktV2zb"			\
	"34MCdy7cpdTh_YVr7tss2u6vneTwrA86rZtu5Mbr1C1XsmvkxHQAdYo0"

#define CRYPTO_VECTOR_RSA_SECOND_EXPONENT		\
	"h_96-mK1R_7glhsum81dZxjTnYynPbZpHziZjeeHcXYsXaaMwkOlODsWa"			\
	"7I9xXDoRwbKgB719rrmI2oKr6N3Do9U0ajaHF-NKJnwgjMd2w9cjz3_-ky"			\
	"NlxAr2v4IKhGNpmM5iIgOS1VZnOZ68m6_pbLBSp3nssTdlqvd0tIiTHU"

#define CRYPTO_VECTOR_RSA_COEFFICIENT			\
	"IYd7DHOhrWvxkwPQsRM2tOgrjbcrfvtQJipd-DlcxyVuuM9sQLdgjVk2o"			\
	"y26F0EmpScGLq2MowX7fhd_QJQ3ydy5cY7YIBi87w93IKLEdfnbJtoOPLU"			\
	"W0ITrJReOgo1cq9SbsxYawBgfp_gh6A5603k2-ZQwVK0JKSHuLFkuQ3U"

#define CRYPTO_VECTOR_RSA_BITS					2048

// the members of the public key and the ones that only the private key has
#define CRYPTO_VECTOR_RSA_PUBLIC_MEMBERS		\
	"\"kty\":\"RSA\","														\
	"\"n\":\"" CRYPTO_VECTOR_RSA_MODULUS "\","								\
	"\"e\":\"" CRYPTO_VECTOR_RSA_EXPONENT "\""

#define CRYPTO_VECTOR_RSA_FACTOR_MEMBERS		\
	"\"p\":\"" CRYPTO_VECTOR_RSA_FIRST_PRIME "\","							\
	"\"q\":\"" CRYPTO_VECTOR_RSA_SECOND_PRIME "\","							\
	"\"dp\":\"" CRYPTO_VECTOR_RSA_FIRST_EXPONENT "\","						\
	"\"dq\":\"" CRYPTO_VECTOR_RSA_SECOND_EXPONENT "\","						\
	"\"qi\":\"" CRYPTO_VECTOR_RSA_COEFFICIENT "\""

#define CRYPTO_VECTOR_RSA_PUBLIC_JWK			\
	"{" CRYPTO_VECTOR_RSA_PUBLIC_MEMBERS "}"

#define CRYPTO_VECTOR_RSA_PRIVATE_JWK			\
	"{" CRYPTO_VECTOR_RSA_PUBLIC_MEMBERS ","								\
	"\"d\":\"" CRYPTO_VECTOR_RSA_PRIVATE_EXPONENT "\","						\
	CRYPTO_VECTOR_RSA_FACTOR_MEMBERS "}"

#define CRYPTO_VECTOR_RSA_MESSAGE				\
	"eyJhbGciOiJSUzI1NiJ9." CRYPTO_VECTOR_PAYLOAD

#define CRYPTO_VECTOR_RSA_SIGNATURE				\
	"cC4hiUPoj9Eetdgtv3hF80EGrhuB__dzERat0XF9g2VtQgr9PJbu3XOiZj5RZmh7"		\
	"AAuHIm4Bh-0Qc_lF5YKt_O8W2Fp5jujGbds9uJdbF9CUAr7t1dnZcAcQjbKBYNX4"		\
	"BAynRFdiuB--f_nZLgrnbyTyWzO75vRK5h6xBArLIARNPvkSjtQBMHlb1L07Qe7K"		\
	"0GarZRmB_eSN9383LcOLn6_dO--xi12jzDwusC-eOkHWEsqtFZESc6BfI7noOPqv"		\
	"hJ1phCnvWh6IeYI2w9QOYEUipUTI8np6LbgGY9Fs98rqVt5AXLIhWkWywlVmtVrB"		\
	"p0igcN_IoypGlUPQGe77Rw"

#pragma endregion

#pragma region curves

// RFC 7515 appendix A.3: ECDSA P-256 SHA-256; the S of its signature is in the upper half of the order
#define CRYPTO_VECTOR_P256_X					"f83OJ3D2xF1Bg8vub9tLe1gHMzV76e8Tus9uPHvRVEU"
#define CRYPTO_VECTOR_P256_Y					"x_FEzRu9m36HLN_tue659LNpXW6pCyStikYjKIWI5a0"
#define CRYPTO_VECTOR_P256_SECRET				"jpsQnnGQmL-YBIffH1136cspYG6-0iY7X1fCE9-E9LI"

#define CRYPTO_VECTOR_P256_PUBLIC_MEMBERS		\
	"\"kty\":\"EC\",\"crv\":\"P-256\","										\
	"\"x\":\"" CRYPTO_VECTOR_P256_X "\","									\
	"\"y\":\"" CRYPTO_VECTOR_P256_Y "\""

#define CRYPTO_VECTOR_P256_PUBLIC_JWK			\
	"{" CRYPTO_VECTOR_P256_PUBLIC_MEMBERS "}"

#define CRYPTO_VECTOR_P256_PRIVATE_JWK			\
	"{" CRYPTO_VECTOR_P256_PUBLIC_MEMBERS ",\"d\":\"" CRYPTO_VECTOR_P256_SECRET "\"}"

#define CRYPTO_VECTOR_P256_MESSAGE				\
	"eyJhbGciOiJFUzI1NiJ9." CRYPTO_VECTOR_PAYLOAD

#define CRYPTO_VECTOR_P256_SIGNATURE			\
	"DtEhU3ljbEg8L38VWAfUAqOyKAM6-Xx-F4GawxaepmXFCgfTjDxw5djxLa8ISlSA"		\
	"pmWQxfKTUJqPP3-Kg6NU1Q"

// RFC 7515 appendix A.4: ECDSA P-521 SHA-512
#define CRYPTO_VECTOR_P521_X					\
	"AekpBQ8ST8a8VcfVOTNl353vSrDCLLJXmPk06wTjxrrjcBpXp5EOnYG_"				\
	"NjFZ6OvLFV1jSfS9tsz4qUxcWceqwQGk"

#define CRYPTO_VECTOR_P521_Y					\
	"ADSmRA43Z1DSNx_RvcLI87cdL07l6jQyyBXMoxVg_l2Th-x3S1WDhjDl"				\
	"y79ajL4Kkd0AZMaZmh9ubmf63e3kyMj2"

#define CRYPTO_VECTOR_P521_SECRET				\
	"AY5pb7A0UFiB3RELSD64fTLOSV_jazdF7fLYyuTw8lOfRhWg6Y6rUrPA"				\
	"xerEzgdRhajnu0ferB0d53vM9mE15j2C"

#define CRYPTO_VECTOR_P521_PUBLIC_MEMBERS		\
	"\"kty\":\"EC\",\"crv\":\"P-521\","										\
	"\"x\":\"" CRYPTO_VECTOR_P521_X "\","									\
	"\"y\":\"" CRYPTO_VECTOR_P521_Y "\""

#define CRYPTO_VECTOR_P521_PUBLIC_JWK			\
	"{" CRYPTO_VECTOR_P521_PUBLIC_MEMBERS "}"

#define CRYPTO_VECTOR_P521_PRIVATE_JWK			\
	"{" CRYPTO_VECTOR_P521_PUBLIC_MEMBERS ",\"d\":\"" CRYPTO_VECTOR_P521_SECRET "\"}"

#define CRYPTO_VECTOR_P521_MESSAGE				"eyJhbGciOiJFUzUxMiJ9.UGF5bG9hZA"

#define CRYPTO_VECTOR_P521_SIGNATURE			\
	"AdwMgeerwtHoh-l192l60hp9wAHZFVJbLfD_UxMi70cwnZOYaRI1bKPWROc-mZZq"		\
	"wqT2SI-KGDKB34XO0aw_7XdtAG8GaSwFKdCAPZgoXD2YBJZCPEX3xKpRwcdOO8Kp"		\
	"EHwJjyqOgzDO7iKvU8vcnwNrmxYbSW9ERBXukOXolLzeO_Jn"

#pragma endregion

#pragma region edwards

// RFC 8037 appendix A.1, A.2 and A.4: Ed25519, the key of the first test of RFC 8032 section 7.1
#define CRYPTO_VECTOR_ED25519_X					"11qYAYKxCrfVS_7TyWQHOg7hcvPapiMlrwIaaPcHURo"
#define CRYPTO_VECTOR_ED25519_SECRET			"nWGxne_9WmC6hEr0kuwsxERJxWl7MmkZcDusAxyuf2A"

#define CRYPTO_VECTOR_ED25519_PUBLIC_JWK		\
	"{\"kty\":\"OKP\",\"crv\":\"Ed25519\","									\
	"\"x\":\"" CRYPTO_VECTOR_ED25519_X "\"}"

#define CRYPTO_VECTOR_ED25519_PRIVATE_JWK		\
	"{\"kty\":\"OKP\",\"crv\":\"Ed25519\","									\
	"\"d\":\"" CRYPTO_VECTOR_ED25519_SECRET "\","							\
	"\"x\":\"" CRYPTO_VECTOR_ED25519_X "\"}"

#define CRYPTO_VECTOR_ED25519_MESSAGE			\
	"eyJhbGciOiJFZERTQSJ9.RXhhbXBsZSBvZiBFZDI1NTE5IHNpZ25pbmc"

#define CRYPTO_VECTOR_ED25519_SIGNATURE			\
	"hgyY0il_MGCjP0JzlnLWG1PPOt7-09PGcvMg3AIbQR6dWbhijcNR4ki4iylGjg5BhVsPt"	\
	"9g7sVvpAr_MuM0KAg"

#pragma endregion

#endif
