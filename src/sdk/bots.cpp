#include "sdk/bots.hpp"

#include "core/log.hpp"
#include "net/crypto.hpp"
#include "net/socket.hpp"
#include "script/json.hpp"
#include "sdk/view.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <format>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

namespace opense4::sdk {

namespace {

using Clock = std::chrono::steady_clock;
using script::Value;
using script::ValueMap;

Value object(std::initializer_list<std::pair<std::string, Value>> entries) { return Value(ValueMap(entries)); }

std::string line(const Value& v) {
    auto text = script::toJson(v);
    return (text ? std::move(*text) : std::string("{}")) + "\n";
}

bool sameToken(std::string_view a, std::string_view b) {
    // Compares every byte whatever the first difference, so the time taken says nothing.
    unsigned diff = a.size() == b.size() ? 0u : 1u;
    const size_t n = std::max(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const unsigned char x = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
        const unsigned char y = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
        diff |= static_cast<unsigned>(x ^ y);
    }
    return diff == 0;
}

int pollMs(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return static_cast<int>(std::clamp<int64_t>(left, 0, 1000));
}

// A connection read and written a line at a time.
struct Link {
    enum class Status : uint8_t { Line, Timeout, Closed, Error, TooLong };
    struct Read {
        Status status = Status::Line;
        std::string text;   // the line, or what went wrong
    };

    net::Socket socket;
    std::string buffer;   // received, not yet taken as lines
    size_t maxLine = size_t{64} << 20;

    // The next line, without its end; blank lines are skipped.
    Read readLine(Clock::time_point deadline) {
        std::array<uint8_t, 65536> chunk{};
        size_t searched = 0;
        for (;;) {
            if (const size_t nl = buffer.find('\n', searched); nl != std::string::npos) {
                std::string text = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                searched = 0;
                if (!text.empty() && text.back() == '\r') text.pop_back();
                if (text.find_first_not_of(" \t") == std::string::npos) continue;
                return {Status::Line, std::move(text)};
            }
            searched = buffer.size();
            if (buffer.size() > maxLine) return {Status::TooLong, std::format("a message longer than {} bytes", maxLine)};
            const net::IoResult r = socket.receive(chunk);
            if (r.status == net::IoStatus::Ok) {
                buffer.append(reinterpret_cast<const char*>(chunk.data()), r.bytes);
                continue;
            }
            if (r.status == net::IoStatus::Closed) return {Status::Closed, "the connection was closed"};
            if (r.status == net::IoStatus::Error) return {Status::Error, r.error};
            if (Clock::now() >= deadline) return {Status::Timeout, {}};
            net::PollItem item;
            item.socket = socket.native();
            item.wantRead = true;
            net::pollSockets(std::span<net::PollItem>(&item, 1), pollMs(deadline));
        }
    }

    // Takes whatever has arrived, without waiting; false when the connection ended.
    bool takeAvailable() {
        std::array<uint8_t, 4096> chunk{};
        for (;;) {
            const net::IoResult r = socket.receive(chunk);
            if (r.status == net::IoStatus::Ok) {
                buffer.append(reinterpret_cast<const char*>(chunk.data()), r.bytes);
                if (buffer.size() > maxLine) return false;
                continue;
            }
            return r.status == net::IoStatus::WouldBlock;
        }
    }

    std::expected<void, std::string> write(std::string_view text, Clock::time_point deadline) {
        size_t sent = 0;
        while (sent < text.size()) {
            const net::IoResult r = socket.send(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()) + sent, text.size() - sent));
            if (r.status == net::IoStatus::Ok) {
                sent += r.bytes;
                continue;
            }
            if (r.status != net::IoStatus::WouldBlock) return std::unexpected(r.error.empty() ? std::string("the connection was closed") : r.error);
            if (Clock::now() >= deadline) return std::unexpected(std::string("the bot did not take the message in time"));
            net::PollItem item;
            item.socket = socket.native();
            item.wantWrite = true;
            net::pollSockets(std::span<net::PollItem>(&item, 1), pollMs(deadline));
        }
        return {};
    }
};

// A connected bot.
struct Bot {
    Link link;
    uint32_t slot = 0;
    std::string name;
    std::string peer;
    std::mutex busy;              // held while a request goes on, or while saying welcome or goodbye
    std::atomic<bool> gone{false};
    uint64_t nextId = 1;
};

// "ValueError: vehicle: no such vehicle" -> its type and message, as the session writes a refused service.
std::pair<std::string, std::string> splitError(const std::string& text) {
    const size_t colon = text.find(": ");
    if (colon != std::string::npos && colon > 0 && colon < 40) {
        const std::string_view type(text.data(), colon);
        const bool word = std::all_of(type.begin(), type.end(), [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        });
        if (word && type.front() >= 'A' && type.front() <= 'Z') return {std::string(type), text.substr(colon + 2)};
    }
    return {"RuntimeError", text};
}

} // namespace

class SlotBot;

struct BotHost::Impl {
    BotHostOptions options;
    net::Socket listener;
    uint16_t port = 0;
    std::atomic<int64_t> timeoutMs{60'000};
    std::atomic<bool> stopping{false};
    mutable std::mutex mutex;
    mutable std::condition_variable changed;
    std::map<uint32_t, std::shared_ptr<Bot>> bots;
    std::map<uint32_t, std::unique_ptr<SlotBot>> proxies;
    std::thread thread;

    void report(const std::string& text) const {
        if (options.report) options.report(text);
        else log::info("{}", text);
    }

    std::shared_ptr<Bot> botOf(uint32_t slot) const {
        std::lock_guard lock(mutex);
        auto it = bots.find(slot);
        return it == bots.end() || it->second->gone ? nullptr : it->second;
    }

    // Ends a bot's connection (the caller holds its `busy`).
    void drop(const std::shared_ptr<Bot>& b, const std::string& why) {
        {
            std::lock_guard lock(mutex);
            auto it = bots.find(b->slot);
            if (it != bots.end() && it->second == b) bots.erase(it);
        }
        if (!b->gone.exchange(true)) {
            b->link.socket.close();
            report(std::format("Bots: {} (slot {}) disconnected: {}", b->name.empty() ? std::string("a bot") : b->name, b->slot, why));
        }
        changed.notify_all();
    }

    void acceptLoop();
    void hello(net::Socket socket, std::string buffer, const std::string& peer, const std::string& text);
};

// What the sessions ask for a slot: its connected bot.
class SlotBot final : public ExternalBot {
public:
    SlotBot(BotHost::Impl& host, uint32_t slot) : host_(host), slot_(slot) {}

    std::expected<Value, std::string> request(const Value& request, const ServiceCall& services) override {
        std::shared_ptr<Bot> b = host_.botOf(slot_);
        if (!b) return std::unexpected(std::format("no bot is connected to external slot {}", slot_));
        std::unique_lock lock(b->busy);
        if (b->gone) return std::unexpected(std::format("the bot of external slot {} has disconnected", slot_));
        const uint64_t id = b->nextId++;
        const auto limit = std::chrono::milliseconds(host_.timeoutMs.load());
        auto deadline = Clock::now() + limit;
        if (auto w = b->link.write(line(object({{"request", request}, {"id", Value(static_cast<int64_t>(id))}})), deadline); !w) {
            host_.drop(b, w.error());
            return std::unexpected("the request could not be sent: " + w.error());
        }
        for (;;) {
            Link::Read r = b->link.readLine(deadline);
            switch (r.status) {
                case Link::Status::Line: break;
                case Link::Status::Timeout:
                    return std::unexpected(std::format("the bot gave no answer within {} ms", limit.count()));
                case Link::Status::Closed:
                    host_.drop(b, "it closed the connection");
                    return std::unexpected(std::string("the bot closed the connection during the request"));
                case Link::Status::Error:
                case Link::Status::TooLong:
                    host_.drop(b, r.text);
                    return std::unexpected("the connection to the bot failed: " + r.text);
            }
            auto parsed = script::parseJson(r.text);
            if (!parsed) return std::unexpected("the bot's message is not valid JSON: " + parsed.error().describe());
            const Value& msg = *parsed;
            if (!msg.isMap() || msg.asMap().empty())
                return std::unexpected(std::string("the bot's message is not a map naming its kind ({\"response\": ...})"));
            // A message about an earlier request (one that ran out of time) is dropped.
            if (const Value* mid = msg.find("id"); mid && !(mid->isInt() && mid->asInt() == static_cast<int64_t>(id))) continue;
            const std::string& kind = msg.asMap().front().first == "id" && msg.asMap().size() > 1 ? msg.asMap()[1].first : msg.asMap().front().first;
            const Value& body = *msg.find(kind);
            if (kind == "response") return body;
            if (kind == "bye") {
                const Value* reason = body.find("reason");
                host_.drop(b, reason && reason->isString() ? reason->asString() : std::string("it said goodbye"));
                return std::unexpected(std::string("the bot left during the request"));
            }
            if (kind != "service") return std::unexpected(std::format("the bot sent '{}' during a request (expected service or response)", kind));
            const Value* name = body.find("name");
            const Value* args = body.find("args");
            Value reply;
            const auto asked = Clock::now();
            if (!name || !name->isString()) {
                reply = object({{"service_error", object({{"type", Value("TypeError")}, {"message", Value("a service names itself: {\"name\": ...}")}})},
                                {"id", Value(static_cast<int64_t>(id))}});
            } else {
                auto result = services(name->asString(), args ? *args : Value::emptyMap());
                if (result) {
                    reply = object({{"result", std::move(*result)}, {"id", Value(static_cast<int64_t>(id))}});
                } else {
                    auto [type, message] = splitError(result.error());
                    reply = object({{"service_error", object({{"type", Value(type)}, {"message", Value(message)}})}, {"id", Value(static_cast<int64_t>(id))}});
                }
            }
            // The engine's own time on a service does not count against the bot.
            deadline += Clock::now() - asked;
            if (auto w = b->link.write(line(reply), deadline); !w) {
                host_.drop(b, w.error());
                return std::unexpected("the service's result could not be sent: " + w.error());
            }
        }
    }

private:
    BotHost::Impl& host_;
    uint32_t slot_;
};

void BotHost::Impl::acceptLoop() {
    struct Pending {
        net::Socket socket;
        std::string buffer;
        std::string peer;
        Clock::time_point deadline;
    };
    std::vector<Pending> pending;
    while (!stopping) {
        std::vector<net::PollItem> items(1 + pending.size());
        items[0].socket = listener.native();
        items[0].wantRead = true;
        for (size_t i = 0; i < pending.size(); ++i) {
            items[i + 1].socket = pending[i].socket.native();
            items[i + 1].wantRead = true;
        }
        net::pollSockets(items, 100);
        for (;;) {
            net::Socket s = net::acceptConnection(listener);
            if (!s.valid()) break;
            if (pending.size() >= 16) continue;   // closed: too many waiting to say hello
            std::string peer = s.peerAddress();
            pending.push_back({std::move(s), {}, std::move(peer), Clock::now() + options.handshakeTimeout});
        }
        for (size_t i = 0; i < pending.size();) {
            Pending& p = pending[i];
            Link link;
            link.socket = std::move(p.socket);
            link.buffer = std::move(p.buffer);
            link.maxLine = 64 * 1024;
            const bool open = link.takeAvailable();
            const size_t nl = link.buffer.find('\n');
            if (nl != std::string::npos) {
                std::string text = link.buffer.substr(0, nl);
                if (!text.empty() && text.back() == '\r') text.pop_back();
                hello(std::move(link.socket), link.buffer.substr(nl + 1), p.peer, text);
                pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            if (!open || Clock::now() > p.deadline) {
                report(std::format("Bots: {} said no hello{}", p.peer, open ? " in time" : " before closing"));
                pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            p.socket = std::move(link.socket);
            p.buffer = std::move(link.buffer);
            ++i;
        }
    }
}

void BotHost::Impl::hello(net::Socket socket, std::string rest, const std::string& peer, const std::string& text) {
    Link link;
    link.socket = std::move(socket);
    const auto soon = Clock::now() + std::chrono::seconds(2);
    auto refuse = [&](const std::string& why) {
        [[maybe_unused]] auto w = link.write(line(object({{"refused", object({{"message", Value(why)}})}})), soon);
        link.socket.shutdownSend();
        report(std::format("Bots: refused {}: {}", peer, why));
    };
    auto parsed = script::parseJson(text);
    const Value* h = parsed ? parsed->find("hello") : nullptr;
    if (!h || !h->isMap()) return refuse("the first message must be {\"hello\": {\"api\", \"token\", \"slot\", \"name\"}}");
    const Value* api = h->find("api");
    if (!api || !api->isInt() || api->asInt() != kApiVersion) return refuse(std::format("this game speaks api {}", kApiVersion));
    const Value* token = h->find("token");
    if (!token || !token->isString() || !sameToken(token->asString(), options.token)) return refuse("wrong token");
    const Value* name = h->find("name");
    const std::string botName = name && name->isString() ? name->asString() : std::string();
    const Value* wanted = h->find("slot");
    if (wanted && !wanted->isNull() && (!wanted->isInt() || wanted->asInt() < 0 || wanted->asInt() > 1'000'000))
        return refuse("'slot' must be a slot number, or null for any free one");

    auto b = std::make_shared<Bot>();
    std::unique_lock own(b->busy);   // nobody asks it before it was welcomed
    {
        std::lock_guard lock(mutex);
        auto taken = [&](uint32_t s) {
            auto it = bots.find(s);
            return it != bots.end() && !it->second->gone;
        };
        uint32_t slot = 0;
        if (wanted && wanted->isInt()) {
            slot = static_cast<uint32_t>(wanted->asInt());
            if (!options.slots.empty() && std::find(options.slots.begin(), options.slots.end(), slot) == options.slots.end()) {
                std::string list;
                for (uint32_t s : options.slots) list += (list.empty() ? "" : ", ") + std::to_string(s);
                return refuse(std::format("the game has no external slot {} (it has {})", slot, list));
            }
        } else if (!options.slots.empty()) {
            auto free = std::find_if(options.slots.begin(), options.slots.end(), [&](uint32_t s) { return !taken(s); });
            if (free == options.slots.end()) return refuse("every external slot already has a bot");
            slot = *free;
        } else {
            while (taken(slot)) ++slot;
        }
        if (taken(slot)) {
            // A bot that connects again takes its slot back, unless the old connection is busy.
            std::shared_ptr<Bot> old = bots[slot];
            std::unique_lock oldLock(old->busy, std::try_to_lock);
            if (!oldLock.owns_lock()) return refuse(std::format("slot {} is busy with another bot", slot));
            [[maybe_unused]] auto w =
                old->link.write(line(object({{"bye", object({{"reason", Value("another bot connected to this slot")}})}})), soon);
            old->gone = true;
            old->link.socket.close();
            report(std::format("Bots: {} (slot {}) replaced by a new connection", old->name.empty() ? std::string("a bot") : old->name, slot));
        }
        b->slot = slot;
        b->name = botName;
        b->peer = peer;
        b->link.socket = std::move(link.socket);
        b->link.buffer = std::move(rest);
        b->link.maxLine = options.maxMessageBytes;
        bots[slot] = b;
    }
    const Value welcome = object({{"welcome", object({{"api", Value(kApiVersion)},
                                                      {"slot", Value(static_cast<int64_t>(b->slot))},
                                                      {"game", Value(options.gameName)},
                                                      {"timeout_ms", Value(static_cast<int64_t>(timeoutMs.load()))}})}});
    if (auto w = b->link.write(line(welcome), soon); !w) {
        own.unlock();
        std::unique_lock again(b->busy);
        drop(b, w.error());
        return;
    }
    report(std::format("Bots: {} connected to slot {} from {}", botName.empty() ? std::string("a bot") : botName, b->slot, peer));
    own.unlock();
    changed.notify_all();
}

BotHost::BotHost(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

BotHost::~BotHost() {
    impl_->stopping = true;
    if (impl_->thread.joinable()) impl_->thread.join();
    std::lock_guard lock(impl_->mutex);
    for (auto& [slot, b] : impl_->bots) {
        b->gone = true;
        b->link.socket.close();
    }
}

std::expected<std::unique_ptr<BotHost>, std::string> BotHost::open(BotHostOptions options) {
    auto impl = std::make_unique<Impl>();
    if (options.token.empty()) options.token = newBotToken();
    impl->timeoutMs = options.requestTimeout.count();
    auto listener = net::listenTcp(options.bind, options.port);
    if (!listener) return std::unexpected(std::format("bots cannot connect at {}:{}: {}", options.bind, options.port, listener.error()));
    impl->listener = std::move(*listener);
    impl->port = impl->listener.localPort();
    impl->options = std::move(options);
    std::unique_ptr<BotHost> host(new BotHost(std::move(impl)));
    Impl* raw = host->impl_.get();
    host->impl_->thread = std::thread([raw] { raw->acceptLoop(); });
    return host;
}

uint16_t BotHost::port() const { return impl_->port; }
const std::string& BotHost::token() const { return impl_->options.token; }

std::string BotHost::address() const {
    const std::string& where = impl_->options.bind;
    return std::format("{}:{}", where.empty() || where == "0.0.0.0" ? std::string("127.0.0.1") : where, impl_->port);
}

ExternalBot* BotHost::bot(uint32_t slot) {
    std::lock_guard lock(impl_->mutex);
    auto it = impl_->bots.find(slot);
    if (it == impl_->bots.end() || it->second->gone) return nullptr;
    auto& proxy = impl_->proxies[slot];
    if (!proxy) proxy = std::make_unique<SlotBot>(*impl_, slot);
    return proxy.get();
}

bool BotHost::connected(uint32_t slot) const { return impl_->botOf(slot) != nullptr; }

std::vector<uint32_t> BotHost::connectedSlots() const {
    std::lock_guard lock(impl_->mutex);
    std::vector<uint32_t> out;
    for (const auto& [slot, b] : impl_->bots)
        if (!b->gone) out.push_back(slot);
    return out;
}

bool BotHost::waitFor(std::span<const uint32_t> slots, std::chrono::milliseconds timeout) const {
    std::unique_lock lock(impl_->mutex);
    return impl_->changed.wait_for(lock, timeout, [&] {
        return std::all_of(slots.begin(), slots.end(), [&](uint32_t s) {
            auto it = impl_->bots.find(s);
            return it != impl_->bots.end() && !it->second->gone;
        });
    });
}

void BotHost::setRequestTimeout(std::chrono::milliseconds timeout) { impl_->timeoutMs = std::max<int64_t>(1, timeout.count()); }
std::chrono::milliseconds BotHost::requestTimeout() const { return std::chrono::milliseconds(impl_->timeoutMs.load()); }

void BotHost::sayGoodbye(const script::Value& details) {
    std::vector<std::shared_ptr<Bot>> all;
    {
        std::lock_guard lock(impl_->mutex);
        for (auto& [slot, b] : impl_->bots) all.push_back(b);
        impl_->bots.clear();
    }
    const Value bye = object({{"bye", details.isMap() ? details : Value::emptyMap()}});
    for (const auto& b : all) {
        std::lock_guard busy(b->busy);
        if (b->gone.exchange(true)) continue;
        [[maybe_unused]] auto w = b->link.write(line(bye), Clock::now() + std::chrono::seconds(2));
        b->link.socket.shutdownSend();
        b->link.socket.close();
    }
    impl_->changed.notify_all();
}

PlayerSetup BotHost::playerSetup(PlayerSetup base) {
    base.externals = [this](uint32_t slot) { return bot(slot); };
    return base;
}

std::string newBotToken() {
    std::array<uint8_t, 16> bytes{};
    net::crypto::randomBytes(bytes);
    return net::crypto::hex(bytes);
}

std::vector<uint32_t> externalSlots(std::span<const game::EmpireSetup> empires) {
    std::vector<uint32_t> out;
    for (const game::EmpireSetup& e : empires)
        if (e.controller.kind == game::Controller::Kind::External) out.push_back(e.controller.slot);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

} // namespace opense4::sdk
