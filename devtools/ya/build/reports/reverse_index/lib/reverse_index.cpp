#include "reverse_index.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace NYa::NReverseIndex {
    namespace {
        constexpr auto MaxId = std::numeric_limits<std::uint32_t>::max();
    } // namespace

    void TReverseIndex::AddNode() {
        if (State_ != EState::Counting) {
            throw std::logic_error("Reverse index is no longer counting");
        }
        if (Counts_.size() >= MaxId) {
            throw std::overflow_error("Reverse index has too many node IDs");
        }
        Counts_.push_back(0);
    }

    void TReverseIndex::CountEdge(std::uint32_t dependency) {
        if (State_ != EState::Counting || dependency >= Counts_.size()) {
            throw std::logic_error("Invalid reverse index count");
        }
        if (Counts_[dependency] == MaxId) {
            throw std::overflow_error("Reverse index degree exceeds uint32");
        }
        ++Counts_[dependency];
    }

    void TReverseIndex::RemapCounts(const std::vector<std::uint32_t>& oldToNew) {
        if (State_ != EState::Counting || oldToNew.size() != Counts_.size()) {
            throw std::logic_error("Invalid reverse index permutation");
        }
        std::vector<std::uint32_t> counts(Counts_.size());
        for (std::size_t i = 0; i < Counts_.size(); ++i) {
            if (oldToNew[i] >= counts.size()) {
                throw std::out_of_range("Invalid reverse index node ID");
            }
            counts[oldToNew[i]] = Counts_[i];
        }
        Counts_.swap(counts);
    }

    void TReverseIndex::BeginFill() {
        if (State_ != EState::Counting) {
            throw std::logic_error("Reverse index cannot begin filling");
        }
        Offsets_.resize(Counts_.size() + 1);
        std::uint64_t total = 0;
        for (std::size_t i = 0; i < Counts_.size(); ++i) {
            Offsets_[i] = static_cast<std::uint32_t>(total);
            total += Counts_[i];
            if (total > MaxId) {
                throw std::overflow_error("Reverse index has too many edges for uint32 offsets");
            }
        }
        Offsets_.back() = static_cast<std::uint32_t>(total);
        Edges_.resize(total);
        // Reuse the count buffer as write cursors during the second pass.
        std::copy(Offsets_.begin(), Offsets_.end() - 1, Counts_.begin());
        State_ = EState::Filling;
    }

    void TReverseIndex::AddEdge(std::uint32_t dependency, std::uint32_t dependent) {
        if (State_ != EState::Filling || dependency >= Counts_.size() || dependent >= Counts_.size() ||
            Counts_[dependency] >= Offsets_[dependency + 1]) {
            throw std::logic_error("Graph changed while building reverse index");
        }
        Edges_[Counts_[dependency]++] = dependent;
    }

    void TReverseIndex::Finish() {
        if (State_ != EState::Filling) {
            throw std::logic_error("Reverse index is not filling");
        }
        std::uint32_t output = 0;
        for (std::size_t i = 0; i < Counts_.size(); ++i) {
            const auto begin = Offsets_[i];
            const auto end = Offsets_[i + 1];
            if (Counts_[i] != end) {
                throw std::logic_error("Graph changed while building reverse index");
            }
            Offsets_[i] = output;
            if (begin != end) {
                auto first = Edges_.begin() + begin;
                auto last = Edges_.begin() + end;
                std::sort(first, last);
                last = std::unique(first, last);
                const auto size = static_cast<std::uint32_t>(last - first);
                if (output != begin) {
                    std::copy(first, last, Edges_.begin() + output);
                }
                output += size;
                ++Keys_;
            }
        }
        Offsets_.back() = output;
        Edges_.resize(output);
        Edges_.shrink_to_fit();
        std::vector<std::uint32_t>().swap(Counts_);
        State_ = EState::Finished;
    }

    std::uint32_t TReverseIndex::Begin(std::uint32_t node) const noexcept {
        return Offsets_[node];
    }

    std::uint32_t TReverseIndex::End(std::uint32_t node) const noexcept {
        return Offsets_[node + 1];
    }

    std::uint32_t TReverseIndex::Edge(std::uint32_t offset) const noexcept {
        return Edges_[offset];
    }

    std::size_t TReverseIndex::KeyCount() const noexcept {
        return Keys_;
    }

    std::size_t TReverseIndex::EdgeCount() const noexcept {
        return Edges_.size();
    }

    std::size_t TReverseIndex::NativeBytes() const noexcept {
        return sizeof(*this) + (Counts_.capacity() + Offsets_.capacity() + Edges_.capacity()) * sizeof(std::uint32_t);
    }
} // namespace NYa::NReverseIndex
