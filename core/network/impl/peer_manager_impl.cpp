/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "network/impl/peer_manager_impl.hpp"

#include <memory>

#include <libp2p/host/host.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <libp2p/protocol/kademlia/impl/peer_routing_table.hpp>
#include <libp2p/protocol/kademlia/kademlia.hpp>

#include "network/common.hpp"
#include "outcome/outcome.hpp"

namespace kagome::network {
  PeerManagerImpl::PeerManagerImpl(
      std::shared_ptr<application::AppStateManager> app_state_manager,
      libp2p::Host &host,
      std::shared_ptr<libp2p::protocol::Identify> identify,
      std::shared_ptr<libp2p::protocol::kademlia::Kademlia> kademlia,
      std::shared_ptr<libp2p::protocol::Scheduler> scheduler,
      std::shared_ptr<StreamEngine> stream_engine,
      const application::AppConfiguration &app_config,
      std::shared_ptr<clock::SteadyClock> clock,
      const BootstrapNodes &bootstrap_nodes,
      const OwnPeerInfo &own_peer_info,
      std::shared_ptr<network::SyncClientsSet> sync_clients,
      std::shared_ptr<network::Router> router)
      : app_state_manager_(std::move(app_state_manager)),
        host_(host),
        identify_(std::move(identify)),
        kademlia_(std::move(kademlia)),
        scheduler_(std::move(scheduler)),
        stream_engine_(std::move(stream_engine)),
        app_config_(app_config),
        clock_(std::move(clock)),
        bootstrap_nodes_(bootstrap_nodes),
        own_peer_info_(own_peer_info),
        sync_clients_(std::move(sync_clients)),
        router_{std::move(router)},
        log_(log::createLogger("PeerManager", "network")) {
    BOOST_ASSERT(app_state_manager_ != nullptr);
    BOOST_ASSERT(identify_ != nullptr);
    BOOST_ASSERT(kademlia_ != nullptr);
    BOOST_ASSERT(scheduler_ != nullptr);
    BOOST_ASSERT(stream_engine_ != nullptr);
    BOOST_ASSERT(sync_clients_ != nullptr);
    BOOST_ASSERT(router_ != nullptr);

    app_state_manager_->takeControl(*this);
  }

  bool PeerManagerImpl::prepare() {
    if (not app_config_.isRunInDevMode() && bootstrap_nodes_.empty()) {
      log_->critical(
          "Does not have any bootstrap nodes. "
          "Provide them by chain spec or CLI argument `--bootnodes'");
      return false;
    }

    return true;
  }

  bool PeerManagerImpl::start() {
    if (app_config_.isRunInDevMode() && bootstrap_nodes_.empty()) {
      log_->warn(
          "Peer manager is started in passive mode, "
          "because have not any bootstrap nodes.");
      return true;
    }

    // Add themselves into peer routing
    kademlia_->addPeer(host_.getPeerInfo(), true);

    add_peer_handle_ =
        host_.getBus()
            .getChannel<libp2p::protocol::kademlia::events::PeerAddedChannel>()
            .subscribe([wp = weak_from_this()](const PeerId &peer_id) {
              if (auto self = wp.lock()) {
                self->processDiscoveredPeer(peer_id);
              }
            });

    identify_->onIdentifyReceived(
        [wp = weak_from_this()](const PeerId &peer_id) {
          if (auto self = wp.lock()) {
            self->processFullyConnectedPeer(peer_id);
          }
        });

    // Start Identify protocol
    identify_->start();

    // Enqueue bootstrap nodes as first peers set
    for (const auto &bootstrap_node : bootstrap_nodes_) {
      kademlia_->addPeer(bootstrap_node, true);
    }

    // Start Kademlia (processing incoming message and random walking)
    kademlia_->start();

    // Do first aligning of peers count
    align();

    return true;
  }

  void PeerManagerImpl::stop() {
    add_peer_handle_.unsubscribe();
  }

  void PeerManagerImpl::connectToPeer(const PeerInfo &peer_info) {
    auto res = host_.getPeerRepository().getAddressRepository().upsertAddresses(
        peer_info.id, peer_info.addresses, libp2p::peer::ttl::kTransient);
    if (res) {
      connectToPeer(peer_info.id);
    }
  }

  size_t PeerManagerImpl::activePeersNumber() const {
    return active_peers_.size();
  }

  void PeerManagerImpl::forEachPeer(
      std::function<void(const PeerId &)> func) const {
    for (auto &it : active_peers_) {
      func(it.first);
    }
  }

  void PeerManagerImpl::forOnePeer(
      const PeerId &peer_id, std::function<void(const PeerId &)> func) const {
    if (active_peers_.count(peer_id)) {
      func(peer_id);
    }
  }

  void PeerManagerImpl::align() {
    const auto target_count = app_config_.peeringConfig().targetPeerAmount;
    const auto soft_limit = app_config_.peeringConfig().softLimit;
    const auto hard_limit = app_config_.peeringConfig().hardLimit;
    const auto peer_ttl = app_config_.peeringConfig().peerTtl;

    align_timer_.cancel();

    // Check disconnected
    auto block_announce_protocol = router_->getBlockAnnounceProtocol();
    for (auto it = active_peers_.begin(); it != active_peers_.end();) {
      auto [peer_id, timepoint] = *it++;
      if (not stream_engine_->isAlive(peer_id, block_announce_protocol)) {
        // Found disconnected
        auto &peer_id_ref = peer_id;
        SL_DEBUG(log_, "Found dead peer_id={}", peer_id_ref.toBase58());
        disconnectFromPeer(peer_id);
      }
    }

    // Soft limit is exceeded
    if (active_peers_.size() > soft_limit) {
      // Get oldest peer
      auto it = std::min_element(active_peers_.begin(),
                                 active_peers_.end(),
                                 [](const auto &item1, const auto &item2) {
                                   return item1.second.time < item2.second.time;
                                 });
      auto &[oldest_peer_id, data] = *it;
      auto &oldest_timepoint = data.time;

      if (active_peers_.size() > hard_limit) {
        // Hard limit is exceeded
        SL_DEBUG(log_, "Hard limit of of active peers is exceeded");
        disconnectFromPeer(oldest_peer_id);

      } else if (oldest_timepoint + peer_ttl < clock_->now()) {
        // Peer is inactive long time
        auto &oldest_peer_id_ref = oldest_peer_id;
        SL_DEBUG(
            log_, "Found inactive peer_id={}", oldest_peer_id_ref.toBase58());
        disconnectFromPeer(oldest_peer_id);

      } else {
        SL_DEBUG(log_, "No peer to disconnect at soft limit");
      }
    }

    // Not enough active peers
    if (active_peers_.size() < target_count) {
      if (not queue_to_connect_.empty()) {
        auto node = peers_in_queue_.extract(queue_to_connect_.front());
        auto &peer_id = node.value();

        queue_to_connect_.pop_front();
        BOOST_ASSERT(queue_to_connect_.size() == peers_in_queue_.size());

        connecting_peers_.emplace(peer_id);
        connectToPeer(peer_id);

        SL_DEBUG(log_,
                 "Remained peers in queue for connect: {}",
                 peers_in_queue_.size());
      } else if (connecting_peers_.empty()) {
        SL_DEBUG(log_, "Queue for connect is empty. Reuse bootstrap nodes");
        for (const auto &bootstrap_node : bootstrap_nodes_) {
          if (own_peer_info_.id != bootstrap_node.id) {
            connecting_peers_.emplace(bootstrap_node.id);
            connectToPeer(bootstrap_node.id);
          }
        }
      } else {
        SL_DEBUG(log_,
                 "Queue for connect is empty. Connecting peers: {}",
                 connecting_peers_.size());
      }
    }

    const auto aligning_period = app_config_.peeringConfig().aligningPeriod;

    align_timer_ = scheduler_->schedule(
        libp2p::protocol::scheduler::toTicks(aligning_period),
        [wp = weak_from_this()] {
          if (auto self = wp.lock()) {
            self->align();
          }
        });
  }

  void PeerManagerImpl::connectToPeer(const PeerId &peer_id) {
    auto peer_info = host_.getPeerRepository().getPeerInfo(peer_id);

    if (peer_info.addresses.empty()) {
      SL_DEBUG(log_, "Not found addresses for peer_id={}", peer_id.toBase58());
      return;
    }

    auto connectedness = host_.connectedness(peer_info);
    if (connectedness == libp2p::Host::Connectedness::CAN_NOT_CONNECT) {
      SL_DEBUG(log_, "Can not connect to peer_id={}", peer_id.toBase58());
      return;
    }

    SL_DEBUG(log_, "Try to connect to peer_id={}", peer_info.id.toBase58());
    for (auto addr : peer_info.addresses) {
      SL_DEBUG(log_, "  address: {}", addr.getStringAddress());
    }

    host_.connect(
        peer_info, [wp = weak_from_this(), peer_id = peer_info.id](auto res) {
          auto self = wp.lock();
          if (not self) {
            return;
          }
          self->connecting_peers_.erase(peer_id);
          if (not res.has_value()) {
            SL_DEBUG(self->log_,
                     "Connecting to peer_id={} is failed: {}",
                     peer_id.toBase58(),
                     res.error().message());
            return;
          }
          auto &connection = res.value();
          auto remote_peer_id_res = connection->remotePeer();
          if (not remote_peer_id_res.has_value()) {
            SL_DEBUG(self->log_,
                     "Connected, but not identifyed yet (expecting peer_id={})",
                     peer_id.toBase58());
            return;
          }
          auto &remote_peer_id = remote_peer_id_res.value();
          if (remote_peer_id == peer_id) {
            SL_DEBUG(self->log_,
                     "Perhaps has already connected to peer_id={}. "
                     "Processing immediately",
                     peer_id.toBase58());
            self->processFullyConnectedPeer(peer_id);
          }
        });
  }  // namespace kagome::network

  void PeerManagerImpl::disconnectFromPeer(const PeerId &peer_id) {
    auto it = active_peers_.find(peer_id);
    if (it != active_peers_.end()) {
      SL_DEBUG(log_, "Disconnect from peer_id={}", peer_id.toBase58());
      stream_engine_->del(peer_id);
      active_peers_.erase(it);
      SL_DEBUG(log_, "Remained {} active peers", active_peers_.size());
    }
    sync_clients_->remove(peer_id);
  }

  void PeerManagerImpl::keepAlive(const PeerId &peer_id) {
    auto it = active_peers_.find(peer_id);
    if (it != active_peers_.end()) {
      it->second.time = clock_->now();
    }
  }

  void PeerManagerImpl::updatePeerStatus(const PeerId &peer_id,
                                         const Status &status) {
    auto it = active_peers_.find(peer_id);
    if (it != active_peers_.end()) {
      it->second.time = clock_->now();
      it->second.status = status;
    } else {
      // Remove from connecting peer list
      connecting_peers_.erase(peer_id);

      // Remove from queue for connection
      if (auto piq_it = peers_in_queue_.find(peer_id);
          piq_it != peers_in_queue_.end()) {
        auto qtc_it = std::find_if(queue_to_connect_.cbegin(),
                                   queue_to_connect_.cend(),
                                   [&peer_id = peer_id](const auto &item) {
                                     return peer_id == item.get();
                                   });
        queue_to_connect_.erase(qtc_it);
        peers_in_queue_.erase(piq_it);
        BOOST_ASSERT(queue_to_connect_.size() == peers_in_queue_.size());

        SL_DEBUG(log_,
                 "Remained peers in queue for connect: {}",
                 peers_in_queue_.size());
      }

      // Add as active peer
      active_peers_.emplace(
          peer_id, ActivePeerData{.time = clock_->now(), .status = status});
    }
  }

  void PeerManagerImpl::updatePeerStatus(const PeerId &peer_id,
                                         const BlockInfo &best_block) {
    auto it = active_peers_.find(peer_id);
    if (it != active_peers_.end()) {
      it->second.time = clock_->now();
      it->second.status.best_block = best_block;
    }
  }

  boost::optional<Status> PeerManagerImpl::getPeerStatus(
      const PeerId &peer_id) {
    auto it = active_peers_.find(peer_id);
    if (it == active_peers_.end()) {
      return boost::none;
    }
    return it->second.status;
  }

  void PeerManagerImpl::processDiscoveredPeer(const PeerId &peer_id) {
    // Ignore himself
    if (own_peer_info_.id == peer_id) {
      return;
    }

    // Skip if peer is already active
    if (active_peers_.find(peer_id) != active_peers_.end()) {
      return;
    }

    auto [it, added] = peers_in_queue_.emplace(peer_id);

    // Already in queue
    if (not added) {
      return;
    }

    queue_to_connect_.emplace_back(*it);
    BOOST_ASSERT(queue_to_connect_.size() == peers_in_queue_.size());

    SL_DEBUG(log_,
             "New peer_id={} enqueued. In queue: {}",
             peer_id.toBase58(),
             queue_to_connect_.size());
  }

  void PeerManagerImpl::processFullyConnectedPeer(const PeerId &peer_id) {
    // Skip connection to himself
    if (own_peer_info_.id == peer_id) {
      return;
    }

    SL_DEBUG(log_, "New connection with peer_id={}", peer_id.toBase58());

    auto addresses_res =
        host_.getPeerRepository().getAddressRepository().getAddresses(peer_id);

    if (not addresses_res.has_value()) {
      SL_DEBUG(log_, "  addresses are not provided");
      return;
    }

    auto &addresses = addresses_res.value();
    for (auto addr : addresses) {
      SL_DEBUG(log_, "  address: {}", addr.getStringAddress());
    }

    PeerInfo peer_info{.id = peer_id, .addresses = std::move(addresses)};

    const auto hard_limit = app_config_.peeringConfig().hardLimit;

    size_t cur_active_peer = active_peers_.size();

    // Capacity is allow
    if (cur_active_peer >= hard_limit) {
      connecting_peers_.erase(peer_id);

    } else {
      auto block_announce_protocol = router_->getBlockAnnounceProtocol();
      if (not stream_engine_->isAlive(peer_info.id, block_announce_protocol)) {
        block_announce_protocol->newOutgoingStream(
            peer_info,

            [wp = weak_from_this(),
             peer_id = peer_info.id,
             protocol = block_announce_protocol](auto &&stream_res) {
              auto self = wp.lock();
              if (not self) {
                return;
              }

              // Remove from list of connecting peers
              self->connecting_peers_.erase(peer_id);

              if (not stream_res.has_value()) {
                self->log_->warn("Unable to create '{}' stream with {}: {}",
                                 protocol->protocol(),
                                 peer_id.toBase58(),
                                 stream_res.error().message());
                self->disconnectFromPeer(peer_id);
                return;
              }

              // Add to active peer list
              if (auto [ap_it, ok] = self->active_peers_.emplace(
                      peer_id, ActivePeerData{.time = self->clock_->now()});
                  ok) {
                // And remove from queue
                if (auto piq_it = self->peers_in_queue_.find(peer_id);
                    piq_it != self->peers_in_queue_.end()) {
                  auto qtc_it =
                      std::find_if(self->queue_to_connect_.cbegin(),
                                   self->queue_to_connect_.cend(),
                                   [&peer_id = peer_id](const auto &item) {
                                     return peer_id == item.get();
                                   });
                  self->queue_to_connect_.erase(qtc_it);
                  self->peers_in_queue_.erase(piq_it);
                  BOOST_ASSERT(self->queue_to_connect_.size()
                               == self->peers_in_queue_.size());

                  SL_DEBUG(self->log_,
                           "Remained peers in queue for connect: {}",
                           self->peers_in_queue_.size());
                }
              }
            });
      }
    }

    kademlia_->addPeer(peer_info, false);
  }

  void PeerManagerImpl::reserveStreams(const PeerId &peer_id) const {
    // Reserve stream slots for needed protocols

    stream_engine_->add(peer_id, router_->getGossipProtocol());
    stream_engine_->add(peer_id, router_->getPropagateTransactionsProtocol());
    stream_engine_->add(peer_id, router_->getSupProtocol());
  }

}  // namespace kagome::network