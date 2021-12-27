/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef KAGOME_OFFCHAIN_OFFCHAINWORKER
#define KAGOME_OFFCHAIN_OFFCHAINWORKER

#include <boost/optional.hpp>
#include <libp2p/multi/multiaddress.hpp>
#include <libp2p/peer/peer_id.hpp>

#include "common/buffer.hpp"
#include "offchain/types.hpp"
#include "outcome/outcome.hpp"
#include "primitives/common.hpp"
#include "primitives/extrinsic.hpp"

namespace kagome::offchain {

  /**
   *
   * The offchain workers allow the execution of long-running and possibly
   * non-deterministic tasks (e.g. web requests, encryption/decryption and
   * signing of data, random number generation, CPU-intensive computations,
   * enumeration/aggregation of on-chain data, etc.) which could otherwise
   * require longer than the block execution time. Offchain workers have their
   * own execution environment. This separation of concerns is to make sure that
   * the block production is not impacted by the long-running tasks.
   */
  class OffchainWorker {
   public:
    static const std::optional<std::reference_wrapper<OffchainWorker>>
        &worker_of_this_thread() {
      return worker_of_this_thread_opt();
    }

    virtual ~OffchainWorker() = default;

    virtual outcome::result<void> run() = 0;

    // ------------------------- Off-Chain API methods -------------------------

    virtual bool isValidator() const = 0;

    virtual Result<Success, Failure> submitTransaction(
        const primitives::Extrinsic &ext) = 0;

    virtual Result<OpaqueNetworkState, Failure> networkState() = 0;

    virtual Timestamp timestamp() = 0;

    virtual void sleepUntil(Timestamp) = 0;

    virtual RandomSeed randomSeed() = 0;

    virtual void localStorageSet(StorageType storage_type,
                                 const common::Buffer &key,
                                 common::Buffer value) = 0;

    virtual void localStorageClear(StorageType storage_type,
                                   const common::Buffer &key) = 0;

    virtual bool localStorageCompareAndSet(
        StorageType storage_type,
        const common::Buffer &key,
        std::optional<std::reference_wrapper<const common::Buffer>> expected,
        common::Buffer value) = 0;

    virtual outcome::result<common::Buffer> localStorageGet(
        StorageType storage_type, const common::Buffer &key) = 0;

    virtual Result<RequestId, Failure> httpRequestStart(
        HttpMethod method, std::string_view uri, common::Buffer meta) = 0;

    virtual Result<Success, Failure> httpRequestAddHeader(
        RequestId id, std::string_view name, std::string_view value) = 0;

    virtual Result<Success, HttpError> httpRequestWriteBody(
        RequestId id,
        common::Buffer chunk,
        std::optional<Timestamp> deadline) = 0;

    virtual std::vector<HttpStatus> httpResponseWait(
        const std::vector<RequestId> &ids,
        std::optional<Timestamp> deadline) = 0;

    virtual std::vector<std::pair<std::string, std::string>>
    httpResponseHeaders(RequestId id) = 0;

    virtual Result<uint32_t, HttpError> httpResponseReadBody(
        RequestId id,
        common::Buffer &chunk,
        std::optional<Timestamp> deadline) = 0;

    virtual void setAuthorizedNodes(std::vector<libp2p::peer::PeerId> nodes,
                                    bool authorized_only) = 0;

   protected:
    static void worker_of_this_thread(
        std::optional<std::reference_wrapper<OffchainWorker>> worker) {
      worker_of_this_thread_opt() = std::move(worker);
    }

   private:
    static std::optional<std::reference_wrapper<OffchainWorker>>
        &worker_of_this_thread_opt() {
      static thread_local std::optional<std::reference_wrapper<OffchainWorker>>
          worker_of_this_thread_opt = std::nullopt;
      return worker_of_this_thread_opt;
    }
  };

}  // namespace kagome::offchain

#endif  // KAGOME_OFFCHAIN_OFFCHAINWORKER