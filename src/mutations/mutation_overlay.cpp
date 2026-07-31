#include "mutations/mutation_overlay.h"

#include "mutations/modifier_utils.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace forevertas {
namespace {

using PhysicsSandboxInputValueKind = forevervalidator::experimental::
        PhysicsSandboxInputValueKind;
using PhysicsSandboxSwitchState = forevervalidator::experimental::
        PhysicsSandboxSwitchState;

template<typename EventId>
std::size_t LowerBoundTime(const std::vector<EventId> &ids,
                           const MutationBaselineIndex &index,
                           std::int64_t timeMs) {
    return static_cast<std::size_t>(std::lower_bound(
            ids.begin(), ids.end(), timeMs,
            [&](EventId id, std::int64_t target) {
                return index.Event(id).timeMs < target;
            }) - ids.begin());
}

void NormalizeEvent(SandboxInputEvent *event,
                    std::uint32_t tickDurationMs) {
    event->timeMs = AlignInputTime(event->timeMs, tickDurationMs);
    if (event->value.kind == PhysicsSandboxInputValueKind::Analog) {
        event->value.analog = SaturateAnalogInputState(event->value.analog);
    } else if (event->value.kind ==
                       PhysicsSandboxInputValueKind::Switch) {
        event->value.switchState =
                event->value.switchState !=
                                PhysicsSandboxSwitchState::Released
                ? PhysicsSandboxSwitchState::Pressed
                : PhysicsSandboxSwitchState::Released;
    }
}

bool SameAction(const SparseMutationTimeline::EventHandle &left,
                const SparseMutationTimeline::EventHandle &right) {
    return left.event.action == right.event.action;
}

}  // namespace

bool MutationBaselineIndex::Build(
        const std::vector<SandboxInputEvent> &inputs,
        std::int64_t minimumTimeMs,
        std::int64_t maximumTimeMs,
        std::uint32_t tickDurationMs) {
    *this = {};
    if (minimumTimeMs > maximumTimeMs ||
        !InputEventsAreCanonical(inputs, tickDurationMs)) {
        return false;
    }

    const auto first = std::lower_bound(
            inputs.begin(), inputs.end(), minimumTimeMs,
            [](const SandboxInputEvent &event, std::int64_t timeMs) {
                return event.timeMs < timeMs;
            });
    const auto last = std::upper_bound(
            first, inputs.end(), maximumTimeMs,
            [](std::int64_t timeMs, const SandboxInputEvent &event) {
                return timeMs < event.timeMs;
            });

    source_ = &inputs;
    sourceBegin_ = static_cast<std::size_t>(first - inputs.begin());
    eventCount_ = static_cast<std::size_t>(last - first);
    minimumTimeMs_ = minimumTimeMs;
    maximumTimeMs_ = maximumTimeMs;

    for (auto it = inputs.begin(); it != first; ++it) {
        contextEvents_[{it->action, it->value.kind}] = *it;
    }

    EventId id = 0u;
    while (id < eventCount_) {
        const std::int64_t timeMs = Event(id).timeMs;
        const EventId firstId = id;
        while (id < eventCount_ && Event(id).timeMs == timeMs) {
            const SandboxInputEvent &event = Event(id);
            actionIds_[event.action].push_back(id);
            std::uint8_t eventMask = 0u;
            if (event.action == SandboxInputAction::Steer &&
                event.value.kind == PhysicsSandboxInputValueKind::Analog) {
                eventMask |= static_cast<std::uint8_t>(
                        MutationEligibility::SteeringAnalog);
            }
            if (IsAccelerateAction(event.action)) {
                eventMask |= static_cast<std::uint8_t>(
                        MutationEligibility::Accelerate);
            }
            if (IsBrakeAction(event.action)) {
                eventMask |= static_cast<std::uint8_t>(
                        MutationEligibility::Brake);
            }
            for (std::uint8_t mask = 1u; mask < eligibleIds_.size(); ++mask) {
                if ((eventMask & mask) != 0u) {
                    eligibleIds_[mask].push_back(id);
                }
            }
            ++id;
        }
        groups_.push_back(TimeGroup{timeMs, firstId, id});
    }
    return true;
}

const SandboxInputEvent &MutationBaselineIndex::Event(EventId id) const {
    if (source_ == nullptr || id >= eventCount_) {
        throw std::out_of_range("mutation baseline event ID is invalid");
    }
    return (*source_)[sourceBegin_ + static_cast<std::size_t>(id)];
}

const MutationBaselineIndex::TimeGroup *MutationBaselineIndex::FindGroup(
        std::int64_t timeMs) const {
    const auto found = std::lower_bound(
            groups_.begin(), groups_.end(), timeMs,
            [](const TimeGroup &group, std::int64_t target) {
                return group.timeMs < target;
            });
    return found != groups_.end() && found->timeMs == timeMs
            ? &*found
            : nullptr;
}

const std::vector<MutationBaselineIndex::EventId> &
MutationBaselineIndex::EligibleIds(
        MutationEligibility eligibility) const {
    const auto index = static_cast<std::uint8_t>(eligibility);
    if (index == 0u || index >= eligibleIds_.size()) {
        throw std::invalid_argument(
                "mutation eligibility mask is invalid");
    }
    return eligibleIds_[index];
}

const std::vector<MutationBaselineIndex::EventId> &
MutationBaselineIndex::ActionIds(SandboxInputAction action) const {
    static const std::vector<EventId> empty;
    const auto found = actionIds_.find(action);
    return found == actionIds_.end() ? empty : found->second;
}

SparseMutationTimeline::SparseMutationTimeline(
        const MutationBaselineIndex &baseline)
    : baseline_(baseline) {
    if (!baseline_.IsUsable()) {
        throw std::invalid_argument("mutation baseline index is unusable");
    }
}

bool SparseMutationTimeline::RankLess(const SequenceRank &left,
                                      const SequenceRank &right) {
    if (left.appended != right.appended) return !left.appended;
    if (left.appended) return left.serial < right.serial;
    if (left.timeMs != right.timeMs) return left.timeMs < right.timeMs;
    return left.ordinal < right.ordinal;
}

bool SparseMutationTimeline::MatchesEligibility(
        const SandboxInputEvent &event,
        MutationEligibility eligibility) {
    MutationEligibility matched = MutationEligibility::None;
    if (event.action == SandboxInputAction::Steer &&
        event.value.kind == PhysicsSandboxInputValueKind::Analog) {
        matched = matched | MutationEligibility::SteeringAnalog;
    }
    if (IsAccelerateAction(event.action)) {
        matched = matched | MutationEligibility::Accelerate;
    }
    if (IsBrakeAction(event.action)) {
        matched = matched | MutationEligibility::Brake;
    }
    return AnyMutationEligibility(matched & eligibility);
}

SparseMutationTimeline::Group &SparseMutationTimeline::TouchGroup(
        std::int64_t timeMs) {
    const auto existing = groups_.find(timeMs);
    if (existing != groups_.end()) return existing->second;

    Group group;
    if (const MutationBaselineIndex::TimeGroup *const baselineGroup =
                baseline_.FindGroup(timeMs)) {
        group.reserve(static_cast<std::size_t>(
                baselineGroup->lastId - baselineGroup->firstId));
        for (EventId id = baselineGroup->firstId;
             id < baselineGroup->lastId;
             ++id) {
            if (IsDeleted(id)) continue;
            const SandboxInputEvent *const updated = UpdatedEvent(id);
            group.push_back(Node{
                    id, updated == nullptr ? baseline_.Event(id) : *updated});
        }
    }
    return groups_.emplace(timeMs, std::move(group)).first->second;
}

const SandboxInputEvent *SparseMutationTimeline::UpdatedEvent(
        EventId id) const {
    const auto found = std::find_if(
            updates_.begin(), updates_.end(),
            [id](const UpdateNode &update) { return update.id == id; });
    return found == updates_.end() ? nullptr : &found->event;
}

bool SparseMutationTimeline::IsDeleted(EventId id) const {
    return std::find(deleted_.begin(), deleted_.end(), id) !=
            deleted_.end();
}

void SparseMutationTimeline::SetUpdate(EventId id,
                                       SandboxInputEvent event) {
    const auto found = std::find_if(
            updates_.begin(), updates_.end(),
            [id](const UpdateNode &update) { return update.id == id; });
    if (found == updates_.end()) {
        updates_.push_back(UpdateNode{id, std::move(event)});
    } else {
        found->event = std::move(event);
    }
    deleted_.erase(std::remove(deleted_.begin(), deleted_.end(), id),
                   deleted_.end());
}

void SparseMutationTimeline::MarkDeleted(EventId id) {
    updates_.erase(std::remove_if(
                           updates_.begin(), updates_.end(),
                           [id](const UpdateNode &update) {
                               return update.id == id;
                           }),
                   updates_.end());
    if (!IsDeleted(id)) deleted_.push_back(id);
}

void SparseMutationTimeline::EraseCanonical(EventId id,
                                            std::int64_t timeMs) {
    const auto override = groups_.find(timeMs);
    if (override == groups_.end()) {
        MarkDeleted(id);
        return;
    }
    override->second.erase(std::remove_if(
                                   override->second.begin(),
                                   override->second.end(),
                                   [id](const Node &node) {
                                       return node.id == id;
                                   }),
                           override->second.end());
}

std::vector<SparseMutationTimeline::EventHandle>
SparseMutationTimeline::CollectEligible(
        MutationEligibility eligibility,
        std::int64_t minimumTimeMs,
        std::int64_t maximumTimeMs) const {
    std::vector<EventHandle> baselineHandles;
    const std::vector<EventId> &ids = baseline_.EligibleIds(eligibility);
    baselineHandles.reserve(ids.size());
    std::size_t index = LowerBoundTime(ids, baseline_, minimumTimeMs);
    for (; index < ids.size(); ++index) {
        const EventId id = ids[index];
        const SandboxInputEvent &baselineEvent = baseline_.Event(id);
        if (baselineEvent.timeMs > maximumTimeMs) break;
        if (groups_.find(baselineEvent.timeMs) != groups_.end() ||
            IsDeleted(id)) {
            continue;
        }
        const SandboxInputEvent *const updated = UpdatedEvent(id);
        const SandboxInputEvent &event = updated == nullptr
                ? baselineEvent
                : *updated;
        if (!MatchesEligibility(event, eligibility)) continue;
        const MutationBaselineIndex::TimeGroup *const group =
                baseline_.FindGroup(baselineEvent.timeMs);
        baselineHandles.push_back(EventHandle{
                id,
                event,
                SequenceRank{
                        false,
                        event.timeMs,
                        static_cast<std::uint32_t>(id - group->firstId),
                        0u}});
    }

    std::vector<EventHandle> overlayHandles;
    for (auto groupIt = groups_.lower_bound(minimumTimeMs);
         groupIt != groups_.end() && groupIt->first <= maximumTimeMs;
         ++groupIt) {
        const auto &[timeMs, group] = *groupIt;
        for (std::size_t ordinal = 0u; ordinal < group.size(); ++ordinal) {
            if (!MatchesEligibility(group[ordinal].event, eligibility)) {
                continue;
            }
            overlayHandles.push_back(EventHandle{
                    group[ordinal].id,
                    group[ordinal].event,
                    SequenceRank{false,
                                 timeMs,
                                 static_cast<std::uint32_t>(ordinal),
                                 0u}});
        }
    }

    std::vector<EventHandle> result;
    result.reserve(baselineHandles.size() + overlayHandles.size());
    std::merge(baselineHandles.begin(), baselineHandles.end(),
               overlayHandles.begin(), overlayHandles.end(),
               std::back_inserter(result),
               [](const EventHandle &left, const EventHandle &right) {
                   return RankLess(left.rank, right.rank);
               });
    return result;
}

std::vector<SparseMutationTimeline::EventHandle>
SparseMutationTimeline::CollectCanonicalAction(
        SandboxInputAction action,
        std::int64_t minimumTimeMs,
        std::int64_t maximumTimeMs) const {
    std::vector<EventHandle> baselineHandles;
    const std::vector<EventId> &ids = baseline_.ActionIds(action);
    baselineHandles.reserve(ids.size());
    std::size_t index = LowerBoundTime(ids, baseline_, minimumTimeMs);
    for (; index < ids.size(); ++index) {
        const EventId id = ids[index];
        const SandboxInputEvent &baselineEvent = baseline_.Event(id);
        if (baselineEvent.timeMs > maximumTimeMs) break;
        if (groups_.find(baselineEvent.timeMs) != groups_.end() ||
            IsDeleted(id)) {
            continue;
        }
        const SandboxInputEvent *const updated = UpdatedEvent(id);
        const SandboxInputEvent &event = updated == nullptr
                ? baselineEvent
                : *updated;
        if (event.action != action) continue;
        const MutationBaselineIndex::TimeGroup *const group =
                baseline_.FindGroup(baselineEvent.timeMs);
        baselineHandles.push_back(EventHandle{
                id,
                event,
                SequenceRank{false,
                             event.timeMs,
                             static_cast<std::uint32_t>(id - group->firstId),
                             0u}});
    }

    std::vector<EventHandle> overlayHandles;
    for (auto groupIt = groups_.lower_bound(minimumTimeMs);
         groupIt != groups_.end() && groupIt->first <= maximumTimeMs;
         ++groupIt) {
        const auto &[timeMs, group] = *groupIt;
        for (std::size_t ordinal = 0u; ordinal < group.size(); ++ordinal) {
            if (group[ordinal].event.action != action) continue;
            overlayHandles.push_back(EventHandle{
                    group[ordinal].id,
                    group[ordinal].event,
                    SequenceRank{false,
                                 timeMs,
                                 static_cast<std::uint32_t>(ordinal),
                                 0u}});
        }
    }

    std::vector<EventHandle> result;
    result.reserve(baselineHandles.size() + overlayHandles.size());
    std::merge(baselineHandles.begin(), baselineHandles.end(),
               overlayHandles.begin(), overlayHandles.end(),
               std::back_inserter(result),
               [](const EventHandle &left, const EventHandle &right) {
                   return RankLess(left.rank, right.rank);
               });
    return result;
}

std::optional<SparseMutationTimeline::EventHandle>
SparseMutationTimeline::LatestCanonicalAction(
        const std::map<std::int64_t, Group> &groups,
        SandboxInputAction action,
        PhysicsSandboxInputValueKind kind,
        std::int64_t timeMs) const {
    std::optional<EventHandle> best;
    const auto consider = [&](EventHandle candidate) {
        if (candidate.event.timeMs > timeMs ||
            candidate.event.action != action ||
            candidate.event.value.kind != kind) {
            return;
        }
        if (!best || candidate.event.timeMs > best->event.timeMs ||
            (candidate.event.timeMs == best->event.timeMs &&
             RankLess(best->rank, candidate.rank))) {
            best = std::move(candidate);
        }
    };

    const auto context = baseline_.contextEvents_.find({action, kind});
    if (context != baseline_.contextEvents_.end()) {
        consider(EventHandle{
                std::numeric_limits<EventId>::max(),
                context->second,
                SequenceRank{false,
                             context->second.timeMs,
                             0u,
                             0u}});
    }

    const std::vector<EventId> &ids = baseline_.ActionIds(action);
    std::size_t upper = static_cast<std::size_t>(std::upper_bound(
            ids.begin(), ids.end(), timeMs,
            [&](std::int64_t target, EventId id) {
                return target < baseline_.Event(id).timeMs;
            }) - ids.begin());
    while (upper != 0u) {
        const EventId id = ids[--upper];
        const SandboxInputEvent &baselineEvent = baseline_.Event(id);
        if (baselineEvent.value.kind != kind || IsDeleted(id)) continue;
        if (groups.find(baselineEvent.timeMs) != groups.end()) continue;
        const SandboxInputEvent *const updated = UpdatedEvent(id);
        const SandboxInputEvent &event = updated == nullptr
                ? baselineEvent
                : *updated;
        if (event.action != action || event.value.kind != kind) continue;
        const MutationBaselineIndex::TimeGroup *const group =
                baseline_.FindGroup(baselineEvent.timeMs);
        consider(EventHandle{
                id,
                event,
                SequenceRank{false,
                             event.timeMs,
                             static_cast<std::uint32_t>(id - group->firstId),
                             0u}});
        break;
    }

    for (auto groupIt = groups.upper_bound(timeMs);
         groupIt != groups.begin();) {
        --groupIt;
        const auto &[groupTime, group] = *groupIt;
        bool foundAtTime = false;
        for (std::size_t ordinal = 0u; ordinal < group.size(); ++ordinal) {
            if (group[ordinal].event.action != action ||
                group[ordinal].event.value.kind != kind) {
                continue;
            }
            consider(EventHandle{
                    group[ordinal].id,
                    group[ordinal].event,
                    SequenceRank{false,
                                 groupTime,
                                 static_cast<std::uint32_t>(ordinal),
                                 0u}});
            foundAtTime = true;
        }
        if (foundAtTime) break;
    }
    return best;
}

std::optional<SparseMutationTimeline::EventHandle>
SparseMutationTimeline::LatestPendingAction(
        SandboxInputAction action,
        PhysicsSandboxInputValueKind kind,
        std::int64_t timeMs) const {
    std::optional<EventHandle> best;
    for (const PendingNode &pending : pending_) {
        if (pending.node.event.action != action ||
            pending.node.event.value.kind != kind ||
            pending.node.event.timeMs > timeMs) {
            continue;
        }
        EventHandle candidate{
                pending.node.id, pending.node.event, pending.rank};
        if (!best || candidate.event.timeMs > best->event.timeMs ||
            (candidate.event.timeMs == best->event.timeMs &&
             RankLess(best->rank, candidate.rank))) {
            best = std::move(candidate);
        }
    }
    return best;
}

AnalogInputState SparseMutationTimeline::SteeringStateAt(
        std::int64_t timeMs) const {
    std::optional<EventHandle> best = LatestCanonicalAction(
            groups_, SandboxInputAction::Steer,
            PhysicsSandboxInputValueKind::Analog, timeMs);
    const std::optional<EventHandle> pending = LatestPendingAction(
            SandboxInputAction::Steer,
            PhysicsSandboxInputValueKind::Analog, timeMs);
    if (pending &&
        (!best || pending->event.timeMs > best->event.timeMs ||
         (pending->event.timeMs == best->event.timeMs &&
          RankLess(best->rank, pending->rank)))) {
        best = pending;
    }
    return best ? best->event.value.analog : 0;
}

AnalogInputState SparseMutationTimeline::SteeringStateAt(
        const Snapshot &snapshot,
        std::int64_t timeMs) const {
    const std::optional<EventHandle> best = LatestSnapshotAction(
            snapshot, SandboxInputAction::Steer,
            PhysicsSandboxInputValueKind::Analog, timeMs);
    return best ? best->event.value.analog : 0;
}

bool SparseMutationTimeline::SwitchStateAt(
        SandboxInputAction action,
        std::int64_t timeMs) const {
    std::optional<EventHandle> best = LatestCanonicalAction(
            groups_, action, PhysicsSandboxInputValueKind::Switch, timeMs);
    const std::optional<EventHandle> pending = LatestPendingAction(
            action, PhysicsSandboxInputValueKind::Switch, timeMs);
    if (pending &&
        (!best || pending->event.timeMs > best->event.timeMs ||
         (pending->event.timeMs == best->event.timeMs &&
          RankLess(best->rank, pending->rank)))) {
        best = pending;
    }
    return best && best->event.value.switchState !=
            PhysicsSandboxSwitchState::Released;
}

bool SparseMutationTimeline::SwitchStateAt(
        const Snapshot &snapshot,
        SandboxInputAction action,
        std::int64_t timeMs) const {
    const std::optional<EventHandle> best = LatestSnapshotAction(
            snapshot, action, PhysicsSandboxInputValueKind::Switch, timeMs);
    return best && best->event.value.switchState !=
            PhysicsSandboxSwitchState::Released;
}

std::optional<SparseMutationTimeline::EventHandle>
SparseMutationTimeline::LatestSnapshotAction(
        const Snapshot &snapshot,
        SandboxInputAction action,
        PhysicsSandboxInputValueKind kind,
        std::int64_t timeMs) const {
    std::optional<EventHandle> best;
    const auto snapshotDeleted = [&](EventId id) {
        return std::find(snapshot.deleted_.begin(),
                         snapshot.deleted_.end(), id) !=
                snapshot.deleted_.end();
    };
    const auto snapshotUpdated = [&](EventId id)
            -> const SandboxInputEvent * {
        const auto found = std::find_if(
                snapshot.updates_.begin(), snapshot.updates_.end(),
                [id](const Snapshot::UpdateNode &update) {
                    return update.id == id;
                });
        return found == snapshot.updates_.end() ? nullptr : &found->event;
    };
    const auto consider = [&](EventHandle candidate) {
        if (candidate.event.timeMs > timeMs ||
            candidate.event.action != action ||
            candidate.event.value.kind != kind) {
            return;
        }
        if (!best || candidate.event.timeMs > best->event.timeMs ||
            (candidate.event.timeMs == best->event.timeMs &&
             RankLess(best->rank, candidate.rank))) {
            best = std::move(candidate);
        }
    };

    const auto context = baseline_.contextEvents_.find({action, kind});
    if (context != baseline_.contextEvents_.end()) {
        consider(EventHandle{
                std::numeric_limits<EventId>::max(),
                context->second,
                SequenceRank{false, context->second.timeMs, 0u, 0u}});
    }

    const std::vector<EventId> &ids = baseline_.ActionIds(action);
    std::size_t upper = static_cast<std::size_t>(std::upper_bound(
            ids.begin(), ids.end(), timeMs,
            [&](std::int64_t target, EventId id) {
                return target < baseline_.Event(id).timeMs;
            }) - ids.begin());
    while (upper != 0u) {
        const EventId id = ids[--upper];
        const SandboxInputEvent &baselineEvent = baseline_.Event(id);
        if (baselineEvent.value.kind != kind || snapshotDeleted(id)) continue;
        if (snapshot.groups_.find(baselineEvent.timeMs) !=
            snapshot.groups_.end()) {
            continue;
        }
        const SandboxInputEvent *const updated = snapshotUpdated(id);
        const SandboxInputEvent &event = updated == nullptr
                ? baselineEvent
                : *updated;
        if (event.action != action || event.value.kind != kind) continue;
        const MutationBaselineIndex::TimeGroup *const group =
                baseline_.FindGroup(baselineEvent.timeMs);
        consider(EventHandle{
                id,
                event,
                SequenceRank{false,
                             event.timeMs,
                             static_cast<std::uint32_t>(id - group->firstId),
                             0u}});
        break;
    }

    for (auto groupIt = snapshot.groups_.upper_bound(timeMs);
         groupIt != snapshot.groups_.begin();) {
        --groupIt;
        const auto &[groupTime, group] = *groupIt;
        bool foundAtTime = false;
        for (std::size_t ordinal = 0u; ordinal < group.size(); ++ordinal) {
            if (group[ordinal].event.action != action ||
                group[ordinal].event.value.kind != kind) {
                continue;
            }
            consider(EventHandle{
                    group[ordinal].id,
                    group[ordinal].event,
                    SequenceRank{false,
                                 groupTime,
                                 static_cast<std::uint32_t>(ordinal),
                                 0u}});
            foundAtTime = true;
        }
        if (foundAtTime) break;
    }
    return best;
}

SparseMutationTimeline::Snapshot SparseMutationTimeline::CaptureSnapshot()
        const {
    if (!pending_.empty()) {
        throw std::logic_error(
                "cannot snapshot an unnormalized sparse mutation timeline");
    }
    Snapshot snapshot;
    for (const auto &[time, group] : groups_) {
        std::vector<Snapshot::Node> copied;
        copied.reserve(group.size());
        for (const Node &node : group) {
            copied.push_back(Snapshot::Node{node.id, node.event});
        }
        snapshot.groups_.emplace(time, std::move(copied));
    }
    snapshot.updates_.reserve(updates_.size());
    for (const UpdateNode &update : updates_) {
        snapshot.updates_.push_back(
                Snapshot::UpdateNode{update.id, update.event});
    }
    snapshot.deleted_ = deleted_;
    return snapshot;
}

void SparseMutationTimeline::ReplaceEvent(const EventHandle &handle,
                                          SandboxInputEvent event) {
    for (PendingNode &entry : pending_) {
        if (entry.node.id == handle.id) {
            entry.node.event = std::move(event);
            return;
        }
    }

    const std::int64_t oldTimeMs = handle.event.timeMs;
    if (event.timeMs == oldTimeMs) {
        const auto override = groups_.find(oldTimeMs);
        if (override == groups_.end()) {
            SetUpdate(handle.id, std::move(event));
            return;
        }
        Group &group = override->second;
        const auto found = std::find_if(
                group.begin(), group.end(),
                [&](const Node &node) { return node.id == handle.id; });
        if (found == group.end()) {
            throw std::logic_error("sparse mutation event is no longer live");
        }
        found->event = std::move(event);
        return;
    }

    EraseCanonical(handle.id, oldTimeMs);
    pending_.push_back(PendingNode{
            Node{handle.id, std::move(event)}, handle.rank});
}

void SparseMutationTimeline::EraseEvent(const EventHandle &handle) {
    const auto pending = std::find_if(
            pending_.begin(), pending_.end(),
            [&](const PendingNode &entry) {
                return entry.node.id == handle.id;
            });
    if (pending != pending_.end()) {
        pending_.erase(pending);
        return;
    }
    EraseCanonical(handle.id, handle.event.timeMs);
}

void SparseMutationTimeline::EraseActionRange(
        SandboxInputAction action,
        std::int64_t minimumTimeMs,
        std::int64_t maximumTimeMs) {
    const std::vector<EventHandle> canonical = CollectCanonicalAction(
            action, minimumTimeMs, maximumTimeMs);
    for (const EventHandle &handle : canonical) {
        EraseCanonical(handle.id, handle.event.timeMs);
    }
    pending_.erase(std::remove_if(
                           pending_.begin(), pending_.end(),
                           [&](const PendingNode &entry) {
                               return entry.node.event.action == action &&
                                       entry.node.event.timeMs >=
                                               minimumTimeMs &&
                                       entry.node.event.timeMs <=
                                               maximumTimeMs;
                           }),
                   pending_.end());
}

void SparseMutationTimeline::AppendEvent(SandboxInputEvent event) {
    pending_.push_back(PendingNode{
            Node{nextSyntheticId_++, std::move(event)},
            SequenceRank{true, 0, 0u, nextAppendSerial_++}});
}

void SparseMutationTimeline::Normalize(std::uint32_t tickDurationMs) {
    if (pending_.empty()) return;

    std::map<std::int64_t, std::vector<PendingNode>> byTargetTime;
    for (PendingNode &pending : pending_) {
        NormalizeEvent(&pending.node.event, tickDurationMs);
        byTargetTime[pending.node.event.timeMs].push_back(std::move(pending));
    }

    for (auto &[targetTime, additions] : byTargetTime) {
        Group &target = TouchGroup(targetTime);
        std::vector<EventHandle> ordered;
        ordered.reserve(target.size() + additions.size());
        for (std::size_t ordinal = 0u; ordinal < target.size(); ++ordinal) {
            ordered.push_back(EventHandle{
                    target[ordinal].id,
                    target[ordinal].event,
                    SequenceRank{false,
                                 targetTime,
                                 static_cast<std::uint32_t>(ordinal),
                                 0u}});
        }
        for (const PendingNode &addition : additions) {
            ordered.push_back(EventHandle{
                    addition.node.id,
                    addition.node.event,
                    addition.rank});
        }
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const EventHandle &left,
                            const EventHandle &right) {
                             return RankLess(left.rank, right.rank);
                         });

        std::vector<EventHandle> resolved;
        resolved.reserve(ordered.size());
        for (const EventHandle &candidate : ordered) {
            const auto duplicate = std::find_if(
                    resolved.rbegin(), resolved.rend(),
                    [&](const EventHandle &existing) {
                        return SameAction(existing, candidate);
                    });
            if (duplicate == resolved.rend()) {
                resolved.push_back(candidate);
            } else {
                // Match NormalizeInputEvents: keep the first slot and stable
                // order, but overwrite its complete event with the last one.
                duplicate->event = candidate.event;
            }
        }

        target.clear();
        target.reserve(resolved.size());
        for (EventHandle &event : resolved) {
            event.event.timeMs = targetTime;
            target.push_back(Node{event.id, std::move(event.event)});
        }
    }
    pending_.clear();
}

std::vector<SandboxInputEvent> SparseMutationTimeline::MaterializeRange(
        std::int64_t minimumTimeMs,
        std::int64_t maximumTimeMs) const {
    if (!pending_.empty()) {
        throw std::logic_error(
                "cannot materialize an unnormalized sparse mutation timeline");
    }
    std::vector<SandboxInputEvent> result;
    result.reserve(baseline_.EventCount() + updates_.size());
    auto baselineIt = std::lower_bound(
            baseline_.groups_.begin(), baseline_.groups_.end(), minimumTimeMs,
            [](const MutationBaselineIndex::TimeGroup &group,
               std::int64_t target) {
                return group.timeMs < target;
            });
    auto overlayIt = groups_.lower_bound(minimumTimeMs);
    while ((baselineIt != baseline_.groups_.end() &&
            baselineIt->timeMs <= maximumTimeMs) ||
           (overlayIt != groups_.end() &&
            overlayIt->first <= maximumTimeMs)) {
        const bool useOverlay =
                overlayIt != groups_.end() &&
                overlayIt->first <= maximumTimeMs &&
                (baselineIt == baseline_.groups_.end() ||
                 overlayIt->first <= baselineIt->timeMs);
        if (useOverlay) {
            for (const auto &node : overlayIt->second) {
                result.push_back(node.event);
            }
            if (baselineIt != baseline_.groups_.end() &&
                baselineIt->timeMs == overlayIt->first) {
                ++baselineIt;
            }
            ++overlayIt;
        } else {
            for (EventId id = baselineIt->firstId;
                 id < baselineIt->lastId;
                 ++id) {
                if (IsDeleted(id)) continue;
                const SandboxInputEvent *const updated = UpdatedEvent(id);
                result.push_back(updated == nullptr
                                         ? baseline_.Event(id)
                                         : *updated);
            }
            ++baselineIt;
        }
    }
    return result;
}

}  // namespace forevertas
