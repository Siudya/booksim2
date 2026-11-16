#include "ringlink.hpp"
#include <algorithm>
#include <stdexcept>

RingLink::Counters RingLink::counters;

RingLink::RingLink(Module* parent, const std::string& name, int vcs, int depth,
                   int stages, bool input_reg, bool out_of_order)
    : TimedModule(parent, name), depth_(depth),
      timeout_(4 * (3 + int(input_reg) + stages)),
      ring_size_(2 * (3 + int(input_reg) + stages)), ooo_(out_of_order) {
  if (vcs != 1 || depth < 2 || stages < 0)
    throw std::runtime_error("RingLink requires one VC, depth >= 2 and stages >= 0");
  for (auto& tap : taps_) {
    tap.free = depth;
    tap.wire.resize(stages + int(input_reg));
  }
}

void RingLink::Accept(int side, Flit* f) {
  if (!f->head || !f->tail || f->vc != 0)
    throw std::runtime_error("RingLink requires single-flit packets on VC 0");
  auto& queue = taps_[side].pending;
  if (queue.size() >= size_t(depth_))
    throw std::runtime_error("RingLink ingress credit overflow");
  queue.push_back(f);
  in_flight_[side].push_back(f->id);
}

void RingLink::Return(int side, Credit* c) {
  // Credits sent by the sink of direction 'side' restore its EJB capacity.
  for (int vc : c->vc) {
    if (vc != 0 || ++taps_[1-side].free > depth_)
      throw std::runtime_error("RingLink ejection credit overflow");
  }
  c->Free();
}

void RingLink::Evaluate() {
  output_.fill(nullptr);
  returned_.fill(nullptr);
  // Snapshot both incoming paths before modifying either tap. Pipeline
  // advancement must be independent of endpoint iteration order.
  std::array<Slot, 2> incoming;
  for (int s = 0; s < 2; ++s) {
    auto& peer = taps_[1-s];
    incoming[s] = peer.wire.empty() ? peer.orfr : peer.wire.front();
  }
  for (int s = 0; s < 2; ++s) {
    auto& t = taps_[s];
    Slot rx = incoming[s];
    const bool active = t.state == 2;
    const bool enter = active && !t.was_active;
    const bool recycle = t.srfr.exclusive && t.srfr.origin == s && !t.srfr.flit;
    Slot forward = active ? t.erfr : t.srfr;
    const bool request = !t.pending.empty();
    const bool ready = !forward.valid();
    const bool inject = request && ready;
    if (enter && (!inject || t.erfr.valid()))
      throw std::runtime_error("RingLink recovery must inject an exclusive slot");
    if (recycle && !active)
      throw std::runtime_error("RingLink exclusive slot returned outside recovery");

    // The receiving IQ input buffer is the EJB. Reserve its last free slot
    // for the oldest overflow flit. ORDER disallows every younger bypass;
    // OOO admits non-VIPs only with at least two free entries.
    if (rx.flit && rx.origin != s) {
      int id = rx.flit->id;
      auto hit = std::find(t.overflow.begin(), t.overflow.end(), id);
      bool vip = !t.overflow.empty() && t.overflow.front() == id;
      bool permit = t.overflow.empty() || vip || (ooo_ && t.free >= 2);
      if (t.free && permit) {
        auto& expected = in_flight_[1-s];
        auto issued = std::find(expected.begin(), expected.end(), id);
        if (issued == expected.end() || (!ooo_ && issued != expected.begin()))
          throw std::runtime_error("RingLink duplicate or in-order delivery violation");
        expected.erase(issued);
        if (!t.overflow.empty() && !vip) ++counters.bypassed;
        if (hit != t.overflow.end()) t.overflow.erase(hit);
        --t.free;
        output_[1-s] = rx.flit;
        rx.flit = nullptr;
        ++counters.delivered;
      } else {
        if (hit == t.overflow.end()) {
          if (t.overflow.size() == size_t(ring_size_))
            throw std::runtime_error("RingLink overflow table exhausted");
          t.overflow.push_back(id);
        }
        ++counters.blocked;
      }
    }
    if (t.srfr.flit && t.srfr.origin == s) ++counters.recirculated;
    if (!t.wire.empty()) {
      t.wire.pop_front();
      t.wire.push_back(t.orfr);
    }
    t.erfr = active && !recycle ? t.srfr : Slot{};
    t.srfr = rx;
    if (inject) {
      forward = Slot{t.pending.front(), s, enter};
      t.pending.pop_front();
      returned_[s] = Credit::New();
      returned_[s]->vc.insert(0);
      ++counters.injected;
    }
    t.orfr = forward;
    t.was_active = active;
    if (t.state == 0 && request && !ready) {
      t.state = 1;
      t.timer = timeout_;
    } else if (t.state == 1) {
      if (inject) t.state = 0;
      else if (t.timer == 0) { t.state = 2; ++counters.recoveries; }
      else --t.timer;
    } else if (active && recycle) {
      t.state = 0;
      ++counters.recycled;
    }
  }
}
