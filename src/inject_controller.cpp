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
#include <cassert>
#include <cstddef>
#include <iostream>
#include <memory>

using namespace std;

InjectController::InjectController(const Configuration &config, ChipletNetwork *network, string const &name, const Router *r):TimedModule(network, name), _inject_upstream_flit_channel(nullptr), _router(r) {
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
  res->cl = -1;
  return res;
}

void InjectController::returnCredit(queue<Credit *> &credit_latch, int vc) {
  if (credit_latch.empty()) {
    auto c = Credit::New();
    c->vc.insert(vc);
    credit_latch.push(c);
  } else {
    credit_latch.front()->vc.insert(vc);
  }
}

void InjectController::returnCredit(queue<Credit *> &credit_latch, const set<int> &vcs) {
  for (auto vc : vcs) {
    returnCredit(credit_latch, vc);
  }
}

void InjectController::ReadInputs() {
  // Read flits from input channel
  assert(_inject_upstream_flit_channel != nullptr);
  assert(_eject_downstream_flit_channel != nullptr);
  Flit *f;

  f = _inject_upstream_flit_channel->Receive();
  if (f) {
    if (f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                 << "Received flit " << f->id << " (packet " << f->pid << ")"
                 << " from inject upstream port."
                 << " vc: " << f->vc << " type: " << Flit::GetTrafficTypeString(f->traffic_type) << endl;
    }
    _inject_rx_latch.push(f);
  }

  f = _eject_downstream_flit_channel->Receive();
  if (f) {
    if (f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                 << "Received flit " << f->id << " (packet " << f->pid << ")"
                 << " from eject downstream port."
                 << " vc: " << f->vc << " type: " << f->type << endl;
    }
    _eject_rx_latch.push(f);
  }

  // Read credits from credit channel
  if (_inject_downstream_credit_channel) {
    auto c = _inject_downstream_credit_channel->Receive();
    if (c) {
      _inject_downstream_buffer_state->ProcessCredit(c);
      c->Free();
    }
  }

  if (_eject_upstream_credit_channel) {
    auto c = _eject_upstream_credit_channel->Receive();
    if (c) {
      _eject_upstream_buffer_state->ProcessCredit(c);
      c->Free();
    }
  }
}

void InjectController::Evaluate() {
  if (!_inject_rx_latch.empty()) {
    Flit *f = _inject_rx_latch.front();
    const int vc = f->vc;
    if (isNeedRequestFlit(_router, f)) {
      auto rf = getRequestFlit(_router, f);
      _rf(_router, rf, _router->NumInputs() -1, &rf->la_route_set, false);
      if (rf->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                   << " Sending RC REQ flit " << rf->id << " to inject downstream port."
                   << " src: " << rf->src << " dest: " << rf->dest << " vc: " << rf->vc
                   << " pid: " << rf->pid << " vc: " << rf->vc << " type: " << rf->type << endl;
      }
      _inject_tx_latch.push(rf);
      _inject_center_buffers[f->pid] = queue<Flit *>();
      _inject_center_buffers[f->pid].push(f);
      if (f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                   << " Added flit " << f->id << " to inject center buffers."
                   << " pid: " << f->pid << " vc: " << f->vc << " type: " << Flit::GetTrafficTypeString(f->traffic_type) << endl;
      }
    } else if (_inject_center_buffers.count(f->pid) > 0) {
      _inject_center_buffers[f->pid].push(f);
    } else {
      _inject_tx_latch.push(f);
    }
    _inject_rx_latch.pop();
    returnCredit(_inject_rx_credit_latch, vc);
  }

  if (!_eject_rx_latch.empty()) {
    Flit *f = _eject_rx_latch.front();
    if (f->type == Flit::OUTBOUND_REQ) {
      assert(f->vc == gOutboundReqVC);
      if (_rc_buf_occ < _rc_buf_size) {
        _rc_buf_occ++;
        f->type = Flit::OUTBOUND_RSP;
        f->dest = f->src;
        f->loc_dest = f->src;
        f->src = _router->GetID();
        f->vc = gOutboundRspVC;
        _rf(_router, f, _router->NumInputs() -1, &f->la_route_set, false);
        _inject_tx_latch.push(f);
        returnCredit(_eject_rx_credit_latch, gOutboundReqVC);
        _eject_rx_latch.pop();
        if(f->watch) {
          *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                     << " RC buffer is available. Sending RC RSP flit " << f->id << " to inject downstream port."
                     << " pid: " << f->pid
                     << " src: " << f->src << " dest: " << f->dest << " vc: " << f->vc
                     << " traffic type: " << Flit::GetTrafficTypeString(f->traffic_type) 
                     << " flit type: " << f->type << endl;
        }
      } else {
        if (f->watch) {
          *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                     << " RC buffer is full, waiting for available entry. RC REQ flit " << f->id << " (packet " << f->pid << ")."
                     << " pid: " << f->pid 
                     << " src: " << f->src << " dest: " << f->dest << " vc: " << f->vc
                     << " traffic type: " << Flit::GetTrafficTypeString(f->traffic_type) 
                     << " flit type: " << f->type << endl;
        }
      }
    } else if (f->type == Flit::OUTBOUND_RSP) {
      assert(_inject_center_buffers.count(f->pid) > 0);
      assert(f->vc == gOutboundRspVC);
      assert(r->IsBoundaryRouter());
      if (_inject_center_buffers[f->pid].size() == _packet_size) {
        if (f->watch) {
          *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                     << " Received RC RSP flit " << f->id << " from eject downstream port."
                     << " pid: " << f->pid 
                     << " src: " << f->src << " dest: " << f->dest << " vc: " << f->vc
                     << " traffic type: " << Flit::GetTrafficTypeString(f->traffic_type) 
                     << " flit type: " << f->type << endl;
        }
        while (!_inject_center_buffers[f->pid].empty()) {
          auto flit = _inject_center_buffers[f->pid].front();
          assert(flit->traffic_type == Flit::OUTBOUND);
          flit->to_rc_buffer = true;
          _inject_tx_latch.push(flit);
          returnCredit(_eject_rx_credit_latch, gOutboundRspVC);
          _inject_center_buffers[f->pid].pop();
        }
        _inject_center_buffers.erase(f->pid);
        f->Free();
        returnCredit(_eject_rx_credit_latch, f->vc);
        _eject_rx_latch.pop();
      }
    } else if(f->to_rc_buffer) {
      assert(f->traffic_type == Flit::OUTBOUND);
      if (f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                   << " Received RC DAT flit " << f->id << " from eject downstream port."
                   << " pid: " << f->pid 
                   << " src: " << f->src << " loc_dest: " << f->loc_dest
                   << " dest: " << f->dest << " vc: " << f->vc
                   << " traffic type: " << Flit::GetTrafficTypeString(f->traffic_type) 
                   << " flit type: " << f->type << endl;
      }
      f->to_rc_buffer = false;
      _rf(_router, f, _router->NumInputs() -1, &f->la_route_set, false);
      _inject_tx_latch.push(f);
      returnCredit(_eject_rx_credit_latch, f->vc);
      _eject_rx_latch.pop();
    } else {
      _eject_tx_latch.push(f);
      returnCredit(_eject_rx_credit_latch, f->vc);
      _eject_rx_latch.pop();
    }
  }
}

void InjectController::WriteOutputs() {
  if (!_inject_tx_latch.empty()) {
    Flit *f = _inject_tx_latch.front();
    auto buf = _inject_downstream_buffer_state.get();
    if(!buf->IsAvailableFor(f->vc)) {
      if(f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                   << " Inject Output VC " << f->vc << " is busy." << endl;
      }
    } else if(buf->IsFullFor(f->vc)) {
      if(f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                   << " Inject Output VC " << f->vc << " is full." << endl;
      }
    } else {
      if(f->watch) {
        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                   << " Sending Inject flit " << f->id << " to inject downstream port."
                   << " pid: " << f->pid
                   << " src: " << f->src << " loc_dest: " << f->loc_dest
                   << " dest: " << f->dest << " vc: " << f->vc
                   << " to_rc_buffer: " << f->to_rc_buffer
                   << " traffic type: " << Flit::GetTrafficTypeString(f->traffic_type)
                   << " flit type: " << f->type << endl;
      }
      if(f->head) buf->TakeBuffer(f->vc);
      _inject_downstream_flit_channel->Send(f);
      buf->SendingFlit(f);
      _inject_tx_latch.pop();
    }
  }
  if (!_eject_tx_latch.empty()) {
    Flit *f = _eject_tx_latch.front();
    auto buf = _eject_upstream_buffer_state.get();
    if(!buf->IsAvailableFor(f->vc)) {
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
                   << "  Sending Eject flit " << f->id << " to eject upstream port."
                   << " src: " << f->src << " dest: " << f->dest << " vc: " << f->vc
                   << " traffic type: " << Flit::GetTrafficTypeString(f->traffic_type)
                   << " flit type: " << f->type << endl;
      }
      if(f->head) buf->TakeBuffer(f->vc);
      _eject_upstream_flit_channel->Send(f);
      buf->SendingFlit(f);
      if(f->tail && f->traffic_type == Flit::OUTBOUND) _rc_buf_occ--;
      _eject_tx_latch.pop();
    }
  }
  if(!_inject_rx_credit_latch.empty()) {
    auto c = _inject_rx_credit_latch.front();
    _inject_rx_credit_latch.pop();
    _inject_upstream_credit_channel->Send(c);
  }
  if(!_eject_rx_credit_latch.empty()) {
    auto c = _eject_rx_credit_latch.front();
    _eject_rx_credit_latch.pop();
    _eject_downstream_credit_channel->Send(c);
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