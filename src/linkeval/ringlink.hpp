#ifndef BOOKSIM_RINGLINK_HPP
#define BOOKSIM_RINGLINK_HPP

#include "flitchannel.hpp"
#include "credit.hpp"
#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

// A bidirectional physical link exclusively owned by one single-VC network.
// IQ-router credits on the source side reserve the bounded adapter queue, NOT
// the destination buffer. Destination credits only gate ejection from the ring.
class RingLink : public TimedModule {
public:
  struct Counters {
    uint64_t injected = 0, delivered = 0, recirculated = 0, blocked = 0;
    uint64_t recoveries = 0, recycled = 0, bypassed = 0;
  };
  static Counters counters;
  RingLink(Module* parent, const std::string& name, int vcs, int depth,
           int stages, bool input_reg, bool out_of_order);
  void Accept(int side, Flit* flit);
  void Return(int side, Credit* credit);
  Flit* Output(int side) const { return output_[side]; }
  Credit* Returned(int side) const { return returned_[side]; }
  void ReadInputs() override {}
  void Evaluate() override;
  void WriteOutputs() override {}
private:
  struct Slot {
    Flit* flit = nullptr;
    int origin = 0;
    bool exclusive = false;
    bool valid() const { return flit || exclusive; }
  };
  struct Tap {
    Slot srfr, erfr, orfr;
    std::deque<Slot> wire;
    std::deque<Flit*> pending;
    // BookSim flit IDs are globally unique during flight; no SID aliasing.
    std::deque<int> overflow;
    int free = 0, state = 0, timer = 0;
    bool was_active = false;
  };
  int depth_, timeout_, ring_size_;
  bool ooo_;
  std::array<Tap, 2> taps_;
  std::array<std::deque<int>, 2> in_flight_;
  std::array<Flit*, 2> output_{{nullptr, nullptr}};
  std::array<Credit*, 2> returned_{{nullptr, nullptr}};
};

class RingFlitChannel : public FlitChannel {
public:
  RingFlitChannel(Module* parent, const std::string& name, int classes,
                  std::shared_ptr<RingLink> link, int side)
      : FlitChannel(parent, name, classes), link_(link), side_(side) {}
  void ReadInputs() override { if (_input) link_->Accept(side_, _input); _input = nullptr; }
  void Evaluate() override { if (!side_) link_->Evaluate(); }
  void WriteOutputs() override { _output = link_->Output(side_); }
private:
  std::shared_ptr<RingLink> link_;
  int side_;
};

class RingCreditChannel : public Channel<Credit> {
public:
  RingCreditChannel(Module* parent, const std::string& name,
                    std::shared_ptr<RingLink> link, int side)
      : Channel<Credit>(parent, name), link_(link), side_(side) {}
  void ReadInputs() override { if (_input) link_->Return(side_, _input); _input = nullptr; }
  void Evaluate() override {}
  void WriteOutputs() override { _output = link_->Returned(side_); }
private:
  std::shared_ptr<RingLink> link_;
  int side_;
};
#endif
