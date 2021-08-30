/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef KAGOME_CORE_RUNTIME_WAVM_CORE_API_PROVIDER_HPP
#define KAGOME_CORE_RUNTIME_WAVM_CORE_API_PROVIDER_HPP

#include "runtime/core_api_factory_impl.hpp"

#include <memory>

namespace kagome::storage::trie {
  class TrieStorage;
}

namespace kagome::storage::changes_trie {
  class ChangesTracker;
}

namespace kagome::blockchain {
  class BlockHeaderRepository;
}

namespace kagome::runtime {
  class Memory;
  class ModuleRepository;
}  // namespace kagome::runtime

namespace kagome::runtime::wavm {

  class IntrinsicModule;
  class CompartmentWrapper;
  class InstanceEnvironmentFactory;

  class CoreApiFactoryImpl final
      : public runtime::CoreApiFactory,
      public std::enable_shared_from_this<CoreApiFactoryImpl> {
   public:
    CoreApiFactoryImpl(
        std::shared_ptr<CompartmentWrapper> compartment,
        std::shared_ptr<storage::trie::TrieStorage> storage,
        std::shared_ptr<blockchain::BlockHeaderRepository> block_header_repo,
        std::shared_ptr<const InstanceEnvironmentFactory> instance_env_factory,
        std::shared_ptr<storage::changes_trie::ChangesTracker> changes_tracker);

    [[nodiscard]] std::unique_ptr<Core> make(
        std::shared_ptr<const crypto::Hasher> hasher,
        const std::vector<uint8_t> &runtime_code) const override;

   private:
    std::shared_ptr<const InstanceEnvironmentFactory> instance_env_factory_;
    std::shared_ptr<CompartmentWrapper> compartment_;
    std::shared_ptr<storage::trie::TrieStorage> storage_;
    std::shared_ptr<blockchain::BlockHeaderRepository> block_header_repo_;
    std::shared_ptr<storage::changes_trie::ChangesTracker> changes_tracker_;
  };

}  // namespace kagome::runtime::wavm

#endif  // KAGOME_CORE_RUNTIME_WAVM_CORE_API_PROVIDER_HPP