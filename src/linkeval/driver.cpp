// The normal BookSim executable remains usable without the Netrace adapter.
#ifdef LINK_EVAL
#include "driver.hpp"
#include "ringlink.hpp"
#include "booksim_config.hpp"
#include "network.hpp"
#include "routefunc.hpp"
#include "traffic.hpp"
#include "random_utils.hpp"
#include "replay.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

SimTime link_eval_time = -1;
namespace {
void require(bool ok, const std::string& why) {
  if (!ok) throw std::runtime_error(why);
}
using Options = std::map<std::string, std::string>;
long long integer(const Options& options, const std::string& key) {
  size_t used = 0;
  auto value = std::stoll(options.at(key), &used);
  require(used == options.at(key).size() && value >= 0, "invalid " + key);
  return value;
}
std::vector<int> numbers(std::string text) {
  std::replace(text.begin(), text.end(), ':', ' ');
  std::istringstream stream(text);
  std::vector<int> result;
  int n;
  while (stream >> n) result.push_back(n);
  return result;
}

// The upstream IQ router and mesh remain the simulation engine. The only
// replacement is the bidirectional ring transport installed by KNCube.
class Fabric {
public:
  Fabric(BookSimConfig& config, unsigned count)
      : nodes(config.GetInt("k") * config.GetInt("k")),
        vcs(config.GetInt("num_vcs")), depth(config.GetInt("vc_buf_size")) {
    for (unsigned n = 0; n < count; ++n) {
      nets.emplace_back(Network::New(config, "net_" + std::to_string(n)));
      require(bool(nets.back()), "cannot create mesh");
    }
    available.resize(count * nodes, std::vector<int>(vcs, depth));
    output.resize(count * nodes);
    next_vc.resize(count * nodes);
    occupancy.resize(count * nodes, std::vector<int>(vcs, 0));
  }
  void begin() {
    for (unsigned n = 0; n < nets.size(); ++n) {
      for (int node = 0; node < nodes; ++node) {
        int at = n * nodes + node;
        if (auto* c = nets[n]->ReadCredit(node)) {
          for (int v : c->vc)
            require(v >= 0 && v < vcs && ++available[at][v] <= depth, "injection credit overflow");
          c->Free();
        }
        if (auto* f = nets[n]->ReadFlit(node)) {
          require(f->dest == node && f->vc >= 0 && f->vc < vcs, "misrouted ejection");
          require(++occupancy[at][f->vc] <= depth, "ejection adapter overflow");
          output[at].push_back(f);
        }
      }
      nets[n]->ReadInputs();
    }
  }
  Flit* eject(unsigned n, int node, bool ready) {
    int at = n * nodes + node;
    auto& q = output[at];
    if (!ready || q.empty()) return nullptr;
    Flit* f = q.front(); q.pop_front();
    --occupancy[at][f->vc];
    auto* c = Credit::New(); c->vc.insert(f->vc);
    nets[n]->WriteCredit(c, node);
    return f;
  }
  bool inject(unsigned n, int node, Flit* f) {
    int at = n * nodes + node;
    for (int offset = 0; offset < vcs; ++offset) {
      int v = (next_vc[at] + offset) % vcs;
      if (!available[at][v]) continue;
      --available[at][v];
      next_vc[at] = (v + 1) % vcs;
      f->vc = v; f->itime = link_eval_time;
      nets[n]->WriteFlit(f, node);
      return true;
    }
    return false;
  }
  void end() { for (auto& net : nets) { net->Evaluate(); net->WriteOutputs(); } }
  int nodes, vcs, depth;
  std::vector<std::unique_ptr<Network>> nets;
private:
  std::vector<std::vector<int>> available, occupancy;
  std::vector<int> next_vc;
  std::vector<std::deque<Flit*>> output;
};

struct Record {
  int source, dest, network;
  trace_sim::Payload payload{};
};
class Scoreboard {
public:
  Flit* create(int network, int source, int dest, const trace_sim::Payload& data = {}) {
    require(sequence < std::numeric_limits<int>::max(), "BookSim flit ID limit reached");
    auto* f = Flit::New();
    f->id = f->pid = sequence++;
    f->head = f->tail = true;
    f->src = source; f->dest = dest; f->cl = 0; f->subnetwork = network;
    f->type = Flit::ANY_TYPE;
    f->ctime = link_eval_time;
    records.emplace(f->id, Record{source, dest, network, data});
    return f;
  }
  Record retire(unsigned network, int node, Flit* f) {
    auto found = records.find(f->id);
    require(found != records.end(), "unknown or duplicate ejected flit");
    auto record = found->second;
    require(record.source == f->src && record.dest == node && record.dest == f->dest &&
            record.network == int(network), "flit metadata mismatch");
    records.erase(found);
    return record;
  }
  std::unordered_map<int, Record> records;
private:
  int sequence = 0;
};

void run_trace(BookSimConfig& config, const Options& opt, const std::string& result) {
  trace_sim::Replay replay(opt.at("trace"), opt.count("events") ? opt.at("events") : "",
                           integer(opt, "region"));
  Fabric fabric(config, trace_sim::NetworkCount);
  require(replay.nodes() == unsigned(fabric.nodes), "trace requires a matching 8x8 mesh");
  Scoreboard scoreboard;
  std::vector<Flit*> pending(fabric.nodes * trace_sim::NetworkCount, nullptr);
  long long limit = integer(opt, "max-cycles");
  uint64_t cycles = 0;
  trace_sim::SimulationTimer timer;
  for (; !replay.drained(); ++cycles) {
    if (limit != 0 && cycles >= uint64_t(limit))
      throw std::runtime_error("trace watchdog: " + replay.progress() + " " + timer.progress(cycles));
    require(cycles < uint64_t(INT64_MAX - 256), "BookSim 64-bit cycle counter exhausted");
    link_eval_time = SimTime(cycles);
    replay.begin_cycle(cycles);
    fabric.begin();
    for (unsigned n = 0; n < trace_sim::NetworkCount; ++n) {
      for (int node = 0; node < fabric.nodes; ++node) {
        if (auto* f = fabric.eject(n, node, true)) {
          auto record = scoreboard.retire(n, node, f);
          replay.eject(n, node, f->src, f->dest, record.payload);
          f->Free();
        }
        const auto* packet = replay.offer(n, node);
        if (!packet) continue;
        auto& f = pending[n * fabric.nodes + node];
        if (packet->src == packet->dst) {
          require(!f, "local packet blocked behind injected packet");
          replay.complete_local(n, node);
          continue;
        }
        if (!f) f = scoreboard.create(n, node, packet->dst, trace_sim::payload_for(*packet));
        if (fabric.inject(n, node, f)) { replay.inject(n, node); f = nullptr; }
      }
    }
    fabric.end();
    replay.end_cycle();
    if ((cycles + 1) % 10000000 == 0)
      std::cout << "cycle=" << cycles + 1 << ' ' << replay.progress() << ' '
                << timer.progress(cycles + 1) << std::endl;
  }
  const auto timing = timer.progress(cycles);
  require(scoreboard.records.empty(), "flits remain after region completion");
  // Quiet checking is deliberately excluded from cycles/end-to-end latency.
  for (unsigned i = 0; i < 128; ++i) {
    link_eval_time = SimTime(cycles + i);
    fabric.begin();
    for (unsigned n = 0; n < trace_sim::NetworkCount; ++n)
      for (int node = 0; node < fabric.nodes; ++node)
        require(!fabric.eject(n, node, true), "unexpected flit after ROI drain");
    fabric.end();
  }
  replay.write_result(result, cycles, 1);
  std::cout << "PASS " << replay.progress() << " cycles=" << cycles << ' ' << timing << std::endl;
}

void run_synthetic(BookSimConfig& config, const Options& opt, const std::string& result) {
  unsigned networks = integer(opt, "networks");
  require(networks > 0, "at least one network is required");
  Fabric fabric(config, networks);
  Scoreboard scoreboard;
  auto seed = integer(opt, "seed");
  RandomSeed(seed);
  config.Assign("perm_seed", int(seed));
  std::string pattern = opt.at("pattern");
  bool hotspot = pattern == "hotspot";
  std::unique_ptr<TrafficPattern> traffic;
  if (!hotspot) traffic.reset(TrafficPattern::New(pattern, fabric.nodes, &config));
  require(hotspot || bool(traffic), "invalid traffic pattern");
  std::mt19937_64 random(seed);
  std::bernoulli_distribution inject(std::stod(opt.at("inject"))), ready(std::stod(opt.at("ready")));
  std::vector<int> spots = opt.count("hotspots") ? numbers(opt.at("hotspots")) : std::vector<int>{int(random() % fabric.nodes)};
  std::vector<int> weights = opt.count("hotspot-weights") ? numbers(opt.at("hotspot-weights")) : std::vector<int>(spots.size(), 1);
  require(!spots.empty() && spots.size() == weights.size(), "invalid hotspot weights");
  for (size_t i = 0; i < spots.size(); ++i)
    require(spots[i] >= 0 && spots[i] < fabric.nodes && weights[i] > 0, "invalid hotspot");
  std::discrete_distribution<int> choose(weights.begin(), weights.end());
  uint64_t warmup = integer(opt, "warmup-flit"), perf = integer(opt, "perf-flit");
  uint64_t limit = integer(opt, "max-cycles"), timeout = integer(opt, "timeout");
  uint64_t drain = integer(opt, "drain-cycles");
  require(perf > 0 && drain > 0, "perf-flit and drain-cycles must be positive");
  struct Stats {
    uint64_t injected = 0, ejected = 0, samples = 0, first = 0, last = 0;
    long double latency = 0;
    uint64_t max_latency = 0;
    SimTime stop = -1;
  };
  std::vector<Stats> stats(networks);
  std::vector<Flit*> pending(networks * fabric.nodes, nullptr);
  std::map<int, SimTime> live; // ID -> injection time; ordered IDs make oldest checks O(1).
  uint64_t cycle = 0;
  for (;; ++cycle) {
    require(limit == 0 || cycle < limit, "synthetic cycle watchdog expired");
    require(cycle < uint64_t(INT64_MAX - 256), "BookSim 64-bit cycle counter exhausted");
    link_eval_time = SimTime(cycle);
    fabric.begin();
    for (unsigned n = 0; n < networks; ++n) {
      auto& s = stats[n];
      for (int node = 0; node < fabric.nodes; ++node) {
        if (auto* f = fabric.eject(n, node, s.stop >= 0 || ready(random))) {
          scoreboard.retire(n, node, f);
          require(live.erase(f->id) == 1, "ejected flit was never injected");
          uint64_t latency = cycle - f->itime;
          if (s.ejected >= warmup && s.samples < perf) {
            if (!s.samples) s.first = cycle;
            ++s.samples; s.last = cycle;
            s.latency += latency; s.max_latency = std::max(s.max_latency, latency);
          }
          ++s.ejected;
          f->Free();
        }
      }
      if (s.stop < 0 && s.samples == perf) s.stop = SimTime(cycle);
      for (int node = 0; node < fabric.nodes; ++node) {
        auto& f = pending[n * fabric.nodes + node];
        if (!f && s.stop < 0 && inject(random)) {
          int dest = hotspot ? spots[choose(random)] : traffic->dest(node);
          if (pattern == "uniform") while (dest == node) dest = traffic->dest(node);
          if (dest != node) f = scoreboard.create(n, node, dest);
        }
        if (f && fabric.inject(n, node, f)) {
          ++s.injected; live.emplace(f->id, SimTime(cycle)); f = nullptr;
        }
      }
      bool has_pending = std::any_of(pending.begin() + n * fabric.nodes,
          pending.begin() + (n + 1) * fabric.nodes, [](Flit* f) { return f != nullptr; });
      if (s.stop >= 0 && (s.injected != s.ejected || has_pending))
        require(cycle - s.stop <= drain, "synthetic drain watchdog expired");
    }
    fabric.end();
    if (timeout) {
      // Creation and injection order may differ under backpressure.
      for (const auto& item : live)
        require(cycle - item.second <= timeout, "injected flit timeout");
    }
    bool done = scoreboard.records.empty();
    for (const auto& s : stats) done &= s.stop >= 0 && s.injected == s.ejected;
    if (done) break;
  }
  std::ofstream out(result);
  require(bool(out), "cannot write synthetic result");
  out << std::setprecision(12) << "{\n\"schema\":1,\"backend\":\"booksim\",\"cycles\":" << cycle + 1
      << ",\"flits_per_packet\":1,\"networks\":[";
  for (unsigned n = 0; n < networks; ++n) {
    const auto& s = stats[n];
    if (n) out << ',';
    out << "{\"injected\":" << s.injected << ",\"ejected\":" << s.ejected
        << ",\"warmup_flits\":" << warmup << ",\"perf_flits\":" << s.samples
        << ",\"perf_cycles\":" << s.last - s.first + 1
        << ",\"network_latency\":" << s.latency / s.samples
        << ",\"max_network_latency\":" << s.max_latency
        << ",\"flits_per_cycle\":" << double(s.samples) / (s.last - s.first + 1) << '}';
  }
  out << "]}\n";
  out.close(); require(bool(out), "cannot complete result write");
  std::cout << "PASS synthetic cycles=" << cycle + 1 << std::endl;
}
} // namespace

int LinkEvalMain(int argc, char** argv) {
  try {
    require(argc >= 4, "usage: booksim --link-eval config mode result [key=value ...]");
    Options options;
    for (int i = 4; i < argc; ++i) {
      std::string arg = argv[i];
      auto pos = arg.find('=');
      require(pos != std::string::npos && pos > 0 && options.emplace(arg.substr(0, pos), arg.substr(pos+1)).second,
              "invalid/duplicate runtime option: " + arg);
    }
    BookSimConfig config;
    config.ParseFile(argv[1]);
    InitializeRoutingMap(config);
    require(config.GetStr("topology") == "mesh" && config.GetInt("n") == 2, "expected 2D mesh");
    require(config.GetInt("packet_size") == 1, "one packet must be one flit");
    require(config.GetInt("num_vcs") == 1, "each independent network must use exactly one VC");
    gPrintActivity = false; gTrace = false; gWatchOut = nullptr;
    link_eval_time = 0;
    RingLink::counters = {};
    if (std::string(argv[2]) == "trace") run_trace(config, options, argv[3]);
    else if (std::string(argv[2]) == "sim") run_synthetic(config, options, argv[3]);
    else throw std::runtime_error("unknown simulation mode");
    const auto& c = RingLink::counters;
    std::ofstream out(std::string(argv[3]) + ".links.json");
    out << "{\"injected\":" << c.injected << ",\"delivered\":" << c.delivered
        << ",\"recirculated\":" << c.recirculated << ",\"blocked\":" << c.blocked
        << ",\"recoveries\":" << c.recoveries << ",\"recycled\":" << c.recycled
        << ",\"ooo_bypasses\":" << c.bypassed << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ERROR: " << error.what() << std::endl;
    return 1;
  }
}
#endif
