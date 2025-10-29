#ifndef _INJECT_CONTROLLER_HPP_
#define _INJECT_CONTROLLER_HPP_

#include "timed_module.hpp"
#include "module.hpp"
#include "globals.hpp"
#include "config_utils.hpp"
#include "flit.hpp"
#include "flitchannel.hpp"
#include "credit.hpp"
#include "buffer.hpp"
#include "buffer_state.hpp"
#include <queue>
#include <vector>
#include <unordered_map>

using namespace std;

typedef Channel<Credit> CreditChannel;

class ChipletNetwork;

class InjectController: public TimedModule {
private:
  tRoutingFunction _rf;
  
  // Input and output channels
  FlitChannel * _inject_upstream_flit_channel; // Input
  FlitChannel * _inject_downstream_flit_channel; // Outpout
  CreditChannel * _inject_upstream_credit_channel; // Outpout
  CreditChannel * _inject_downstream_credit_channel; // Input

  FlitChannel * _eject_upstream_flit_channel; // Output
  FlitChannel * _eject_downstream_flit_channel; // Input
  CreditChannel * _eject_upstream_credit_channel; // Input
  CreditChannel * _eject_downstream_credit_channel; // Output
  
  // Local buffer - using Buffer class to manage input flits

  // Used in ReadInputs() stage
  queue<Flit *> _inject_rx_latch; // Downstream RX port flit channel
  queue<Flit *> _eject_rx_req_latch; // Upstream RX port req flit channel
  queue<Flit *> _eject_rx_rsp_latch; // Upstream RX port rsp flit channel
  queue<Flit *> _eject_rx_dat_latch; // Upstream RX port dat flit channel
  
  // Used in Evaluate() stage
  unordered_map<int, deque<Flit*>> _inject_center_buffers;
  
  // Used in WriteOutputs() stage
  queue<Flit *> _inject_tx_latch; // Downstream TX port flit channel
  queue<Flit *> _eject_tx_latch; // Upstream TX port flit channel
  deque<Credit *> _inject_rx_credit_latch; // Downstream RX port credit channel
  deque<Credit *> _eject_rx_credit_latch; // Upstream RX port credit channel

  // Buffer state for buffer
  int _rc_buf_size;
  int _rc_buf_occ;
  
  // Downstream buffer state - using BufferState to track downstream buffer
  unique_ptr<BufferState>  _inject_downstream_buffer_state;
  unique_ptr<BufferState>  _eject_upstream_buffer_state;
  
  // Statistics
  int _packet_size;
  bool _deterministic;

  const Router * _router;

public:
  InjectController(const Configuration& config, ChipletNetwork * parent, string const & name, const Router * router);

  // TimedModule interface implementation
  void ReadInputs();
  void Evaluate();
  void WriteOutputs();
  
  // Channel setup methods
  void SetInjectUpstreamChannel(FlitChannel * channel, CreditChannel * back_channel);
  void SetInjectDownstreamChannel(FlitChannel * channel, CreditChannel * back_channel);
  void SetEjectUpstreamChannel(FlitChannel * channel, CreditChannel * back_channel);
  void SetEjectDownstreamChannel(FlitChannel * channel, CreditChannel * back_channel);

  void returnCredit(deque<Credit *> & credit_latch, int vc);
  void returnCredit(deque<Credit *> & credit_latch, const set<int> & vcs);
};

#endif