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

/*routefunc.cpp
 *
 *This is where most of the routing functions reside. Some of the topologies
 *has their own "register routing functions" which must be called to access
 *those routing functions. 
 *
 *After writing a routing function, don't forget to register it. The reg 
 *format is rfname_topologyname. 
 *
 */

#include <map>
#include <cstdlib>
#include <cassert>

#include "booksim.hpp"
#include "routefunc.hpp"
#include "kncube.hpp"
#include "random_utils.hpp"
#include "misc_utils.hpp"
#include "chiplet_network.hpp"



map<string, tRoutingFunction> gRoutingFunctionMap;

/* Global information used by routing functions */

int gNumVCs;

/* Add more functions here
 *
 */

// ============================================================
//  Balfour-Schultz
int gReadReqBeginVC, gReadReqEndVC;
int gWriteReqBeginVC, gWriteReqEndVC;
int gReadReplyBeginVC, gReadReplyEndVC;
int gWriteReplyBeginVC, gWriteReplyEndVC;
int gOutboundReqVC;
int gOutboundRspVC;
int gDataBeginVC, gDataEndVC;
bool gUseRCBuffer;

//
// End Balfour-Schultz
//=============================================================

//=============================================================

int dor_next_mesh( int cur, int dest, bool descending )
{
  if ( cur == dest ) {
    return 2*gN;  // Eject
  }

  int dim_left;

  if(descending) {
    for ( dim_left = ( gN - 1 ); dim_left > 0; --dim_left ) {
      if ( ( cur * gK / gNodes ) != ( dest * gK / gNodes ) ) { break; }
      cur = (cur * gK) % gNodes; dest = (dest * gK) % gNodes;
    }
    cur = (cur * gK) / gNodes;
    dest = (dest * gK) / gNodes;
  } else {
    for ( dim_left = 0; dim_left < ( gN - 1 ); ++dim_left ) {
      if ( ( cur % gK ) != ( dest % gK ) ) { break; }
      cur /= gK; dest /= gK;
    }
    cur %= gK;
    dest %= gK;
  }

  if ( cur < dest ) {
    return 2*dim_left;     // Right
  } else {
    return 2*dim_left + 1; // Left
  }
}

//=============================================================

void dim_order_mesh( const Router *r, const Flit *f, int in_channel, OutputSet *outputs, bool inject )
{
  int out_port = inject ? -1 : dor_next_mesh( r->GetID( ), f->dest, false );
  
  int vcBegin = 0, vcEnd = gNumVCs-1;
  if ( f->type == Flit::READ_REQUEST ) {
    vcBegin = gReadReqBeginVC;
    vcEnd = gReadReqEndVC;
  } else if ( f->type == Flit::WRITE_REQUEST ) {
    vcBegin = gWriteReqBeginVC;
    vcEnd = gWriteReqEndVC;
  } else if ( f->type ==  Flit::READ_REPLY ) {
    vcBegin = gReadReplyBeginVC;
    vcEnd = gReadReplyEndVC;
  } else if ( f->type ==  Flit::WRITE_REPLY ) {
    vcBegin = gWriteReplyBeginVC;
    vcEnd = gWriteReplyEndVC;
  }
  assert(((f->vc >= vcBegin) && (f->vc <= vcEnd)) || (inject && (f->vc < 0)));

  if ( !inject && f->watch ) {
    *gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
	       << "Adding VC range [" 
	       << vcBegin << "," 
	       << vcEnd << "]"
	       << " at output port " << out_port
	       << " for flit " << f->id
	       << " (input port " << in_channel
	       << ", destination " << f->dest << ")"
	       << "." << endl;
  }
  
  outputs->Clear();

  outputs->AddRange( out_port, vcBegin, vcEnd );
}

//=============================================================
// Helper function for ChipletNetwork routing
int dor_chiplet_port_decision(const Router *r, const Flit *f) {
  int cur = r->GetID();
  int dst = f->dest;
  int loc_dst = f->loc_dest;

  int cur_x = ChipletNetwork::get_x(cur);
  int cur_y = ChipletNetwork::get_y(cur);
  int dest_x = ChipletNetwork::get_x(loc_dst);
  int dest_y = ChipletNetwork::get_y(loc_dst);
  
  if (cur == dst) {
    return ChipletNetwork::local_port;
  } else if(cur == loc_dst) {
    assert(r->IsBoundaryRouter());
    return f->to_rc_buffer ? ChipletNetwork::local_port : r->GetD2DPort();
  } else if(cur_x != dest_x) {
    return (cur_x < dest_x) ? ChipletNetwork::right_port : ChipletNetwork::left_port;
  } else if(cur_y != dest_y) {
    return (cur_y < dest_y) ? ChipletNetwork::down_port : ChipletNetwork::up_port;
  } else {
    assert(false);
    return -1;
  }
}

// Call this function when flit crossing chiplet boundary or inject to the network
void set_traffic_type(const Router *r, const Flit *f) {
  const int cur = r->GetID();
  const int src = f->src;
  const int dst = f->dest;
  const int src_chip = ChipletNetwork::get_chip(src);
  const int cur_chip = ChipletNetwork::get_chip(cur);
  const int dst_chip = ChipletNetwork::get_chip(dst);
  auto network = dynamic_cast<ChipletNetwork*>(r->GetNetwork());

  if(src_chip == cur_chip && src_chip == dst_chip) {
    f->traffic_type = Flit::LOCAL;
    f->loc_dest = dst;
  } else if(src_chip != cur_chip && dst_chip == cur_chip) {
    f->traffic_type = Flit::INBOUND;
    f->loc_dest = dst;
  } else if(src_chip != cur_chip && dst_chip != cur_chip) {
    f->traffic_type = Flit::TRANSIT;
    f->loc_dest = network->get_boundary_router(cur, dst);
  } else if(src_chip == cur_chip && cur_chip != dst_chip) {
    f->traffic_type = Flit::OUTBOUND;
    f->loc_dest = network->get_boundary_router(cur, dst);
  } else {
    assert(false);
    f->traffic_type = Flit::LOCAL;
    f->loc_dest = dst;
  }
}

typedef void (*va_strategy_func)(const Router *r, const Flit *f, const int out_port, const int vc_begin, const int vc_end, int &vc_sel_begin, int &vc_sel_end);

void va_vda(const Router *r, const Flit *f, const int out_port, const int vc_begin, const int vc_end, int &vc_sel_begin, int &vc_sel_end) {
  int vc_num = (vc_end - vc_begin + 1);
  assert(vc_num % 2 == 0);
  int vn0_end = vc_num / 2 + vc_begin - 1;  // vn0_end is the last VC of vn0
  int vn1_begin = vn0_end + 1;
  int vn1_end = vc_end;
  if(f->traffic_type == Flit::OUTBOUND) {
    vc_sel_begin = vn1_begin;
    vc_sel_end = vn1_end;
  } else {
    vc_sel_begin = vc_begin;
    vc_sel_end = vc_end;
  }
}

void va_red(const Router *r, const Flit *f, const int out_port, const int vc_begin, const int vc_end, int &vc_sel_begin, int &vc_sel_end) {
  int vc_num = (vc_end - vc_begin + 1);
  assert(vc_num % 2 == 0);
  int vn0_begin = vc_begin;
  int vn0_end = vc_num / 2 + vc_begin - 1;  // vn0_end is the last VC of vn0
  int vn1_begin = vn0_end + 1;
  int vn1_end = vc_end;
  if(vn1_begin <= f->vc && f->vc < vn1_end) { // Flit in VN1 should not route to VN0
    vc_sel_begin = vn1_begin;
    vc_sel_end = vn1_end;
  } else if(f->traffic_type == Flit::INBOUND) { // Inbound flit should route to VN1
    vc_sel_begin = vn1_begin;
    vc_sel_end = vn1_end;
  } else if(f->traffic_type == Flit::OUTBOUND) { // Outbound flit should route to VN0
    vc_sel_begin = vn0_begin;
    vc_sel_end = vn0_end;
  } else {
    vc_sel_begin = vc_begin;
    vc_sel_end = vc_end;
  }
}

void va_mvn(const Router *r, const Flit *f, const int out_port, const int vc_begin, const int vc_end, int &vc_sel_begin, int &vc_sel_end) {
  int vc_num = (vc_end - vc_begin + 1);
  assert(vc_num % 2 == 0);
  int vn0_end = vc_num / 2 + vc_begin - 1;  // vn0_end is the last VC of vn0
  int vn1_begin = vn0_end + 1;
  int vn1_end = vc_end;
  if(r->CheckOutputMayBeDeadlock(out_port) && f->traffic_type == Flit::OUTBOUND) {
    vc_sel_begin = vn1_begin;
    vc_sel_end = vn1_end;
  } else {
    vc_sel_begin = vc_begin;
    vc_sel_end = vc_end;
  }
}

void va_rc(const Router *r, const Flit *f, const int out_port, const int vc_begin, const int vc_end, int &vc_sel_begin, int &vc_sel_end) {
  vc_sel_begin = vc_begin;
  vc_sel_end = vc_end;
}

template<va_strategy_func VAStrategy>
void dor_chiplet(const Router *r, const Flit *f, int in_channel, OutputSet *outputs, bool inject) {
  if(inject || (r->IsBoundaryRouter() && r->GetD2DPort() == in_channel)) set_traffic_type(r, f);
  int out_port = inject ? -1 : dor_chiplet_port_decision(r, f);
  
  int vcBegin, vcEnd;
  if (f->type == Flit::OUTBOUND_REQ) {
    vcBegin = gOutboundReqVC;
    vcEnd = gOutboundReqVC;
  } else if (f->type == Flit::OUTBOUND_RSP) {
    vcBegin = gOutboundRspVC;
    vcEnd = gOutboundRspVC;
  } else {
    vcBegin = gDataBeginVC;
    vcEnd = gDataEndVC;
  }
  assert(((f->vc >= vcBegin) && (f->vc <= vcEnd)) || (inject && (f->vc < 0)));

  // Select the legal VC range based on the VA strategy
  if(out_port != ChipletNetwork::local_port && !inject) VAStrategy(r, f, out_port, vcBegin, vcEnd, vcBegin, vcEnd);

  // If deterministic, select the VC based on the flit's input VC
  const int candidate_vc_num = (vcEnd - vcBegin + 1);
  if(f->deterministic && !inject) {
    vcBegin = vcBegin + (f->vc_prealloc % candidate_vc_num);
    vcEnd = vcBegin;
  }
  if (f->watch) {
    *gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
               << "Adding VC range [" 
               << vcBegin << "," 
               << vcEnd << "]"
               << " at output port " << out_port
               << " for flit " << f->id
               << " (input port " << in_channel
               << ", local destination " << f->loc_dest
               << ", destination " << f->dest << ")"
               << " [Traffic: " << Flit::GetTrafficTypeString(f->traffic_type) << "]"
               << "." << endl;
  }
  
  outputs->Clear();
  outputs->AddRange(out_port, vcBegin, vcEnd);
}

//=============================================================

void InitializeRoutingMap( const Configuration & config )
{

  gNumVCs = config.GetInt( "num_vcs" );

  //
  // traffic class partitions
  //
  gReadReqBeginVC    = config.GetInt("read_request_begin_vc");
  if(gReadReqBeginVC < 0) {
    gReadReqBeginVC = 0;
  }
  gReadReqEndVC      = config.GetInt("read_request_end_vc");
  if(gReadReqEndVC < 0) {
    gReadReqEndVC = gNumVCs / 2 - 1;
  }
  gWriteReqBeginVC   = config.GetInt("write_request_begin_vc");
  if(gWriteReqBeginVC < 0) {
    gWriteReqBeginVC = 0;
  }
  gWriteReqEndVC     = config.GetInt("write_request_end_vc");
  if(gWriteReqEndVC < 0) {
    gWriteReqEndVC = gNumVCs / 2 - 1;
  }
  gReadReplyBeginVC  = config.GetInt("read_reply_begin_vc");
  if(gReadReplyBeginVC < 0) {
    gReadReplyBeginVC = gNumVCs / 2;
  }
  gReadReplyEndVC    = config.GetInt("read_reply_end_vc");
  if(gReadReplyEndVC < 0) {
    gReadReplyEndVC = gNumVCs - 1;
  }
  gWriteReplyBeginVC = config.GetInt("write_reply_begin_vc");
  if(gWriteReplyBeginVC < 0) {
    gWriteReplyBeginVC = gNumVCs / 2;
  }
  gWriteReplyEndVC   = config.GetInt("write_reply_end_vc");
  if(gWriteReplyEndVC < 0) {
    gWriteReplyEndVC = gNumVCs - 1;
  }
  
  gUseRCBuffer = config.GetInt("use_rc_buffer") > 0;
  if(gUseRCBuffer) assert(gNumVCs > 2);
  gDataBeginVC = 0;
  gDataEndVC = gUseRCBuffer ? gNumVCs - 3 : gNumVCs - 1;
  gOutboundReqVC = gNumVCs - 2;
  gOutboundRspVC = gNumVCs - 1;

  /* Register routing functions here */

  // ===================================================
  // Balfour-Schultz
  gRoutingFunctionMap["dor_mesh"]            = &dim_order_mesh;
  gRoutingFunctionMap["dim_order_mesh"]  = &dim_order_mesh;
  // End Balfour-Schultz
  // ===================================================
  
  // ===================================================
  // Chiplet routing functions
  gRoutingFunctionMap["dor_vda_chiplet_twin"] = &dor_chiplet<va_vda>;
  gRoutingFunctionMap["dor_vda_chiplet_mesh"] = &dor_chiplet<va_vda>;
  gRoutingFunctionMap["dor_vda_chiplet_p2p"] = &dor_chiplet<va_vda>;
  gRoutingFunctionMap["dor_red_chiplet_twin"] = &dor_chiplet<va_red>;
  gRoutingFunctionMap["dor_red_chiplet_mesh"] = &dor_chiplet<va_red>;
  gRoutingFunctionMap["dor_red_chiplet_p2p"] = &dor_chiplet<va_red>;
  gRoutingFunctionMap["dor_mvn_chiplet_twin"] = &dor_chiplet<va_mvn>;
  gRoutingFunctionMap["dor_mvn_chiplet_mesh"] = &dor_chiplet<va_mvn>;
  gRoutingFunctionMap["dor_mvn_chiplet_p2p"] = &dor_chiplet<va_mvn>;
  gRoutingFunctionMap["dor_rc_chiplet_twin"] = &dor_chiplet<va_rc>;
  gRoutingFunctionMap["dor_rc_chiplet_mesh"] = &dor_chiplet<va_rc>;
  gRoutingFunctionMap["dor_rc_chiplet_p2p"] = &dor_chiplet<va_rc>;
  // ===================================================
}
