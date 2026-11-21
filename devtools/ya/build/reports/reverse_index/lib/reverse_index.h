#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace NYa::NReverseIndex {
    // Two-pass CSR builder. After Finish(), only the immutable offsets/edges
    // remain. Numeric IDs are assigned in Python UID sort order by the caller.
    class TReverseIndex {
    public:
        void AddNode();
        void CountEdge(std::uint32_t dependency);
        void RemapCounts(const std::vector<std::uint32_t>& oldToNew);
        void BeginFill();
        void AddEdge(std::uint32_t dependency, std::uint32_t dependent);
        void Finish();

        std::uint32_t Begin(std::uint32_t node) const noexcept;
        std::uint32_t End(std::uint32_t node) const noexcept;
        std::uint32_t Edge(std::uint32_t offset) const noexcept;
        std::size_t KeyCount() const noexcept;
        std::size_t EdgeCount() const noexcept;
        std::size_t NativeBytes() const noexcept;

    private:
        enum class EState { Counting,
                            Filling,
                            Finished };
        EState State_ = EState::Counting;
        std::vector<std::uint32_t> Counts_;
        std::vector<std::uint32_t> Offsets_;
        std::vector<std::uint32_t> Edges_;
        std::size_t Keys_ = 0;
    };
} // namespace NYa::NReverseIndex
