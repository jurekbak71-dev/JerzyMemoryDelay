#include "session.hpp"
#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
namespace nextfx {
namespace {
std::string mode(Mode m) {
    return m == Mode::Active ? "ACTIVE" : m == Mode::Bypass ? "BYPASS" : "UNLOADED";
}
Mode parse_mode(const std::string &s) {
    if (s == "ACTIVE")
        return Mode::Active;
    if (s == "BYPASS")
        return Mode::Bypass;
    if (s == "UNLOADED")
        return Mode::Unloaded;
    throw std::runtime_error("unknown mode");
}
Json read_json(const std::string &path) {
    if (std::filesystem::file_size(path) > 1024 * 1024)
        throw std::runtime_error("JSON too large");
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("cannot read JSON");
    return Json::parse(f);
}
void write_json(const std::string &path, const Json &j) {
    auto target = std::filesystem::absolute(path);
    auto pattern = target.string() + ".tmp.XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back(0);
    int fd = mkstemp(name.data());
    if (fd < 0)
        throw std::runtime_error("cannot create preset temporary file");
    try {
        const auto text = j.dump(2) + "\n";
        size_t pos = 0;
        while (pos < text.size()) {
            ssize_t n = write(fd, text.data() + pos, text.size() - pos);
            if (n <= 0)
                throw std::runtime_error("preset write failed");
            pos += size_t(n);
        }
        if (fsync(fd))
            throw std::runtime_error("preset fsync failed");
        close(fd);
        fd = -1;
        std::filesystem::rename(name.data(), target);
        int dir = open(target.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
        if (dir >= 0) {
            fsync(dir);
            close(dir);
        }
    } catch (...) {
        if (fd >= 0)
            close(fd);
        unlink(name.data());
        throw;
    }
}
} // namespace
Session::Entry Session::open(int id, const std::string &path, const std::string &asset) {
    Entry e;
    e.path = std::filesystem::canonical(path).string();
    e.asset = asset.empty() ? "" : std::filesystem::canonical(asset).string();
    auto lib = Library::open(e.path);
    const auto &d = lib->descriptor();
    auto manifest = std::filesystem::path(e.path).parent_path() / "manifest.json";
    if (std::filesystem::exists(manifest)) {
        auto m = read_json(manifest.string());
        if (m.at("format_version") != 1 || m.at("abi_major") != 3 || m.at("plugin_id") != d.id ||
            m.at("plugin_version") != d.version || m.at("ports").at("inputs") != d.audio_inputs ||
            m.at("ports").at("outputs") != d.audio_outputs ||
            m.at("ports").at("midi_input") != bool(d.flags & JFX_MIDI_INPUT) ||
            m.at("parameters").size() != d.parameter_count)
            throw std::runtime_error("manifest differs from ABI");
        for (uint32_t i = 0; i < d.parameter_count; ++i) {
            const auto &p = m.at("parameters").at(i);
            if (p.at("key") != d.parameters[i].key ||
                p.at("min").get<float>() != d.parameters[i].minimum ||
                p.at("max").get<float>() != d.parameters[i].maximum ||
                p.at("default").get<float>() != d.parameters[i].initial)
                throw std::runtime_error("manifest parameter differs from ABI");
        }
    }
    e.id = d.id;
    e.version = d.version;
    e.slot.id = id;
    e.slot.inputs = d.audio_inputs;
    e.slot.latency = d.latency_frames;
    e.slot.midi = (d.flags & JFX_MIDI_INPUT) != 0;
    for (uint32_t i = 0; i < d.parameter_count; ++i) {
        e.slot.parameters.push_back(d.parameters[i].initial);
        e.keys.emplace_back(d.parameters[i].key);
        e.ranges.push_back({d.parameters[i].minimum, d.parameters[i].maximum});
    }
    e.slot.instance = Instance::create(lib, engine_.sample_rate(), engine_.max_frames(), e.asset);
    return e;
}
GraphSpec Session::graph(const Config &c) const {
    GraphSpec g;
    g.edges.clear();
    g.bpm = c.bpm;
    for (const auto &e : c.slots)
        g.slots.push_back(e.slot);
    g.midi_routes = c.routes;
    if (!c.automatic) {
        g.edges = c.edges;
        return g;
    }
    int first = -1, last = 0;
    for (const auto &e : c.slots)
        if (e.slot.inputs) {
            if (first == -1)
                first = e.slot.id;
            g.edges.push_back({last, e.slot.id});
            last = e.slot.id;
        }
    g.edges.push_back({last, -1});
    for (const auto &e : c.slots)
        if (!e.slot.inputs)
            g.edges.push_back({e.slot.id, first == -1 ? -1 : first});
    return g;
}
void Session::apply(Config c) {
    engine_.collect();
    if (!engine_.publish(graph(c)))
        throw std::runtime_error("busy: await audio acknowledgement");
    config_ = std::move(c);
}
Json Session::encode(const Config &c) const {
    Json j = {{"schema_version", 1},          {"bpm", c.bpm},
              {"automatic", c.automatic},     {"slots", Json::array()},
              {"audio_edges", Json::array()}, {"midi_routes", Json::array()}};
    for (const auto &e : c.slots) {
        Json params = Json::object();
        Json ranges = Json::object();
        for (size_t i = 0; i < e.keys.size(); ++i)
            ranges[e.keys[i]] = e.ranges[i];
        if (e.slot.instance) {
            const auto &d = e.slot.instance->descriptor();
            for (uint32_t i = 0; i < d.parameter_count; ++i)
                params[d.parameters[i].key] = e.slot.parameters[i];
        } else {
            // Parameter key metadata is retained in the session's saved entry without keeping code
            // mapped.
            for (size_t i = 0; i < e.keys.size(); ++i)
                params[e.keys[i]] = e.slot.parameters[i];
        }
        j["slots"].push_back({{"id", e.slot.id},
                              {"plugin_id", e.id},
                              {"version", e.version},
                              {"path", e.path},
                              {"asset", e.asset},
                              {"mode", mode(e.slot.mode)},
                              {"inputs", e.slot.inputs},
                              {"latency", e.slot.latency},
                              {"midi", e.slot.midi},
                              {"parameters", params},
                              {"ranges", ranges}});
    }
    for (const auto &e : c.edges)
        j["audio_edges"].push_back(
            {e.from, e.to, e.gain, e.source_channel, e.target_channel, e.channels});
    for (const auto &r : c.routes)
        j["midi_routes"].push_back({r.from, r.to, r.channels});
    return j;
}
void Session::restore(const Json &j) {
    if (j.at("schema_version") != 1 || !j.at("slots").is_array() || j.at("slots").size() > 16)
        throw std::runtime_error("invalid session schema");
    Config c;
    c.bpm = j.at("bpm");
    c.automatic = j.at("automatic");
    int next_id = next_id_;
    for (const auto &item : j.at("slots")) {
        if (!item.at("id").is_number_integer())
            throw std::runtime_error("slot ID must be integer");
        auto raw_id = item.at("id").get<int64_t>();
        if (raw_id < 1 || raw_id > 1000000000)
            throw std::runtime_error("invalid saved slot ID");
        int id = int(raw_id);
        auto state = parse_mode(item.at("mode"));
        Entry e;
        if (state == Mode::Unloaded) {
            e.path = item.at("path");
            e.asset = item.at("asset");
            e.id = item.at("plugin_id");
            e.version = item.at("version");
            e.slot.id = id;
            e.slot.mode = state;
            e.slot.inputs = item.at("inputs");
            e.slot.latency = item.at("latency");
            e.slot.midi = item.at("midi");
            if (!item.at("parameters").is_object() || item.at("parameters").size() > 48)
                throw std::runtime_error("invalid unloaded parameter metadata");
            for (auto it = item.at("parameters").begin(); it != item.at("parameters").end(); ++it) {
                float value = it.value();
                auto range = item.at("ranges").at(it.key()).get<std::array<float, 2>>();
                if (!std::isfinite(value) || !std::isfinite(range[0]) || !std::isfinite(range[1]) ||
                    range[0] > range[1] || value < range[0] || value > range[1])
                    throw std::runtime_error("invalid unloaded parameter range");
                e.ranges.push_back(range);
                e.keys.push_back(it.key());
                e.slot.parameters.push_back(value);
            }
        } else {
            e = open(id, item.at("path"), item.at("asset"));
            if (e.id != item.at("plugin_id").get<std::string>() ||
                e.version != item.at("version").get<std::string>())
                throw std::runtime_error("preset plugin identity mismatch");
            const auto &d = e.slot.instance->descriptor();
            if (item.at("parameters").size() != d.parameter_count)
                throw std::runtime_error("preset parameter count mismatch");
            for (uint32_t i = 0; i < d.parameter_count; ++i)
                e.slot.parameters[i] = item.at("parameters").at(d.parameters[i].key);
            e.slot.mode = state;
        }
        next_id = std::max(next_id, id + 1);
        c.slots.push_back(std::move(e));
    }
    for (const auto &a : j.at("audio_edges")) {
        if (a.size() != 6)
            throw std::runtime_error("invalid saved edge");
        c.edges.push_back({a[0], a[1], a[2], a[3], a[4], a[5]});
    }
    for (const auto &r : j.at("midi_routes")) {
        if (r.size() != 3)
            throw std::runtime_error("invalid saved MIDI route");
        c.routes.push_back({r[0], r[1], r[2]});
    }
    apply(std::move(c));
    next_id_ = next_id;
}
std::string Session::command(const std::string &line) {
    try {
        collect();
        std::istringstream ss(line);
        std::string op, extra;
        ss >> op;
        if (op == "PING")
            return "OK";
        if (op == "LIST")
            return "OK " + snapshot().dump();
        if (op == "STATS") {
            auto d = engine_.diagnostics();
            Json j = {{"blocks", d.blocks},
                      {"total_ns", d.total_ns},
                      {"max_ns", d.max_ns},
                      {"deadline_misses", d.deadline_misses},
                      {"oversize_blocks", d.oversize_blocks},
                      {"midi_overflows", d.midi_overflows},
                      {"bad_samples", d.bad_samples},
                      {"latency_frames", d.latency},
                      {"owned_bytes_estimate", d.owned_bytes},
                      {"pending", engine_.busy()},
                      {"slot_ids", d.slot_ids},
                      {"slot_calls", d.slot_calls},
                      {"slot_total_ns", d.slot_total_ns},
                      {"slot_max_ns", d.slot_max_ns}};
            std::ifstream stat("/proc/self/statm");
            uint64_t total = 0, resident = 0;
            if (stat >> total >> resident)
                j["rss_bytes"] = resident * uint64_t(sysconf(_SC_PAGESIZE));
            return "OK " + j.dump();
        }
        if (op == "SAVE") {
            std::string path;
            if (!(ss >> std::quoted(path)) || (ss >> extra))
                throw std::runtime_error("SAVE path");
            write_json(path, snapshot());
            return "OK";
        }
        if (op == "LOAD") {
            std::string path;
            if (!(ss >> std::quoted(path)) || (ss >> extra))
                throw std::runtime_error("LOAD path");
            restore(read_json(path));
            return "OK";
        }
        Config c = config_;
        auto entry = [&](int id) -> Entry & {
            for (auto &e : c.slots)
                if (e.slot.id == id)
                    return e;
            throw std::runtime_error("unknown slot");
        };
        std::string result = "OK";
        if (op == "ADD") {
            std::string path, asset;
            if (!(ss >> std::quoted(path)))
                throw std::runtime_error("ADD path [asset]");
            if (ss >> std::quoted(asset)) {
                if (ss >> extra)
                    throw std::runtime_error("extra arguments");
            }
            if (next_id_ > 1000000000)
                throw std::runtime_error("slot ID exhausted");
            auto e = open(next_id_, path, asset);
            if (e.slot.midi)
                c.routes.push_back({0, next_id_, 65535});
            c.slots.push_back(std::move(e));
            result += " " + std::to_string(next_id_);
        } else if (op == "CLEAR") {
            if (ss >> extra)
                throw std::runtime_error("extra arguments");
            c = Config{};
        } else if (op == "REMOVE") {
            int id;
            if (!(ss >> id) || (ss >> extra))
                throw std::runtime_error("REMOVE id");
            entry(id);
            std::erase_if(c.slots, [&](const auto &e) { return e.slot.id == id; });
            std::erase_if(c.edges, [&](const auto &e) { return e.from == id || e.to == id; });
            std::erase_if(c.routes, [&](const auto &r) { return r.to == id; });
        } else if (op == "MOVE") {
            int id, pos;
            if (!(ss >> id >> pos) || (ss >> extra) || pos < 0 || size_t(pos) >= c.slots.size())
                throw std::runtime_error("MOVE id position");
            entry(id);
            auto it = std::find_if(c.slots.begin(), c.slots.end(),
                                   [&](const auto &e) { return e.slot.id == id; });
            auto moved = std::move(*it);
            c.slots.erase(it);
            c.slots.insert(c.slots.begin() + pos, std::move(moved));
        } else if (op == "MODE") {
            int id;
            std::string state;
            if (!(ss >> id >> state) || (ss >> extra))
                throw std::runtime_error("MODE id ACTIVE|BYPASS|UNLOADED");
            auto &e = entry(id);
            auto m = parse_mode(state);
            if (m != e.slot.mode) {
                if (m == Mode::Unloaded)
                    e.slot.instance.reset();
                else if (e.slot.mode == Mode::Unloaded ||
                         (e.slot.mode == Mode::Bypass && m == Mode::Active)) {
                    auto fresh = open(id, e.path, e.asset);
                    if (fresh.id != e.id || fresh.version != e.version ||
                        fresh.keys.size() != e.keys.size())
                        throw std::runtime_error("reactivation identity mismatch");
                    for (size_t i = 0; i < fresh.keys.size(); ++i) {
                        auto it = std::find(e.keys.begin(), e.keys.end(), fresh.keys[i]);
                        if (it == e.keys.end())
                            throw std::runtime_error("reactivation parameter mismatch");
                        fresh.slot.parameters[i] = e.slot.parameters[size_t(it - e.keys.begin())];
                    }
                    e = std::move(fresh);
                }
                e.slot.mode = m;
            }
        } else if (op == "SET") {
            int id;
            std::string key;
            float value;
            if (!(ss >> id >> key >> value) || (ss >> extra) || !std::isfinite(value))
                throw std::runtime_error("SET id key finite_value");
            auto &e = entry(id);
            auto it = std::find(e.keys.begin(), e.keys.end(), key);
            if (it == e.keys.end())
                throw std::runtime_error("unknown parameter");
            size_t i = size_t(it - e.keys.begin());
            if (value < e.ranges[i][0] || value > e.ranges[i][1])
                throw std::runtime_error("parameter out of range");
            e.slot.parameters[i] = value;
        } else if (op == "BPM") {
            if (!(ss >> c.bpm) || (ss >> extra))
                throw std::runtime_error("BPM value");
        } else if (op == "AUTO") {
            c.automatic = true;
        } else if (op == "EDGES_CLEAR") {
            c.automatic = false;
            c.edges.clear();
        } else if (op == "CONNECT") {
            AudioEdge edge;
            if (!(ss >> edge.from >> edge.to >> edge.gain))
                throw std::runtime_error(
                    "CONNECT from to gain [source_channel target_channel channels]");
            if (ss >> edge.source_channel) {
                if (!(ss >> edge.target_channel >> edge.channels) || (ss >> extra))
                    throw std::runtime_error("invalid channel map");
            }
            if (c.automatic) {
                c.edges = graph(c).edges;
                c.automatic = false;
            }
            c.edges.push_back(edge);
        } else if (op == "DISCONNECT") {
            int from, to;
            if (!(ss >> from >> to) || (ss >> extra))
                throw std::runtime_error("DISCONNECT from to");
            if (c.automatic) {
                c.edges = graph(c).edges;
                c.automatic = false;
            }
            std::erase_if(c.edges, [&](const auto &e) { return e.from == from && e.to == to; });
        } else if (op == "ROUTE") {
            MidiRoute r;
            if (!(ss >> r.from >> r.to))
                throw std::runtime_error("ROUTE from to [channel_mask]");
            if (ss >> r.channels) {
                if (ss >> extra)
                    throw std::runtime_error("extra route arguments");
            }
            std::erase_if(c.routes,
                          [&](const auto &old) { return old.from == r.from && old.to == r.to; });
            c.routes.push_back(r);
        } else
            throw std::runtime_error("unknown command");
        apply(std::move(c));
        if (op == "ADD")
            ++next_id_;
        return result;
    } catch (const std::exception &e) {
        return "ERR " + std::string(e.what());
    }
}
} // namespace nextfx
