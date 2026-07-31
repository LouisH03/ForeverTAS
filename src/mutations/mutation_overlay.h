#ifndef FOREVERTAS_MUTATIONS_MUTATION_OVERLAY_H
#define FOREVERTAS_MUTATIONS_MUTATION_OVERLAY_H

#include "mutations/input_event_utils.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace forevertas {

enum class MutationEligibility : std::uint8_t {
    None = 0u,
    SteeringAnalog = 1u << 0u,
    Accelerate = 1u << 1u,
    Brake = 1u << 2u
};

constexpr MutationEligibility operator|(MutationEligibility left,
                                        MutationEligibility right) {
    return static_cast<MutationEligibility>(
            static_cast<std::uint8_t>(left) |
            static_cast<std::uint8_t>(right));
}

constexpr MutationEligibility operator&(MutationEligibility left,
                                        MutationEligibility right) {
    return static_cast<MutationEligibility>(
            static_cast<std::uint8_t>(left) &
            static_cast<std::uint8_t>(right));
}

constexpr bool AnyMutationEligibility(MutationEligibility value) {
    return value != MutationEligibility::None;
}

struct SparseMutationRequest {
    std::uint64_t iterationIndex = 0u;
    std::uint32_t passIndex = 0u;
    std::uint32_t tickDurationMs = 10u;
    std::int64_t mutableFromTimeMs = 0;
};

class MutationBaselineIndex final {
public:
    bool Build(const std::vector<SandboxInputEvent> &inputs,
               std::int64_t minimumTimeMs,
               std::int64_t maximumTimeMs,
               std::uint32_t tickDurationMs);

    bool IsUsable() const { return source_ != nullptr; }
    const std::vector<SandboxInputEvent> *Source() const { return source_; }
    std::int64_t MinimumTimeMs() const { return minimumTimeMs_; }
    std::int64_t MaximumTimeMs() const { return maximumTimeMs_; }
    std::size_t EventCount() const { return eventCount_; }
    const SandboxInputEvent &Event(std::uint64_t id) const;

private:
    friend class SparseMutationTimeline;

    using EventId = std::uint64_t;

    struct TimeGroup {
        std::int64_t timeMs = 0;
        EventId firstId = 0u;
        EventId lastId = 0u;
    };

    const TimeGroup *FindGroup(std::int64_t timeMs) const;
    const std::vector<EventId> &EligibleIds(
            MutationEligibility eligibility) const;
    const std::vector<EventId> &ActionIds(
            SandboxInputAction action) const;

    const std::vector<SandboxInputEvent> *source_ = nullptr;
    std::size_t sourceBegin_ = 0u;
    std::size_t eventCount_ = 0u;
    std::int64_t minimumTimeMs_ = 0;
    std::int64_t maximumTimeMs_ = 0;
    std::vector<TimeGroup> groups_;
    std::array<std::vector<EventId>, 8u> eligibleIds_;
    std::map<SandboxInputAction, std::vector<EventId>> actionIds_;
    std::map<std::pair<
                     SandboxInputAction,
                     forevervalidator::experimental::
                             PhysicsSandboxInputValueKind>,
             SandboxInputEvent>
            contextEvents_;
};

class SparseMutationTimeline final {
public:
    using EventId = std::uint64_t;

    struct SequenceRank {
        bool appended = false;
        std::int64_t timeMs = 0;
        std::uint32_t ordinal = 0u;
        std::uint64_t serial = 0u;
    };

    struct EventHandle {
        EventId id = 0u;
        SandboxInputEvent event;
        SequenceRank rank;
    };

    class Snapshot final {
    public:
        Snapshot() = default;

    private:
        friend class SparseMutationTimeline;
        struct Node {
            EventId id = 0u;
            SandboxInputEvent event;
        };
        struct UpdateNode {
            EventId id = 0u;
            SandboxInputEvent event;
        };
        std::map<std::int64_t, std::vector<Node>> groups_;
        std::vector<UpdateNode> updates_;
        std::vector<EventId> deleted_;
    };

    explicit SparseMutationTimeline(const MutationBaselineIndex &baseline);

    std::vector<EventHandle> CollectEligible(
            MutationEligibility eligibility,
            std::int64_t minimumTimeMs,
            std::int64_t maximumTimeMs) const;

    AnalogInputState SteeringStateAt(std::int64_t timeMs) const;
    AnalogInputState SteeringStateAt(const Snapshot &snapshot,
                                     std::int64_t timeMs) const;
    bool SwitchStateAt(SandboxInputAction action,
                       std::int64_t timeMs) const;
    bool SwitchStateAt(const Snapshot &snapshot,
                       SandboxInputAction action,
                       std::int64_t timeMs) const;

    Snapshot CaptureSnapshot() const;

    void ReplaceEvent(const EventHandle &handle,
                      SandboxInputEvent event);
    void EraseEvent(const EventHandle &handle);
    void EraseActionRange(SandboxInputAction action,
                          std::int64_t minimumTimeMs,
                          std::int64_t maximumTimeMs);
    void AppendEvent(SandboxInputEvent event);

    // Resolves only groups touched by moved or appended events. Untouched
    // baseline groups remain shared and are never copied or sorted.
    void Normalize(std::uint32_t tickDurationMs);

    std::vector<SandboxInputEvent> MaterializeRange(
            std::int64_t minimumTimeMs,
            std::int64_t maximumTimeMs) const;

    std::size_t TouchedGroupCount() const { return groups_.size(); }
    std::size_t PendingEventCount() const { return pending_.size(); }

private:
    struct Node {
        EventId id = 0u;
        SandboxInputEvent event;
    };

    struct PendingNode {
        Node node;
        SequenceRank rank;
    };

    struct UpdateNode {
        EventId id = 0u;
        SandboxInputEvent event;
    };

    using Group = std::vector<Node>;

    static constexpr EventId kSyntheticIdBase = EventId{1} << 63u;

    static bool RankLess(const SequenceRank &left,
                         const SequenceRank &right);
    static bool MatchesEligibility(const SandboxInputEvent &event,
                                   MutationEligibility eligibility);

    Group &TouchGroup(std::int64_t timeMs);
    const SandboxInputEvent *UpdatedEvent(EventId id) const;
    bool IsDeleted(EventId id) const;
    void SetUpdate(EventId id, SandboxInputEvent event);
    void MarkDeleted(EventId id);
    void EraseCanonical(EventId id, std::int64_t timeMs);

    std::vector<EventHandle> CollectCanonicalAction(
            SandboxInputAction action,
            std::int64_t minimumTimeMs,
            std::int64_t maximumTimeMs) const;

    std::optional<EventHandle> LatestCanonicalAction(
            const std::map<std::int64_t, Group> &groups,
            SandboxInputAction action,
            forevervalidator::experimental::PhysicsSandboxInputValueKind kind,
            std::int64_t timeMs) const;
    std::optional<EventHandle> LatestPendingAction(
            SandboxInputAction action,
            forevervalidator::experimental::PhysicsSandboxInputValueKind kind,
            std::int64_t timeMs) const;
    std::optional<EventHandle> LatestSnapshotAction(
            const Snapshot &snapshot,
            SandboxInputAction action,
            forevervalidator::experimental::PhysicsSandboxInputValueKind kind,
            std::int64_t timeMs) const;

    const MutationBaselineIndex &baseline_;
    std::map<std::int64_t, Group> groups_;
    std::vector<UpdateNode> updates_;
    std::vector<EventId> deleted_;
    std::vector<PendingNode> pending_;
    EventId nextSyntheticId_ = kSyntheticIdBase;
    std::uint64_t nextAppendSerial_ = 0u;
};

}  // namespace forevertas

#endif
