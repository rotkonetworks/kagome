/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef KAGOME_CORE_RUNTIME_WAVM_INSTANCE_ENVIRONMENT_FACTORY_HPP
#define KAGOME_CORE_RUNTIME_WAVM_INSTANCE_ENVIRONMENT_FACTORY_HPP

#include "runtime/instance_environment.hpp"

namespace kagome::host_api {
  class HostApiFactory;
}

namespace kagome::storage::changes_trie {
  class ChangesTracker;
}

namespace kagome::storage::trie {
  class TrieStorage;
}

namespace kagome::blockchain {
  class BlockHeaderRepository;
}

namespace WAVM::Runtime {
  struct Instance;
}

namespace kagome::runtime::wavm {

  class ModuleInstance;
  class IntrinsicModule;
  class IntrinsicModuleInstance;
  class IntrinsicResolver;
  class CompartmentWrapper;

  class InstanceEnvironmentFactory final
      : public std::enable_shared_from_this<InstanceEnvironmentFactory> {
   public:
    InstanceEnvironmentFactory(
        std::shared_ptr<storage::trie::TrieStorage> storage,
        std::shared_ptr<CompartmentWrapper> compartment,
        std::shared_ptr<const IntrinsicModule> intrinsic_module,
        std::shared_ptr<host_api::HostApiFactory> host_api_factory,
        std::shared_ptr<blockchain::BlockHeaderRepository> block_header_repo,
        std::shared_ptr<storage::changes_trie::ChangesTracker> changes_tracker);

    enum class MemoryOrigin { EXTERNAL, INTERNAL };
    [[nodiscard]] InstanceEnvironment make(
        MemoryOrigin memory_origin,
        WAVM::Runtime::Instance *runtime_instance,
        std::shared_ptr<IntrinsicModuleInstance> intrinsic_instance) const;

   private:
    std::shared_ptr<storage::trie::TrieStorage> storage_;
    std::shared_ptr<CompartmentWrapper> compartment_;
    std::shared_ptr<const IntrinsicModule> intrinsic_module_;
    std::shared_ptr<host_api::HostApiFactory> host_api_factory_;
    std::shared_ptr<blockchain::BlockHeaderRepository> block_header_repo_;
    std::shared_ptr<storage::changes_trie::ChangesTracker> changes_tracker_;
  };

}  // namespace kagome::runtime::wavm

#endif  // KAGOME_CORE_RUNTIME_WAVM_INSTANCE_ENVIRONMENT_FACTORY_HPP
