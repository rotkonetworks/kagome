/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef KAGOME_KADEMLIA_KAD_HPP
#define KAGOME_KADEMLIA_KAD_HPP

#include "libp2p/network/network.hpp"
#include "libp2p/protocol/kademlia/config.hpp"
#include "libp2p/protocol/kademlia/value_store.hpp"
#include "libp2p/routing/content_routing.hpp"
#include "libp2p/routing/peer_routing.hpp"

namespace libp2p::protocol::kademlia {

  /**
   * @class Kad
   *
   * Entrypoint to a kademlia network.
   */
  struct Kad
      : public PeerRouting /*, public ContentRouting, public ValueStore */ {
    ~Kad() override = default;

    enum class Error {
      SUCCESS = 0,
      NO_PEERS = 1
    };
  };

}  // namespace libp2p::protocol::kademlia

OUTCOME_HPP_DECLARE_ERROR(libp2p::protocol::kademlia, Kad::Error);

#endif  // KAGOME_KADEMLIA_KAD_HPP