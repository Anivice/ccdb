// Encrypted CCDB notification envelopes. Public keys are one-line base64 SPKI DER.
#include "general_info_pulling.h"
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>

namespace {
using key_ptr = std::shared_ptr<EVP_PKEY>;
using ctx_ptr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using cipher_ptr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
using md_ptr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
void require(bool ok) { if (!ok) throw std::runtime_error("CCDB notification cryptography failed"); }
key_ptr adopt(EVP_PKEY* p) { require(p != nullptr); return {p, EVP_PKEY_free}; }
std::string b64(const std::string& bytes) {
    require(bytes.size() <= static_cast<size_t>(INT_MAX));
    std::string result(4 * ((bytes.size() + 2) / 3), '\0');
    EVP_EncodeBlock(reinterpret_cast<unsigned char*>(result.data()),
        reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<int>(bytes.size()));
    return result;
}
std::string unb64(const std::string& encoded) {
    require(!encoded.empty() && encoded.size() % 4 == 0 && encoded.size() <= 8 * 1024 * 1024);
    std::string result(3 * (encoded.size() / 4), '\0');
    const int n = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(result.data()),
        reinterpret_cast<const unsigned char*>(encoded.data()), static_cast<int>(encoded.size()));
    require(n >= 0);
    result.resize(static_cast<size_t>(n) - (encoded.back() == '=' ? 1 : 0) -
        (encoded.size() > 1 && encoded[encoded.size() - 2] == '=' ? 1 : 0));
    require(b64(result) == encoded);
    return result;
}
std::string public_line(EVP_PKEY* key) {
    const int n = i2d_PUBKEY(key, nullptr);
    require(n > 0 && n <= 1024);
    std::string der(static_cast<size_t>(n), '\0');
    auto* p = reinterpret_cast<unsigned char*>(der.data());
    require(i2d_PUBKEY(key, &p) == n);
    return b64(der);
}
key_ptr parse_public(const std::string& line) {
    const auto der = unb64(line);
    const auto* p = reinterpret_cast<const unsigned char*>(der.data());
    const auto* end = p + der.size();
    auto key = adopt(d2i_PUBKEY(nullptr, &p, static_cast<long>(der.size())));
    require(p == end && EVP_PKEY_base_id(key.get()) == EVP_PKEY_RSA && EVP_PKEY_get_bits(key.get()) == 4096);
    require(public_line(key.get()) == line);
    return key;
}
std::string random_bytes(size_t n) {
    std::string s(n, '\0');
    require(RAND_bytes(reinterpret_cast<unsigned char*>(s.data()), static_cast<int>(n)) == 1);
    return s;
}
ctx_ptr oaep_ctx(EVP_PKEY* key, bool encrypt) {
    ctx_ptr ctx(EVP_PKEY_CTX_new(key, nullptr), EVP_PKEY_CTX_free);
    require(ctx != nullptr && (encrypt ? EVP_PKEY_encrypt_init(ctx.get()) : EVP_PKEY_decrypt_init(ctx.get())) > 0);
    require(EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_OAEP_PADDING) > 0);
    require(EVP_PKEY_CTX_set_rsa_oaep_md(ctx.get(), EVP_sha256()) > 0);
    require(EVP_PKEY_CTX_set_rsa_mgf1_md(ctx.get(), EVP_sha256()) > 0);
    return ctx;
}
std::string wrap(EVP_PKEY* key, const std::string& data) {
    auto ctx = oaep_ctx(key, true);
    size_t n = 0;
    require(EVP_PKEY_encrypt(ctx.get(), nullptr, &n,
        reinterpret_cast<const unsigned char*>(data.data()), data.size()) > 0);
    std::string out(n, '\0');
    require(EVP_PKEY_encrypt(ctx.get(), reinterpret_cast<unsigned char*>(out.data()), &n,
        reinterpret_cast<const unsigned char*>(data.data()), data.size()) > 0);
    out.resize(n); return out;
}
std::string unwrap(EVP_PKEY* key, const std::string& data) {
    auto ctx = oaep_ctx(key, false);
    size_t n = 0;
    require(EVP_PKEY_decrypt(ctx.get(), nullptr, &n,
        reinterpret_cast<const unsigned char*>(data.data()), data.size()) > 0);
    std::string out(n, '\0');
    require(EVP_PKEY_decrypt(ctx.get(), reinterpret_cast<unsigned char*>(out.data()), &n,
        reinterpret_cast<const unsigned char*>(data.data()), data.size()) > 0);
    out.resize(n); return out;
}
std::string sign(EVP_PKEY* key, const std::string& data) {
    md_ptr ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    EVP_PKEY_CTX* pctx = nullptr;
    require(ctx && EVP_DigestSignInit(ctx.get(), &pctx, EVP_sha256(), nullptr, key) > 0);
    require(EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) > 0);
    require(EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, RSA_PSS_SALTLEN_DIGEST) > 0);
    require(EVP_DigestSignUpdate(ctx.get(), data.data(), data.size()) > 0);
    size_t n = 0;
    require(EVP_DigestSignFinal(ctx.get(), nullptr, &n) > 0);
    std::string sig(n, '\0');
    require(EVP_DigestSignFinal(ctx.get(), reinterpret_cast<unsigned char*>(sig.data()), &n) > 0);
    sig.resize(n); return sig;
}
void verify(EVP_PKEY* key, const std::string& data, const std::string& sig) {
    md_ptr ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    EVP_PKEY_CTX* pctx = nullptr;
    require(ctx && EVP_DigestVerifyInit(ctx.get(), &pctx, EVP_sha256(), nullptr, key) > 0);
    require(EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) > 0);
    require(EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, RSA_PSS_SALTLEN_DIGEST) > 0);
    require(EVP_DigestVerifyUpdate(ctx.get(), data.data(), data.size()) > 0);
    require(EVP_DigestVerifyFinal(ctx.get(), reinterpret_cast<const unsigned char*>(sig.data()), sig.size()) == 1);
}
std::pair<std::string, std::string> seal(const std::string& plain, const std::string& key, const std::string& nonce) {
    require(plain.size() <= INT_MAX);
    cipher_ptr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    require(ctx && EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr,
        reinterpret_cast<const unsigned char*>(key.data()), reinterpret_cast<const unsigned char*>(nonce.data())) == 1);
    std::string cipher(plain.size() + 16, '\0'); int n = 0, extra = 0;
    require(EVP_EncryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(cipher.data()), &n,
        reinterpret_cast<const unsigned char*>(plain.data()), static_cast<int>(plain.size())) == 1);
    require(EVP_EncryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(cipher.data()) + n, &extra) == 1);
    cipher.resize(n + extra);
    std::string tag(16, '\0');
    require(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, 16, tag.data()) == 1);
    return {cipher, tag};
}
std::string open_seal(const std::string& cipher, const std::string& key,
    const std::string& nonce, const std::string& tag) {
    require(key.size() == 32 && nonce.size() == 12 && tag.size() == 16 && cipher.size() <= INT_MAX);
    cipher_ptr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    require(ctx && EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr,
        reinterpret_cast<const unsigned char*>(key.data()), reinterpret_cast<const unsigned char*>(nonce.data())) == 1);
    std::string plain(cipher.size() + 16, '\0'); int n = 0, extra = 0;
    require(EVP_DecryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(plain.data()), &n,
        reinterpret_cast<const unsigned char*>(cipher.data()), static_cast<int>(cipher.size())) == 1);
    require(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, 16, const_cast<char*>(tag.data())) == 1);
    require(EVP_DecryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(plain.data()) + n, &extra) == 1);
    plain.resize(n + extra); return plain;
}
} // namespace

void general_info_pulling::load_or_create_client_keys() {
    namespace fs = std::filesystem;
    const char* home = std::getenv("HOME");
    require(home && *home);
    const fs::path dir = fs::path(home) / ".config" / "ccdb";
    fs::create_directories(dir);
    client_config_dir_ = dir.string();
    const auto priv = dir / "client_private_key", pub = dir / "client_public_key";
    require(fs::exists(priv) == fs::exists(pub)); // Partial key pairs must be repaired explicitly.
    if (fs::exists(priv)) {
        require((fs::status(priv).permissions() & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
        FILE* f = std::fopen(priv.c_str(), "rb"); require(f != nullptr);
        EVP_PKEY* p = PEM_read_PrivateKey(f, nullptr, nullptr, nullptr);
        std::fclose(f);
        client_private_key_ = adopt(p);
        require(EVP_PKEY_base_id(p) == EVP_PKEY_RSA && EVP_PKEY_get_bits(p) == 4096);
        std::ifstream in(pub); require(in.good());
        std::getline(in, client_public_key_);
        require(client_public_key_ == public_line(p));
        return;
    }
    ctx_ptr ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
    require(ctx && EVP_PKEY_keygen_init(ctx.get()) > 0 && EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(), 4096) > 0);
    EVP_PKEY* p = nullptr;
    require(EVP_PKEY_keygen(ctx.get(), &p) > 0);
    client_private_key_ = adopt(p);
    client_public_key_ = public_line(p);
    const int fd = ::open(priv.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    require(fd >= 0);
    FILE* f = ::fdopen(fd, "wb");
    if (!f) { ::close(fd); ::unlink(priv.c_str()); require(false); }
    const bool written = PEM_write_PrivateKey(f, p, nullptr, nullptr, 0, nullptr, nullptr) == 1;
    const bool closed = std::fclose(f) == 0;
    if (!written || !closed) { ::unlink(priv.c_str()); require(false); }
    const int pubfd = ::open(pub.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
    require(pubfd >= 0);
    const std::string line = client_public_key_ + "\n";
    const bool pubwritten = ::write(pubfd, line.data(), line.size()) == static_cast<ssize_t>(line.size());
    ::close(pubfd);
    if (!pubwritten) { ::unlink(pub.c_str()); require(false); }
}

std::unordered_map<std::string, std::shared_ptr<evp_pkey_st>> general_info_pulling::acceptable_clients() const {
    std::unordered_map<std::string, std::shared_ptr<evp_pkey_st>> result;
    std::ifstream input(std::filesystem::path(client_config_dir_) / "acceptable_clients");
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        result.emplace(line, parse_public(line));
    }
    return result;
}

void general_info_pulling::broadcast(const nlohmann::json& message)
{
    const auto allowed = acceptable_clients();
    nlohmann::json recipients = nlohmann::json::object();
    std::string secret = random_bytes(32);
    {
        std::lock_guard lock(peer_mtx_);
        for (const auto& [id, peer] : peers_) {
            if (const auto it = allowed.find(peer.public_key); it != allowed.end())
                recipients[peer.public_key] = b64(wrap(it->second.get(), secret));
        }
    }
    if (recipients.empty()) return;
    const auto nonce = random_bytes(12);
    const auto [cipher, tag] = seal(message.dump(), secret, nonce);
    nlohmann::json envelope = {
        {"encrypted", true}, {"sender_public_key", client_public_key_},
        {"content", b64(cipher)}, {"nonce", b64(nonce)}, {"tag", b64(tag)},
        {"keys", recipients},
        {"timestamp", std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    envelope["signature"] = b64(sign(client_private_key_.get(), envelope.dump()));
    send_raw_envelope(envelope);
}

nlohmann::json general_info_pulling::decrypt_notification(const nlohmann::json& envelope)
{
    require(envelope.at("encrypted").get<bool>());
    const auto sender = envelope.at("sender_public_key").get<std::string>();
    const auto allowed = acceptable_clients();
    const auto it = allowed.find(sender);
    require(it != allowed.end());
    const auto sig = unb64(envelope.at("signature").get<std::string>());
    auto signed_part = envelope;
    signed_part.erase("signature");
    verify(it->second.get(), signed_part.dump(), sig);
    const auto timestamp = envelope.at("timestamp").get<std::int64_t>();
    const auto now_wall = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    require(timestamp >= now_wall - 120 && timestamp <= now_wall + 120);
    const auto& keys = envelope.at("keys");
    require(keys.contains(client_public_key_));
    const auto secret = unwrap(client_private_key_.get(), unb64(keys.at(client_public_key_).get<std::string>()));
    const auto plain = open_seal(unb64(envelope.at("content").get<std::string>()), secret,
        unb64(envelope.at("nonce").get<std::string>()), unb64(envelope.at("tag").get<std::string>()));
    auto result = nlohmann::json::parse(plain);
    {
        std::lock_guard lock(notification_replay_mtx_);
        const auto now = std::chrono::steady_clock::now();
        for (auto it2 = seen_signatures_.begin(); it2 != seen_signatures_.end();) {
            if (now - it2->second > std::chrono::seconds(240)) it2 = seen_signatures_.erase(it2);
            else ++it2;
        }
        require(seen_signatures_.emplace(sender + sig, now).second);
    }
    return result;
}
