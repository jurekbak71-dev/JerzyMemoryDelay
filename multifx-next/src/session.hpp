#pragma once
#include "engine.hpp"
#include "json.hpp"
#include <string>
namespace nextfx {
using Json = nlohmann::json;
class Session {
  struct Entry {
    SlotSpec slot;
    std::string path, asset, id, version;
    std::vector<std::string> keys;
    std::vector<std::array<float, 2>> ranges;
  };
  struct Config {
    std::vector<Entry> slots;
    std::vector<AudioEdge> edges;
    std::vector<MidiRoute> routes;
    std::vector<MidiBinding> bindings;
    float master = 1;
    bool master_update = true, bpm_update = true;
    bool automatic = true;
    double bpm = 120;
  };
  Engine &engine_;
  Config config_;
  int next_id_ = 1;
  Entry open(int id, const std::string &path, const std::string &asset);
  GraphSpec graph(const Config &) const;
  void apply(Config);
  Json encode(const Config &) const;
  Config observed_config() const;

public:
  explicit Session(Engine &e) : engine_(e) {}
  bool collect() { return engine_.collect(); }
  Json snapshot() const { return encode(observed_config()); }
  void restore(const Json &);
  std::string command(const std::string &);
};
} // namespace nextfx
