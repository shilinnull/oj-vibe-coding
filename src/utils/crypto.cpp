#include "utils/crypto.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/err.h>
 #include <openssl/buffer.h>

#include <vector>
#include <stdexcept>
#include <sstream>

namespace oj {

namespace {
std::string ToBase64(const unsigned char* data, std::size_t len) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    b64 = BIO_push(b64, mem);
    BIO_write(b64, data, static_cast<int>(len));
    BIO_flush(b64);
    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string out(bptr->data, bptr->length);
    BIO_free_all(b64);
    return out;
}

std::vector<unsigned char> FromBase64(const std::string& in) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new_mem_buf(in.data(), static_cast<int>(in.size()));
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    mem = BIO_push(b64, mem);
    std::vector<unsigned char> out(in.size());
    int n = BIO_read(mem, out.data(), static_cast<int>(out.size()));
    if (n <= 0) {
        BIO_free_all(mem);
        throw std::runtime_error("base64 decode failed");
    }
    out.resize(n);
    BIO_free_all(mem);
    return out;
}

std::string Base64UrlFromBase64(const std::string& s) {
    std::string out = s;
    for (auto& c : out) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    // remove padding
    while (!out.empty() && out.back() == '=') out.pop_back();
    return out;
}

std::string Base64FromBase64Url(const std::string& s) {
    std::string out = s;
    for (auto& c : out) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    // add padding
    size_t m = out.size() % 4;
    if (m) {
        out.append(4 - m, '=');
    }
    return out;
}

}

std::string Base64UrlEncode(const std::string& data) {
    return Base64UrlFromBase64(ToBase64(reinterpret_cast<const unsigned char*>(data.data()), data.size()));
}

std::optional<std::string> Base64UrlDecode(const std::string& b64url) {
    try {
        auto bs = FromBase64(Base64FromBase64Url(b64url));
        return std::string(reinterpret_cast<char*>(bs.data()), bs.size());
    } catch (...) {
        return std::nullopt;
    }
}

std::string GeneratePasswordHash(const std::string& password) {
    const int iterations = 100000;
    const int salt_len = 16;
    const int dk_len = 32;
    std::vector<unsigned char> salt(salt_len);
    if (RAND_bytes(salt.data(), salt_len) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }
    std::vector<unsigned char> dk(dk_len);
    if (!PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()), salt.data(), salt_len, iterations, EVP_sha256(), dk_len, dk.data())) {
        throw std::runtime_error("PBKDF2 failed");
    }
    std::string salt_b64 = ToBase64(salt.data(), salt.size());
    std::string dk_b64 = ToBase64(dk.data(), dk.size());
    // store as iterations:salt_b64:dk_b64
    std::ostringstream oss;
    oss << iterations << ":" << salt_b64 << ":" << dk_b64;
    return oss.str();
}

bool VerifyPassword(const std::string& password, const std::string& stored) {
    // parse
    size_t p1 = stored.find(':');
    if (p1 == std::string::npos) return false;
    size_t p2 = stored.find(':', p1 + 1);
    if (p2 == std::string::npos) return false;
    int iterations = std::stoi(stored.substr(0, p1));
    std::string salt_b64 = stored.substr(p1 + 1, p2 - (p1 + 1));
    std::string dk_b64 = stored.substr(p2 + 1);
    auto salt = FromBase64(salt_b64);
    auto dk = FromBase64(dk_b64);
    std::vector<unsigned char> out(dk.size());
    if (!PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()), salt.data(), static_cast<int>(salt.size()), iterations, EVP_sha256(), static_cast<int>(out.size()), out.data())) {
        return false;
    }
    return out == dk;
}

std::string HmacSha256Base64Url(const std::string& key, const std::string& data) {
    unsigned int len = EVP_MAX_MD_SIZE;
    std::vector<unsigned char> buf(len);
    if (!HMAC(EVP_sha256(), reinterpret_cast<const unsigned char*>(key.data()), static_cast<int>(key.size()), reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size()), buf.data(), &len)) {
        throw std::runtime_error("HMAC failed");
    }
    return Base64UrlFromBase64(ToBase64(buf.data(), len));
}

}  // namespace oj
