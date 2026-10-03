#include "net/secure.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <system_error>

namespace opense4::net::secure {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kProtocolName = "OpenSE4 Noise_NXpsk2_25519_XChaChaPoly_BLAKE2b v1";

// The keys both sides derive from the transcript and the secrets.
std::expected<SessionKeys, std::string> derive(std::span<const uint8_t> clientHello, std::span<const uint8_t> serverHello, const crypto::Key& ee,
                                               const crypto::Key& es, const crypto::Key& psk, bool client) {
    const std::array<uint8_t, 64> transcript = crypto::Hash({}, 64).add(kProtocolName).add(clientHello).add(serverHello).finish64();
    std::array<uint8_t, 64> chain = crypto::Hash(transcript, 64).add(ee).add(es).add(psk).finish64();
    SessionKeys k;
    const crypto::Key toHost = crypto::Hash(chain).add("client to host").finish32();
    const crypto::Key toClient = crypto::Hash(chain).add("host to client").finish32();
    k.sessionId = crypto::Hash(chain).add("session id").finish32();
    k.send = client ? toHost : toClient;
    k.receive = client ? toClient : toHost;
    crypto::wipe(chain.data(), chain.size());
    return k;
}

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

std::string hostId(std::string_view host, uint16_t port) { return std::format("{}:{}", lowerAscii(host), port); }

} // namespace

crypto::Key joinKey(std::string_view joinPasswordHash) {
    if (joinPasswordHash.empty()) return {};
    return crypto::Hash().add("OpenSE4 join password key v1").add(joinPasswordHash).finish32();
}

std::expected<SessionKeys, std::string> clientKeys(const crypto::KeyPair& ephemeral, const crypto::Key& hostEphemeral, const crypto::Key& hostKey,
                                                   const crypto::Key& psk, std::span<const uint8_t> clientHello, std::span<const uint8_t> serverHello) {
    crypto::Key ee{}, es{};
    const bool ok1 = crypto::agree(ee, ephemeral.secret, hostEphemeral);
    const bool ok2 = crypto::agree(es, ephemeral.secret, hostKey);
    if (!ok1 || !ok2) return std::unexpected(std::string("the host sent an unusable key"));
    auto k = derive(clientHello, serverHello, ee, es, psk, true);
    crypto::wipe(ee.data(), ee.size());
    crypto::wipe(es.data(), es.size());
    return k;
}

std::expected<SessionKeys, std::string> hostKeys(const crypto::KeyPair& ephemeral, const crypto::KeyPair& hostKey, const crypto::Key& clientEphemeral,
                                                 const crypto::Key& psk, std::span<const uint8_t> clientHello, std::span<const uint8_t> serverHello) {
    crypto::Key ee{}, es{};
    const bool ok1 = crypto::agree(ee, ephemeral.secret, clientEphemeral);
    const bool ok2 = crypto::agree(es, hostKey.secret, clientEphemeral);
    if (!ok1 || !ok2) return std::unexpected(std::string("the client sent an unusable key"));
    auto k = derive(clientHello, serverHello, ee, es, psk, false);
    crypto::wipe(ee.data(), ee.size());
    crypto::wipe(es.data(), es.size());
    return k;
}

crypto::Key loginDigest(const crypto::Key& sessionId, std::string_view role, std::string_view player) {
    return crypto::Hash().add("OpenSE4 login v1").add(sessionId).add(role).add(player).finish32();
}

// ---- Host keys ---------------------------------------------------------------------------------

std::expected<crypto::KeyPair, std::string> loadOrCreateHostKey(const fs::path& file) {
    std::error_code ec;
    if (fs::exists(file, ec)) {
        std::ifstream in(file, std::ios::binary);
        std::string line;
        while (std::getline(in, line)) {
            std::erase(line, '\r');
            if (line.empty() || line.front() == '#') continue;
            const auto secret = crypto::keyFromHex(line);
            crypto::wipe(line.data(), line.size());
            if (!secret) break;
            crypto::KeyPair k = crypto::keyPairFromSecret(*secret);
            return k;
        }
        return std::unexpected(std::format("{}: not a host key file (expected 64 hex digits)", file.string()));
    }
    const crypto::KeyPair k = crypto::newKeyPair();
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return std::unexpected(std::format("{}: cannot write the host key", tmp.string()));
        // Only the owner may read it (where the file system has permissions).
        fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
        std::string secret = crypto::hex(k.secret);
        out << "# OpenSE4 host key: the secret half of this machine's identity in network games. Keep it private.\n"
            << "# Fingerprint: " << crypto::fingerprint(k.publicKey) << "\n"
            << secret << "\n";
        crypto::wipe(secret.data(), secret.size());
        if (!out) return std::unexpected(std::format("{}: cannot write the host key", tmp.string()));
    }
    fs::rename(tmp, file, ec);
    if (ec) return std::unexpected(std::format("{}: cannot save the host key: {}", file.string(), ec.message()));
    return k;
}

// ---- Known hosts -------------------------------------------------------------------------------

std::vector<std::pair<std::string, crypto::Key>> KnownHosts::read() const {
    std::vector<std::pair<std::string, crypto::Key>> out;
    std::ifstream in(file_, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        std::erase(line, '\r');
        if (line.empty() || line.front() == '#') continue;
        const size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        if (auto key = crypto::keyFromHex(line.substr(space + 1))) out.emplace_back(lowerAscii(line.substr(0, space)), *key);
    }
    return out;
}

std::optional<crypto::Key> KnownHosts::find(std::string_view host, uint16_t port) const {
    const std::string id = hostId(host, port);
    for (const auto& [name, key] : read())
        if (name == id) return key;
    return std::nullopt;
}

std::expected<void, std::string> KnownHosts::remember(std::string_view host, uint16_t port, const crypto::Key& key) {
    const std::string id = hostId(host, port);
    auto entries = read();
    std::erase_if(entries, [&](const auto& e) { return e.first == id; });
    entries.emplace_back(id, key);
    std::error_code ec;
    if (file_.has_parent_path()) fs::create_directories(file_.parent_path(), ec);
    fs::path tmp = file_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return std::unexpected(std::format("{}: cannot write", tmp.string()));
        out << "# Host keys OpenSE4 has trusted: <address>:<port> <key>. Delete a line to trust that host anew.\n";
        for (const auto& [name, k] : entries) out << name << ' ' << crypto::hex(k) << '\n';
        if (!out) return std::unexpected(std::format("{}: cannot write", tmp.string()));
    }
    fs::rename(tmp, file_, ec);
    if (ec) return std::unexpected(std::format("{}: cannot save: {}", file_.string(), ec.message()));
    return {};
}

fs::path userDataDir() {
    if (const char* own = std::getenv("OPENSE4_USER_DIR"); own && *own) return fs::path(own);
#if defined(_WIN32)
    if (const char* appData = std::getenv("APPDATA"); appData && *appData) return fs::path(appData) / "OpenSE4";
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home && *home) return fs::path(home) / "Library" / "Application Support" / "OpenSE4";
#else
    if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data) return fs::path(data) / "OpenSE4";
    if (const char* home = std::getenv("HOME"); home && *home) return fs::path(home) / ".local" / "share" / "OpenSE4";
#endif
    std::error_code ec;
    return fs::current_path(ec) / "userdata";
}

} // namespace opense4::net::secure
