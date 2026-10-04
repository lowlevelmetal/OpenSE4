#include "net/secure.hpp"

#include "core/environment.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

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
std::string gameKeyId(uint64_t gameId) { return std::format("pbem-game:{:016x}", gameId); }

} // namespace

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

namespace {

// The permissions of an existing key file: other users of the computer must
// not read it (POSIX; Windows keeps a user's files apart by itself).
std::string permissionWarning(const fs::path& file) {
#ifdef _WIN32
    (void)file;
    return {};
#else
    std::error_code ec;
    const fs::perms p = fs::status(file, ec).permissions();
    if (ec || (p & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none) return {};
    return std::format("Warning: {} can be read by other users of this computer (mode {:o}), and whoever reads it can pose as this host. "
                       "Make it private (chmod 600).",
                       file.string(), static_cast<unsigned>(p) & 0777u);
#endif
}

std::expected<HostKeyFile, std::string> readHostKey(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("{}: cannot read the host key", file.string()));
    std::string line;
    while (std::getline(in, line)) {
        std::erase(line, '\r');
        if (line.empty() || line.front() == '#') continue;
        auto secret = crypto::keyFromHex(line);
        crypto::wipe(line.data(), line.size());
        if (!secret) break;
        HostKeyFile out{hostIdentity(*secret), permissionWarning(file)};
        crypto::wipe(secret->data(), secret->size());
        return out;
    }
    return std::unexpected(std::format("{}: not a host key file (expected 64 hex digits)", file.string()));
}

// Creates `file` for writing only if it does not exist, readable by the owner
// only. -1: it exists (or cannot be made).
int createExclusive(const fs::path& file) {
#ifdef _WIN32
    int fd = -1;
    if (_wsopen_s(&fd, file.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _SH_DENYRW, _S_IREAD | _S_IWRITE) != 0) return -1;
    return fd;
#else
    return ::open(file.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
#endif
}

bool writeAll(int fd, std::string_view text) {
    while (!text.empty()) {
#ifdef _WIN32
        const int n = _write(fd, text.data(), static_cast<unsigned>(text.size()));
#else
        const auto n = ::write(fd, text.data(), text.size());
#endif
        if (n <= 0) return false;
        text.remove_prefix(static_cast<size_t>(n));
    }
    return true;
}

void closeFile(int fd) {
#ifdef _WIN32
    _close(fd);
#else
    ::close(fd);
#endif
}

} // namespace

HostIdentity hostIdentity(const crypto::Key& secret) {
    crypto::Key network = crypto::Hash(secret).add("OpenSE4 host key: network handshake").finish32();
    crypto::Key box = crypto::Hash(secret).add("OpenSE4 host key: PBEM box").finish32();
    crypto::Key signing = crypto::Hash(secret).add("OpenSE4 host key: PBEM signing").finish32();
    HostIdentity id;
    id.network = crypto::keyPairFromSecret(network);
    id.pbem.box = crypto::keyPairFromSecret(box);
    id.pbem.signing = crypto::signingKey(signing);
    crypto::wipe(network.data(), network.size());
    crypto::wipe(box.data(), box.size());
    crypto::wipe(signing.data(), signing.size());
    return id;
}

std::expected<HostKeyFile, std::string> loadOrCreateHostKey(const fs::path& file) {
    std::error_code ec;
    if (fs::exists(file, ec)) return readHostKey(file);
    if (file.has_parent_path() && !fs::exists(file.parent_path(), ec)) {
        fs::create_directories(file.parent_path(), ec);
#ifndef _WIN32
        fs::permissions(file.parent_path(), fs::perms::owner_all, fs::perm_options::replace, ec);
#endif
    }
    const int fd = createExclusive(file);
    if (fd < 0) {
        // Made by another program meanwhile: that one counts.
        if (fs::exists(file, ec)) return readHostKey(file);
        return std::unexpected(std::format("{}: cannot create the host key", file.string()));
    }
    crypto::Key secret{};
    crypto::randomBytes(secret);
    HostKeyFile out{hostIdentity(secret), {}};
    std::string text = std::format("# OpenSE4 host key: the secret of this machine's identity in network and play-by-e-mail games. Keep it "
                                   "private.\n# Network fingerprint: {}\n# Play-by-e-mail fingerprint: {}\n{}\n",
                                   crypto::fingerprint(out.keys.network.publicKey), crypto::fingerprint(out.keys.pbem.signing.publicKey),
                                   crypto::hex(secret));
    crypto::wipe(secret.data(), secret.size());
    const bool ok = writeAll(fd, text);
    crypto::wipe(text.data(), text.size());
    closeFile(fd);
    if (!ok) {
        fs::remove(file, ec);
        return std::unexpected(std::format("{}: cannot write the host key", file.string()));
    }
    return out;
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

std::optional<crypto::Key> KnownHosts::findId(const std::string& id) const {
    for (const auto& [name, key] : read())
        if (name == id) return key;
    return std::nullopt;
}

std::optional<crypto::Key> KnownHosts::find(std::string_view host, uint16_t port) const { return findId(hostId(host, port)); }

std::expected<void, std::string> KnownHosts::remember(std::string_view host, uint16_t port, const crypto::Key& key) {
    return rememberId(hostId(host, port), key);
}

std::optional<crypto::Key> KnownHosts::findGame(uint64_t gameId) const { return findId(gameKeyId(gameId)); }

std::expected<void, std::string> KnownHosts::rememberGame(uint64_t gameId, const crypto::Key& key) { return rememberId(gameKeyId(gameId), key); }

std::expected<void, std::string> KnownHosts::rememberId(const std::string& id, const crypto::Key& key) {
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
        out << "# Host keys OpenSE4 has trusted: <address>:<port> <key>, or pbem-game:<game id> <key> for the host of a play-by-e-mail "
               "game. Delete a line to trust that host anew.\n";
        for (const auto& [name, k] : entries) out << name << ' ' << crypto::hex(k) << '\n';
        if (!out) return std::unexpected(std::format("{}: cannot write", tmp.string()));
    }
    fs::rename(tmp, file_, ec);
    if (ec) return std::unexpected(std::format("{}: cannot save: {}", file_.string(), ec.message()));
    return {};
}

fs::path userDataDir() {
    // The variables as UTF-8 (core/environment.hpp), which every narrow path is.
    auto variable = [](const char* name) { return core::environment(name).value_or(std::string()); };
    if (const std::string own = variable("OPENSE4_USER_DIR"); !own.empty()) return fs::path(own);
#if defined(_WIN32)
    if (const std::string appData = variable("APPDATA"); !appData.empty()) return fs::path(appData) / "OpenSE4";
#elif defined(__APPLE__)
    if (const std::string home = variable("HOME"); !home.empty()) return fs::path(home) / "Library" / "Application Support" / "OpenSE4";
#else
    if (const std::string data = variable("XDG_DATA_HOME"); !data.empty()) return fs::path(data) / "OpenSE4";
    if (const std::string home = variable("HOME"); !home.empty()) return fs::path(home) / ".local" / "share" / "OpenSE4";
#endif
    std::error_code ec;
    return fs::current_path(ec) / "userdata";
}

} // namespace opense4::net::secure
