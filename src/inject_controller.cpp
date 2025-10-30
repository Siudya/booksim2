// $Id$

/*
 Copyright (c) 2007-2015, Trustees of The Leland Stanford Junior University
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "inject_controller.hpp"
#include "booksim.hpp"
#include "chiplet_network.hpp"
#include "credit.hpp"
#include "flit.hpp"
#include "flitchannel.hpp"
#include "globals.hpp"
#include <cassert>
#include <cstddef>
#include <iostream>
#include <memory>

using namespace std;

void OutboundBuffer::add_flit(Flit *f) {
  if(check_exists(f->pid)) {
    _packets[f->pid].push_back(f);
  } else {
    _packets[f->pid] = deque<Flit*>();
    _packets[f->pid].push_back(f);
    _allows[f->pid] = false;
  }
}

void OutboundBuffer::rm_pkt(int pid) {
  if(check_exists(pid)) {
    _packets.erase(pid);
    _allows.erase(pid);
  } else {
    cout << "OutboundBuffer::rm_pkt: PID " << pid << " not found" << endl;
    assert(false);
  }
}

void OutboundBuffer::output(queue<Flit *> & tx_latch) {
  queue<int> to_remove;
  for(auto &[pid, pkt]: _packets) {
    if(_allows[pid] && !pkt.empty()) {
      if(pkt.back()->tail) {
        for(auto f: pkt) {
          tx_latch.push(f);
          if(f->watch) *gWatchOut << GetSimTime() << " | " << parent->FullName() << " | " << " Sending flit from outbound buffer. |" << *f << endl;
        }
        to_remove.push(pid);
      }
    }
  }
  while(!to_remove.empty()) {
    rm_pkt(to_remove.front());
    to_remove.pop();
  }
}

void OutboundBuffer::allow_pkt(int pid) {
  if(check_exists(pid)) {
    _allows[pid] = true;
  } else {
    cout << "OutboundBuffer::allow_pkt: PID " << pid << " not found" << endl;
    assert(false);
  }
}

InjectController::InjectController(const Configuration &config, ChipletNetwork *network, string const &name, const Router *r):TimedModule(network, name), _inject_upstream_flit_channel(nullptr), _outbound_buffer(this), _router(r) {
  _inject_upstream_flit_channel = nullptr;
  _inject_downstream_flit_channel = nullptr;
  _inject_upstream_credit_channel = nullptr;
  _inject_downstream_credit_channel = nullptr;
  _eject_upstream_flit_channel = nullptr;
  _eject_downstream_flit_channel = nullptr;
  _eject_upstream_credit_channel = nullptr;
  _eject_downstream_credit_channel = nullptr;
  _packet_size = config.GetInt("packet_size");
  _deterministic = config.GetInt("deterministic") == 1;
  _rc_buf_size = config.GetInt("rc_buf_size");
  _rc_buf_occ = 0;
  // Create downstream buffer state tracker
  _inject_downstream_buffer_state = make_unique<BufferState>(config, this, "ij_ds_buf_state");
  _eject_upstream_buffer_state = make_unique<BufferState>(config, this, "ej_us_buf_state");
  string const rf = config.GetStr("routing_function") + "_" + config.GetStr("topology");
  map<string, tRoutingFunction>::const_iterator rf_iter = gRoutingFunctionMap.find(rf);
  if(rf_iter == gRoutingFunctionMap.end()) {
    Error("Invalid routing function: " + rf);
  }
  _rf = rf_iter->second;
}

bool isNeedRequestFlit(const Router *r, const Flit *f) {
  const int cur = r->GetID();
  const int dst = f->dest;
  const int cur_chip = ChipletNetwork::get_chip(cur);
  const int dst_chip = ChipletNetwork::get_chip(dst);
  return dst_chip != cur_chip && f->head;
}

Flit *getRequestFlit(const Router *r, const Flit *f) {
  Flit *res = Flit::New();
  res->type = Flit::OUTBOUND_REQ;
  res->dest = f->loc_dest;
  res->loc_dest = f->loc_dest;
  res->src = r->GetID();
  res->id = f->pid;
  res->pid = f->pid;
  res->head = true;
  res->tail = true;
  res->watch = f->watch;
  res->subnetwork = f->subnetwork;
  res->data = nullptr;
  res->deterministic = false;
  res->vc = gOutboundReqVC;
  res->vc_prealloc = gOutboundReqVC;
  res->cl = f->cl;
  res->size = f->size;
  return res;
}

void InjectController::returnCredit(deque<Credit *> &credit_latch, int vc) {
  for(auto &e: credit_latch) {
    if(e->vc.count(vc) == 0) {
      e->vc.insert(vc);
      return;
    }
  }
  auto c = Credit::New();
  c->vc.insert(vc);
  credit_latch.push_back(c);
}

void InjectController::returnCredit(deque<Credit *> &credit_latch, const set<int> &vcs) {
  for (auto vc : vcs) {
    returnCredit(credit_latch, vc);
  }
}

void InjectController::ReadInputs() {
  // Read flits from input channel
  assert(_inject_upstream_flit_channel != nullptr);
  assert(_inject_downstream_credit_channel != nullptr);
  assert(_eject_downstream_flit_channel != nullptr);
  assert(_eject_upstream_credit_channel != nullptr);
  Flit *f;
  Credit *c;

  f = _inject_upstream_flit_channel->Receive();
  if (f) {
    if (f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                 << "Received flit from inject upstream port. |" << *f << endl;
    }
    _inject_rx_latch.push(f);
  }

  f = _eject_downstream_flit_channel->Receive();
  if (f) {
    if (f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                 << "Received flit from eject downstream port. |" << *f;
    }
    if(f->type == Flit::OUTBOUND_REQ) {
      _eject_rx_req_latch.push(f);
      if (f->watch) *gWatchOut << " type: RC_ALLOC_REQ" << endl;
    } else if(f->type == Flit::OUTBOUND_RSP) {
      _eject_rx_rsp_latch.push(f);
      if (f->watch) *gWatchOut << " type: RC_ALLOC_RSP" << endl;
    } else {
      _eject_rx_dat_latch.push(f);
      if (f->watch) *gWatchOut << " type: RC_ALLOC_DAT" << endl;
    }
  }


  c = _inject_downstream_credit_channel->Receive();
  if (c) {
    _inject_downstream_buffer_state->ProcessCredit(c);
    c->Free();
  }

  c = _eject_upstream_credit_channel->Receive();
  if (c) {
    _eject_upstream_buffer_state->ProcessCredit(c);
    c->Free();
  }
}

void InjectController::Evaluate() {
  assert(_rc_buf_occ >= 0);
  assert(_rc_buf_occ <= _rc_buf_size);
  if (!_inject_rx_latch.empty()) {
    Flit *f = _inject_rx_latch.front();
    if (isNeedRequestFlit(_router, f)) {
      f->to_rc_buffer = !(f->loc_dest == _router->GetID());
      auto rf = getRequestFlit(_router, f);
      _rf(_router, rf, _router->NumInputs() -1, &rf->la_route_set, false);
      _rf(_router, f, _router->NumInputs() -1, &f->la_route_set, false);
      if (rf->watch) *gWatchOut << GetSimTime() << " | " << FullName() << " | " << " Sending RC REQ flit. |" << *rf << endl;
      if(rf->dest == _router->GetID()) {
        _eject_rx_req_latch.push(rf); 
      } else {
        _inject_tx_latch.push(rf);
      }
    }
    _outbound_buffer.add_flit(f);
    if(f->traffic_type != Flit::OUTBOUND && f->head) _outbound_buffer.allow_pkt(f->pid);
    returnCredit(_inject_rx_credit_latch, f->vc);
    _inject_rx_latch.pop();
  }
  
  if(!_eject_rx_req_latch.empty()) {
    Flit *f = _eject_rx_req_latch.front();
    if(_rc_buf_occ + f->size < _rc_buf_size) {
      assert(f->vc == gOutboundReqVC);
      if(f->src != _router->GetID()) returnCredit(_eject_rx_credit_latch, gOutboundReqVC);
      _eject_rx_req_latch.pop();
      _rc_buf_occ += f->size;
      f->type = Flit::OUTBOUND_RSP;
      f->dest = f->src;
      f->loc_dest = f->src;
      f->src = _router->GetID();
      f->vc = gOutboundRspVC;
      _rf(_router, f, _router->NumInputs() -1, &f->la_route_set, false);
      if(f->dest == _router->GetID()) {
        _eject_rx_rsp_latch.push(f);
      } else {
        _inject_tx_latch.push(f);
      }
      if(f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                   << " Received RC REQ flit. Sending RC RSP flit. |" << *f << endl;
      }
    } else {
      if(f->watch) *gWatchOut << GetSimTime() << " | " << FullName() << " | " << " RC buffer is full. Waiting... | " << *f << endl;
    }
  }

  if(!_eject_rx_rsp_latch.empty()) {
    Flit *f = _eject_rx_rsp_latch.front();
    assert(f->vc == gOutboundRspVC);
    _outbound_buffer.allow_pkt(f->pid);
    if (f->watch) *gWatchOut << GetSimTime() << " | " << FullName() << " | " << " Received RC RSP flit. |" << *f << endl;
    if(f->src != _router->GetID()) returnCredit(_eject_rx_credit_latch, gOutboundRspVC);
    _eject_rx_rsp_latch.pop();
    f->Free();
  }

  if(!_eject_rx_dat_latch.empty()) {
    Flit *f = _eject_rx_dat_latch.front();
    returnCredit(_eject_rx_credit_latch, f->vc);
    _eject_rx_dat_latch.pop();
    if(f->head && f->to_rc_buffer) {
      assert(f->traffic_type == Flit::OUTBOUND);
      f->to_rc_buffer = false;
      _rf(_router, f, _router->NumInputs() -1, &f->la_route_set, false);
      _outbound_buffer.add_flit(f);
      _outbound_buffer.allow_pkt(f->pid);
    } else if(_outbound_buffer.check_exists(f->pid)) {
      _outbound_buffer.add_flit(f);
    } else {
      _eject_tx_latch.push(f);
    }
    if (f->watch) *gWatchOut << GetSimTime() << " | " << FullName() << " | " << " Received RC DAT flit. |" << *f << endl;
  }
}

void InjectController::WriteOutputs() {
  assert(_inject_downstream_flit_channel != nullptr);
  assert(_inject_upstream_credit_channel != nullptr);
  assert(_eject_upstream_flit_channel != nullptr);
  assert(_eject_downstream_credit_channel != nullptr);
  
  _outbound_buffer.output(_inject_tx_latch);
  if (!_inject_tx_latch.empty()) {
    Flit *f = _inject_tx_latch.front();
    auto buf = _inject_downstream_buffer_state.get();
    if(!buf->IsAvailableFor(f->vc) && f->head) {
      if(f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                   << " Inject TX VC " << f->vc << " is busy." << endl;
         buf->Display(*gWatchOut);
      }
    } else if(buf->IsFullFor(f->vc)) {
      if(f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                   << " Inject TX VC " << f->vc << " is full." << endl;
      }
    } else {
      if(f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                   << " Sending Inject flit to inject downstream port. |" << *f << endl;
      }
      if(f->head) buf->TakeBuffer(f->vc);
      _inject_downstream_flit_channel->Send(f);
      buf->SendingFlit(f);
      _inject_tx_latch.pop();
      if(f->src != _router->GetID()) _rc_buf_occ--;
    }
  }
  if (!_eject_tx_latch.empty()) {
    Flit *f = _eject_tx_latch.front();
    auto buf = _eject_upstream_buffer_state.get();
    if(!buf->IsAvailableFor(f->vc) && f->head) {
      if(f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                   << "  Eject Output VC " << f->vc << " is busy." << endl;
      }
    } else if(buf->IsFullFor(f->vc)) {
      if(f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                   << "  Eject Output VC " << f->vc << " is full." << endl;
      }
    } else {
      if(f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                   << " Sending Eject flit to eject upstream port. |" << *f << endl;
      }
      if(f->head) buf->TakeBuffer(f->vc);
      _eject_upstream_flit_channel->Send(f);
      buf->SendingFlit(f);
      _eject_tx_latch.pop();
    }
  }
  if(!_inject_rx_credit_latch.empty()) {
    _inject_upstream_credit_channel->Send(_inject_rx_credit_latch.front());
    _inject_rx_credit_latch.pop_front();
  }
  if(!_eject_rx_credit_latch.empty()) {
    _eject_downstream_credit_channel->Send(_eject_rx_credit_latch.front());
    _eject_rx_credit_latch.pop_front();
  }
}

void InjectController::SetInjectUpstreamChannel(FlitChannel * channel, CreditChannel * back_channel) { 
  _inject_upstream_flit_channel = channel;
  _inject_upstream_credit_channel = back_channel;
  channel->SetSink( _router, _router->NumInputs() - 1);
}

void InjectController::SetInjectDownstreamChannel(FlitChannel * channel, CreditChannel * back_channel) {
  _inject_downstream_flit_channel = channel;
  _inject_downstream_credit_channel = back_channel;
}

void InjectController::SetEjectUpstreamChannel(FlitChannel * channel, CreditChannel * back_channel) {
  _eject_upstream_flit_channel = channel;
  _eject_upstream_credit_channel = back_channel;
}

void InjectController::SetEjectDownstreamChannel(FlitChannel * channel, CreditChannel * back_channel) {
  _eject_downstream_flit_channel = channel;
  _eject_downstream_credit_channel = back_channel;
  channel->SetSink( _router, _router->NumInputs() - 1);
}