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

// ============================================================
//  Mesh - adatpive XY,YX Routing 
//         pick xy or yx min routing adaptively at the source router
// ===

int dor_next_mesh( int cur, int dest, bool descending = false );

void adaptive_xy_yx_mesh( const Router *r, const Flit *f, 
		 int in_channel, OutputSet *outputs, bool inject )
{
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

  int out_port;

  if(inject) {

    out_port = -1;

  } else if(r->GetID() == f->dest) {

    // at destination router, we don't need to separate VCs by dim order
    out_port = 2*gN;

  } else {

    //each class must have at least 2 vcs assigned or else xy_yx will deadlock
    int const available_vcs = (vcEnd - vcBegin + 1) / 2;
    assert(available_vcs > 0);
    
    int out_port_xy = dor_next_mesh( r->GetID(), f->dest, false );
    int out_port_yx = dor_next_mesh( r->GetID(), f->dest, true );

    // Route order (XY or YX) determined when packet is injected
    //  into the network, adaptively
    bool x_then_y;
    if(in_channel < 2*gN){
      x_then_y =  (f->vc < (vcBegin + available_vcs));
    } else {
      int credit_xy = r->GetUsedCredit(out_port_xy);
      int credit_yx = r->GetUsedCredit(out_port_yx);
      if(credit_xy > credit_yx) {
	x_then_y = false;
      } else if(credit_xy < credit_yx) {
	x_then_y = true;
      } else {
	x_then_y = (RandomInt(1) > 0);
      }
    }
    
    if(x_then_y) {
      out_port = out_port_xy;
      vcEnd -= available_vcs;
    } else {
      out_port = out_port_yx;
      vcBegin += available_vcs;
    }

  }

  outputs->Clear();

  outputs->AddRange( out_port , vcBegin, vcEnd );
  
}

void xy_yx_mesh( const Router *r, const Flit *f, 
		 int in_channel, OutputSet *outputs, bool inject )
{
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

  int out_port;

  if(inject) {

    out_port = -1;

  } else if(r->GetID() == f->dest) {

    // at destination router, we don't need to separate VCs by dim order
    out_port = 2*gN;

  } else {

    //each class must have at least 2 vcs assigned or else xy_yx will deadlock
    int const available_vcs = (vcEnd - vcBegin + 1) / 2;
    assert(available_vcs > 0);

    // Route order (XY or YX) determined when packet is injected
    //  into the network
    bool x_then_y = ((in_channel < 2*gN) ?
		     (f->vc < (vcBegin + available_vcs)) :
		     (RandomInt(1) > 0));

    if(x_then_y) {
      out_port = dor_next_mesh( r->GetID(), f->dest, false );
      vcEnd -= available_vcs;
    } else {
      out_port = dor_next_mesh( r->GetID(), f->dest, true );
      vcBegin += available_vcs;
    }

  }

  outputs->Clear();

  outputs->AddRange( out_port , vcBegin, vcEnd );
  
}

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
  int out_port = inject ? -1 : dor_next_mesh( r->GetID( ), f->dest );
  
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

  /* Register routing functions here */

  // ===================================================
  // Balfour-Schultz
  gRoutingFunctionMap["dor_mesh"]            = &dim_order_mesh;
  gRoutingFunctionMap["xy_yx_mesh"]          = &xy_yx_mesh;
  gRoutingFunctionMap["adaptive_xy_yx_mesh"]          = &adaptive_xy_yx_mesh;
  // End Balfour-Schultz
  // ===================================================

  gRoutingFunctionMap["dim_order_mesh"]  = &dim_order_mesh;
}
