#ifndef ATLAS_STUDIO_PACK_SNAPSHOTS
#define ATLAS_STUDIO_PACK_SNAPSHOTS

#include "atlas/Extension/TaskPack.h"

#include <filesystem>

namespace Atlas::Studio
{
    /** @brief Owns private pack copies; must outlive registries, graphs, and prepared tasks using them.
     * Snapshotting never executes native code. Operations are single-threaded and throw on any
     * filesystem, inspection, or digest failure. Destruction removes only this owner's directory.
     */
    class PackSnapshots final
    {
      public:
        PackSnapshots();
        ~PackSnapshots();
        PackSnapshots(const PackSnapshots&) = delete;
        PackSnapshots& operator=(const PackSnapshots&) = delete;
        /** @brief Copies referenced files with bounds, reinspects, and requires the inspected digest. */
        std::filesystem::path copy(const TaskPackManifest& inspected, const TaskPackRegistry& registry);
        /** @brief Reinspects the owned copy and rejects any post-copy digest mismatch before loading. */
        void verify(const std::string& digest, const TaskPackRegistry& registry) const;

      private:
        std::filesystem::path root;
    };
} // namespace Atlas::Studio
#endif
