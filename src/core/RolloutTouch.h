#pragma once
#include <map>
#include <string>
#include <vector>

// Per-rollout card-execution instrumentation for EXECUTION-TRACE carry
// (docs/design/execution-trace-carry.md). Records which cards' effects actually EXECUTED during a
// rollout, so a re-run can reuse a prior for cells whose rollouts never touched a change's cards.
//
// Off by default: g_sink is a null thread_local, so Record() is a single predicted-not-taken branch
// and the engine is byte-identical when instrumentation is disabled. One sink per worker thread (each
// rollout runs on one thread with its own AIEngine); the sink is repointed per rollout by the caller.
namespace rollout_touch
{
struct Sink
{
    const std::map<std::string, int>* index = nullptr;  // card name -> compact index (deck distinct cards)
    std::vector<char>*                hit   = nullptr;   // index -> 1 if that card's effect ran this rollout
};

extern thread_local Sink* g_sink;   // defined in EffectHandler.cpp; nullptr => instrumentation off

// Record that card `name`'s effect executed in the current rollout. Cheap no-op when off.
inline void Record(const std::string& name)
{
    Sink* s = g_sink;
    if (!s) { return; }
    auto it = s->index->find(name);
    if (it != s->index->end()) { (*s->hit)[it->second] = 1; }
}
}  // namespace rollout_touch
