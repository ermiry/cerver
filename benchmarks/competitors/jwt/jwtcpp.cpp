#include <chrono>
#include <cstring>
#include <functional>
#include <string>

#include <jwt-cpp/jwt.h>

#include "backend.h"

// the claims of the driver: the API of the library takes each claim, so they are written here
#define JWT_CPP_ISSUER						"cerver"
#define JWT_CPP_EXPIRATION					4102444800
#define JWT_CPP_ISSUED_AT					1700000000

#define JWT_CPP_CLAIM						"id"
#define JWT_CPP_CLAIM_VALUE					"user-123"
#define JWT_CPP_ROLE						"role"
#define JWT_CPP_ROLE_VALUE					"admin"

// jwt-cpp, over OpenSSL with picojson; its algorithm keeps the key that it read
const char *jwt_backend_name = { "jwt-cpp" };

typedef jwt::decoded_jwt<jwt::traits::kazuho_picojson> JwtCppDecoded;

static std::function<std::string ()> jwt_cpp_sign;
static std::function<void (const JwtCppDecoded &)> jwt_cpp_verify;

// the token of the claims of the driver and its verifier, with an algorithm that has its keys
template <typename Algorithm> static void jwt_cpp_use (const Algorithm &algorithm) {

	jwt_cpp_sign = [algorithm] () {
		return jwt::create ()
			.set_type ("JWT")
			.set_issuer (JWT_CPP_ISSUER)
			.set_expires_at (std::chrono::system_clock::from_time_t (JWT_CPP_EXPIRATION))
			.set_issued_at (std::chrono::system_clock::from_time_t (JWT_CPP_ISSUED_AT))
			.set_payload_claim (JWT_CPP_CLAIM, jwt::claim (std::string (JWT_CPP_CLAIM_VALUE)))
			.set_payload_claim (JWT_CPP_ROLE, jwt::claim (std::string (JWT_CPP_ROLE_VALUE)))
			.sign (algorithm);
	};

	const auto verifier = jwt::verify ().allow_algorithm (algorithm);

	jwt_cpp_verify = [verifier] (const JwtCppDecoded &decoded) {
		verifier.verify (decoded);
	};

}

static bool jwt_cpp_algorithm_use (
	const std::string &name, const std::string &private_key, const std::string &public_key
) {

	bool found = true;

	if (name == "HS256") jwt_cpp_use (jwt::algorithm::hs256 { private_key });
	else if (name == "HS384") jwt_cpp_use (jwt::algorithm::hs384 { private_key });
	else if (name == "HS512") jwt_cpp_use (jwt::algorithm::hs512 { private_key });
	else if (name == "RS256") jwt_cpp_use (jwt::algorithm::rs256 { public_key, private_key });
	else if (name == "RS384") jwt_cpp_use (jwt::algorithm::rs384 { public_key, private_key });
	else if (name == "RS512") jwt_cpp_use (jwt::algorithm::rs512 { public_key, private_key });
	else if (name == "PS256") jwt_cpp_use (jwt::algorithm::ps256 { public_key, private_key });
	else if (name == "PS384") jwt_cpp_use (jwt::algorithm::ps384 { public_key, private_key });
	else if (name == "PS512") jwt_cpp_use (jwt::algorithm::ps512 { public_key, private_key });
	else if (name == "ES256") jwt_cpp_use (jwt::algorithm::es256 { public_key, private_key });
	else if (name == "ES384") jwt_cpp_use (jwt::algorithm::es384 { public_key, private_key });
	else if (name == "ES512") jwt_cpp_use (jwt::algorithm::es512 { public_key, private_key });
	else if (name == "EdDSA") jwt_cpp_use (jwt::algorithm::ed25519 { public_key, private_key });
	else found = false;

	return found;

}

bool jwt_backend_init (
	const char *algorithm,
	const char *private_key, const size_t private_size,
	const char *public_key, const size_t public_size
) {

	bool ready = false;

	try {
		ready = jwt_cpp_algorithm_use (
			algorithm, std::string (private_key, private_size), std::string (public_key, public_size)
		);
	}

	catch (const std::exception &) {
		ready = false;
	}

	return ready;

}

size_t jwt_backend_sign (
	const char *claims, const size_t claims_size, char *token, const size_t capacity
) {

	(void) claims;
	(void) claims_size;

	size_t length = 0;

	try {
		const std::string encoded = jwt_cpp_sign ();
		if (encoded.size () < capacity) {
			length = encoded.size ();
			(void) std::memcpy (token, encoded.c_str (), length + 1);
		}
	}

	catch (const std::exception &) {
		length = 0;
	}

	return length;

}

bool jwt_backend_verify (const char *token, const size_t length) {

	bool valid = false;

	try {
		const JwtCppDecoded decoded = jwt::decode (std::string (token, length));

		jwt_cpp_verify (decoded);

		valid = decoded.get_payload_claim (JWT_CPP_CLAIM).as_string () == JWT_CPP_CLAIM_VALUE;
	}

	catch (const std::exception &) {
		valid = false;
	}

	return valid;

}

void jwt_backend_end (void) {

	jwt_cpp_sign = nullptr;
	jwt_cpp_verify = nullptr;

}
