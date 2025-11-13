#include "booksim.hpp"
#include "chiplet_network.hpp"
#include <sstream>
#include "inject_controller.hpp"

ChipletNetwork::ChipletNetwork (const Configuration &config, const string & name):Network( config, name ) {
  _d2d_lat = config.GetInt("d2d_latency");
  _num_vcs = config.GetInt("num_vcs");
}

int ChipletNetwork::left_node(int node_id) {
  int self_chip = get_chip(node_id);
  int self_x = get_x(node_id);
  int self_y = get_y(node_id);
  int left_node_x = (self_x - 1 + x_len) % x_len;
  int left_node_y = self_y;
  return get_node_id(self_chip, left_node_y, left_node_x);
}

int ChipletNetwork::right_node(int node_id) {
  int self_chip = get_chip(node_id);
  int self_x = get_x(node_id);
  int self_y = get_y(node_id);
  int right_node_x = (self_x + 1) % x_len;
  int right_node_y = self_y;
  return get_node_id(self_chip, right_node_y, right_node_x);
}

int ChipletNetwork::up_node(int node_id) {
  int self_chip = get_chip(node_id);
  int self_x = get_x(node_id);
  int self_y = get_y(node_id);
  int up_node_x = self_x;
  int up_node_y = (self_y - 1 + y_len) % y_len;
  return get_node_id(self_chip, up_node_y, up_node_x);
}

int ChipletNetwork::down_node(int node_id) {
  int self_chip = get_chip(node_id);
  int self_x = get_x(node_id);
  int self_y = get_y(node_id);
  int down_node_x = self_x;
  int down_node_y = (self_y + 1) % y_len;
  return get_node_id(self_chip, down_node_y, down_node_x);
}

void ChipletNetwork::node_conn(int node, int in_chn, int out_chn, int in_lat, int out_lat) {
  _routers[node]->AddInputChannel( _chan[in_chn].get(), _chan_cred[in_chn].get() );
  _chan[in_chn]->SetLatency( in_lat );
  _chan_cred[in_chn]->SetLatency( in_lat );

  _routers[node]->AddOutputChannel( _chan[out_chn].get(), _chan_cred[out_chn].get() );
  _chan[out_chn]->SetLatency( out_lat );
  _chan_cred[out_chn]->SetLatency( out_lat );
}

void ChipletNetwork::node_conn_d2d(int n0, int n1, int n0_port, int n1_port) {

  auto r0 = _routers.at(n0).get();
  auto r1 = _routers.at(n1).get();

  r0->AlterInputChannel(n0_port, r1->GetOutputChannel(n1_port), r1->GetOutputCreditChannel(n1_port), true);
  r1->AlterInputChannel(n1_port, r0->GetOutputChannel(n0_port), r0->GetOutputCreditChannel(n0_port), true);

  r0->GetOutputChannel(n0_port)->SetLatency(_d2d_lat);
  r1->GetOutputChannel(n1_port)->SetLatency(_d2d_lat);
  r0->GetOutputCreditChannel(n0_port)->SetLatency(_d2d_lat);
  r1->GetOutputCreditChannel(n1_port)->SetLatency(_d2d_lat);

  r0->SetBufferSize(n0_port, _d2d_lat * 2 + 1);
  r1->SetBufferSize(n1_port, _d2d_lat * 2 + 1);
  r0->SetWaitForTail(n0_port, false);
  r1->SetWaitForTail(n1_port, false);
  r0->SetBufferStatistics(n0_port, false);
  r1->SetBufferStatistics(n1_port, false);
}

void ChipletNetwork::setup_deadlock_channels_mono_dir(int br0, int br1) {
  const auto chip = get_chip(br0);
  assert(get_chip(br0) == get_chip(br1));
  const int br1_x = get_x(br1);
  const int br1_y = get_y(br1);
  int cur = br0;
  int next = -1;
  bool print = false;
  while(cur != br1) {
    const int cur_x = get_x(cur);
    const int cur_y = get_y(cur);
    if(cur_x < br1_x) {
      print = _routers[cur]->SetOutputMayBeDeadlock(right_port);
      next = get_node_id(chip, cur_y, cur_x + 1);
    } else if(cur_x > br1_x) {
      print = _routers[cur]->SetOutputMayBeDeadlock(left_port);
      next = get_node_id(chip, cur_y, cur_x - 1);
    } else if(cur_y < br1_y) {
      print = _routers[cur]->SetOutputMayBeDeadlock(down_port);
      next = get_node_id(chip, cur_y + 1, cur_x);
    } else if(cur_y > br1_y) {
      print = _routers[cur]->SetOutputMayBeDeadlock(up_port);
      next = get_node_id(chip, cur_y - 1, cur_x);
    } else {
      assert(false);
      next = -1;
    }
    if(print) cout << "Setting up deadlock link from " << cur << " to " << next << endl;
    cur = next;
  }
  assert(cur == br1);
}

void ChipletNetwork::setup_deadlock_channels_dual_dir(int br0, int br1) {
  if((get_chip(br0) != get_chip(br1)) || br0 == br1) return;
  setup_deadlock_channels_mono_dir(br0, br1);
  setup_deadlock_channels_mono_dir(br1, br0);
}

void ChipletNetwork::setup_deadlock_channels(const vector<int> &brs) {
  for(int i = 0; i < brs.size(); ++i) {
    for(int j = i + 1; j < brs.size(); ++j) {
      setup_deadlock_channels_dual_dir(brs[i], brs[j]);
    }
  }
}

void ChipletNetwork::setup_resources(const Configuration &config) {
  ostringstream name;
  const int _classes = config.GetInt("classes");
  if(config.GetInt("use_rc_buffer") > 0) {
    _inject_inter.resize(_nodes);
    _inject_cred_inter.resize(_nodes);
    _eject_inter.resize(_nodes);
    _eject_cred_inter.resize(_nodes);
    _inject_controllers.resize(_nodes);
  }
  for(int i = 0; i < _nodes; ++i) {
    const int chip_id = get_chip(i);
    const int y = get_y(i);
    const int x = get_x(i);
    name << "router";
    name << '_' << chip_id << '_' << y << '_' << x << '_' << i;
    _routers[i] = Router::NewRouter( config, this, name.str( ),i, 5, 5);
    _timed_modules.push_back(_routers[i].get());
    name.str("");
    if(config.GetInt("use_rc_buffer") > 0) {
      name << Name() << "_fchan_ingress_inter_" << i;
      _inject_inter[i] = make_unique<FlitChannel>(this, name.str(), _classes);
      name.str("");
      name << Name() << "_cchan_ingress_inter_" << i;
      _inject_cred_inter[i] = make_unique<CreditChannel>(this, name.str());
      name.str("");
      name << Name() << "_fchan_egress_inter_" << i;
      _eject_inter[i] = make_unique<FlitChannel>(this, name.str(), _classes);
      name.str("");
      name << Name() << "_cchan_egress_inter_" << i;
      _eject_cred_inter[i] = make_unique<CreditChannel>(this, name.str());
      name.str("");
      name << Name() << "_inject_controller_" << i;
      _inject_controllers[i] = make_unique<InjectController>(config, this, name.str(), _routers[i].get());
      name.str("");
      _timed_modules.push_back(_inject_inter[i].get());
      _timed_modules.push_back(_inject_cred_inter[i].get());
      _timed_modules.push_back(_eject_inter[i].get());
      _timed_modules.push_back(_eject_cred_inter[i].get());
      _timed_modules.push_back(_inject_controllers[i].get());
    }
  }
}

void ChipletNetwork::single_chip_conn(const Configuration &config, int chip_id) {
  int node = chip_id * chip_size;

  int left_node;
  int right_node;
  int up_node;
  int down_node;

  int right_input;
  int left_input;
  int up_input;
  int down_input;

  int right_output;
  int left_output;
  int up_output;
  int down_output;

  for(int i = node; i < node + chip_size; ++i) {
    left_node = this->left_node(i);
    right_node = this->right_node(i);
    up_node = this->up_node(i);
    down_node = this->down_node(i);

    right_input = this->left_channel(right_node);
    left_input = this->right_channel(left_node);
    up_input = this->down_channel(up_node);
    down_input = this->up_channel(down_node);

    right_output = this->right_channel(i);
    left_output = this->left_channel(i);
    up_output = this->up_channel(i);
    down_output = this->down_channel(i);

    // Do not connect boundary edges
    // right:0 left:1 down:2 up:3
    node_conn(i, right_input, right_output, 1, 1);
    node_conn(i, left_input, left_output, 1, 1);
    node_conn(i, down_input, down_output, 1, 1);
    node_conn(i, up_input, up_output, 1, 1);

    if(config.GetInt("use_rc_buffer") > 0) {
      _routers[i]->AddInputChannel( _inject_inter[i].get(), _inject_cred_inter[i].get() );
      _routers[i]->AddOutputChannel( _eject_inter[i].get(), _eject_cred_inter[i].get() );
      _inject_inter[i]->SetLatency( 1 );
      _eject_inter[i]->SetLatency( 1 );
      
      _inject_controllers[i]->SetInjectUpstreamChannel( _inject[i].get(), _inject_cred[i].get() );
      _inject_controllers[i]->SetInjectDownstreamChannel( _inject_inter[i].get(), _inject_cred_inter[i].get() );
      _inject_controllers[i]->SetEjectUpstreamChannel( _eject[i].get(), _eject_cred[i].get() );
      _inject_controllers[i]->SetEjectDownstreamChannel( _eject_inter[i].get(), _eject_cred_inter[i].get() );
      _inject[i]->SetLatency( 1 );
      _eject[i]->SetLatency( 1 );
    } else {
      _routers[i]->AddInputChannel( _inject[i].get(), _inject_cred[i].get() );
      _routers[i]->AddOutputChannel( _eject[i].get(), _eject_cred[i].get() );
      _inject[i]->SetLatency( 1 );
      _eject[i]->SetLatency( 1 );
      _routers[i]->SetBufferStatistics(local_port, false);
    }
  }
}