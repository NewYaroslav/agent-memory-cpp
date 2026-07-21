# Affective Memory Roadmap

This document defines an optional roadmap lane for storing affectively
meaningful agent memories. It does **not** turn `agent-memory-cpp` into an
emotion engine or an autonomous-agent framework. The intended boundary is:

```text
agent-affect runtime
  live needs, goals, appraisal, affect dynamics, coping, behaviour choice

agent-memory-cpp
  persisted events, snapshots, transitions, outcomes, relationship evidence,
  retrieval and compaction contracts
```

The library should remain a universal embedded memory/retrieval toolkit. The
affective lane is an opt-in profile extension for projects that need durable
emotional context, relationship history, and outcome-aware episode recall.

## ADR-A01 — Live affect is runtime state

The current affective state of an agent is not a `KnowledgeUnit` and must not
be reconstructed by naive retrieval of old snapshots.

Memory stores:

- which event happened;
- which appraisal was recorded;
- which goals were affected;
- what affect snapshot existed before/after the transition;
- which action or coping strategy was chosen;
- what outcome and prediction error followed.

The live state belongs to a sibling runtime controller:

```cpp
AffectController affect;
AffectState current = affect.state();
```

The memory stack may persist snapshots and transition records, but it does not
own the controller dynamics.

## ADR-A02 — Appraisal precedes emotion labels

Discrete labels such as `"angry"`, `"sad"`, or `"relieved"` are derived
interpretations. The durable primary data is structured appraisal: goal
relevance, goal congruence, certainty, controllability, responsibility,
urgency, social significance, and confidence.

This keeps memory useful across affect models and avoids baking one emotion
taxonomy into the storage layer.

## ADR-A03 — Self and inferred user affect are separate

Agent self-state, user self-report, and inferred user affect must never be
stored in the same field. Every appraisal or affect snapshot carries a
perspective:

```cpp
enum class AppraisalPerspective : std::uint8_t {
    AgentSelf,
    UserSelfReported,
    UserInferred,
    ThirdPartyInferred
};
```

Inferences about a user's emotional or mental state require confidence,
provenance, model identity, and retention policy. They must not be presented as
facts unless the source explicitly supports that.

## ADR-A04 — Episodes are append-only

Affective transitions are event-sourced. Old transition records are not
rewritten when the controller is improved or when a relationship state is
recomputed.

Conceptual record:

```cpp
struct AffectTransitionRecord {
    std::uint64_t sequence = 0;
    KnowledgeUnitId trigger_event_id = 0;
    KnowledgeUnitId transition_unit_id = 0;
    std::string controller_version;
    std::uint64_t config_hash = 0;
};
```

Changing controller parameters may produce new derived state, but it must not
silently mutate history.

## ADR-A05 — Affect may modulate retrieval, not truth

Affective context can influence retrieval and reranking:

- similar appraisal dimensions;
- same target entity or relationship;
- same impacted goal;
- unresolved episodes;
- similar prediction error;
- prior coping outcome.

It must not change factual confidence, provenance, or safety policy. Mood
congruence may be a small capped factor, not a dominant loop:

```text
score += w_mood * mood_congruence
where abs(w_mood) is deliberately bounded
```

This avoids artificial rumination where negative state retrieves negative
memories, which further amplifies negative state.

## ADR-A06 — Compaction preserves causal structure

Affective episodes must not be merged solely because their text is similar.
Compaction must preserve at least:

- trigger;
- actor/target;
- appraisal;
- impacted goals;
- peak affect snapshot;
- action or coping strategy;
- outcome;
- prediction error;
- unresolvedness;
- relationship evidence.

Two textually similar events can have different affective meaning if
responsibility, controllability, outcome, or relationship target differs.

## ADR-A07 — Sensitive inference requires provenance

Sensitive emotional, mental-health, or relationship inferences need explicit
policy metadata:

```cpp
enum class SensitivityClass : std::uint8_t {
    Normal,
    Personal,
    Sensitive,
    CrisisRelated
};

struct RetentionPolicyComponent {
    SensitivityClass sensitivity = SensitivityClass::Normal;
    bool allow_personalization = true;
    bool allow_model_training = false;
    bool allow_long_term_storage = true;
    std::uint64_t erase_after_ms = 0;
};
```

Every inferred user-state component should also carry confidence, provenance,
model/version, observation time, and deletion support.

## Optional capabilities

These capabilities are opt-in extensions to `AgentLongTermMemory`, not a new
mandatory baseline:

```cpp
enum class MemoryCapability : std::uint64_t {
    // existing...
    AffectiveEpisodes = 1ull << 13,
    GoalAttribution   = 1ull << 14,
    OutcomeTracking   = 1ull << 15,
    RelationshipState = 1ull << 16,
    SensitiveInferencePolicy = 1ull << 17
};
```

The exact bit positions are illustrative until the profile schema is
implemented. The important decision is the capability grouping: do not add a
separate capability for every scalar appraisal field.

## Candidate components

The first implementation should prototype these through `Custom` units and
typed metadata before freezing dedicated stores or DBIs.

### AppraisalComponent

```cpp
struct AppraisalComponent {
    AppraisalPerspective perspective = AppraisalPerspective::AgentSelf;

    double novelty = 0.0;
    double goal_relevance = 0.0;
    double goal_congruence = 0.0;
    double certainty = 0.0;
    double controllability = 0.0;
    double reversibility = 0.0;
    double urgency = 0.0;

    double responsibility_self = 0.0;
    double responsibility_other = 0.0;
    double norm_violation = 0.0;
    double social_significance = 0.0;

    double inference_confidence = 1.0;
    std::string appraisal_model_id;
};
```

### AffectSnapshotComponent

```cpp
struct AffectSnapshotComponent {
    double valence = 0.0;
    double arousal = 0.0;
    double control = 0.0;
    double certainty = 0.0;
    double affiliation = 0.0;
    double cognitive_load = 0.0;
    double allostatic_load = 0.0;

    std::uint64_t captured_at_ms = 0;
    std::string affect_model_id;
};
```

Emotion names can be stored as a derived interpretation:

```cpp
struct EmotionLabel {
    std::string label;
    double confidence = 0.0;
};

struct EmotionInterpretationComponent {
    std::vector<EmotionLabel> candidates;
};
```

### GoalImpactComponent

```cpp
using GoalId = std::uint64_t;

struct GoalImpact {
    GoalId goal_id = 0;
    double importance = 0.0;
    double success_probability_before = 0.0;
    double success_probability_after = 0.0;
    double information_deficit = 0.0;
    double resource_deficit = 0.0;
};

struct GoalImpactComponent {
    std::vector<GoalImpact> impacts;
};
```

The active goal catalogue stays in the planning/affect runtime. Memory stores
which goals a concrete episode affected.

### ActionOutcomeComponent

```cpp
enum class CopingStrategy : std::uint8_t {
    None,
    ProblemFocused,
    InformationSeeking,
    Reappraisal,
    Acceptance,
    AttentionShift,
    ExpressionControl,
    SocialRepair,
    HelpSeeking
};

struct ActionOutcomeComponent {
    std::string action_id;
    CopingStrategy coping = CopingStrategy::None;

    double predicted_utility = 0.0;
    double predicted_success_probability = 0.0;
    double actual_utility = 0.0;
    double prediction_error = 0.0;

    bool completed = false;
    bool resolved_trigger = false;
    std::uint64_t evaluated_at_ms = 0;
};
```

This makes the causal chain inspectable:

```text
event -> appraisal -> affect before/after -> action -> outcome
```

### SalienceComponent

`priority_weight`, usage statistics, and emotional salience are different
signals. Do not collapse them.

```cpp
struct SalienceComponent {
    double surprise = 0.0;
    double emotional_intensity = 0.0;
    double goal_relevance = 0.0;
    double social_significance = 0.0;
    double unresolvedness = 0.0;
    double prediction_error = 0.0;
    double encoding_strength = 0.0;
    double retrieval_boost = 0.0;
};
```

### RelationshipStateComponent

Relationship state is slow-moving derived state backed by evidence units:

```cpp
struct RelationshipStateComponent {
    std::string target_entity_id;

    double trust = 0.0;
    double affiliation = 0.0;
    double familiarity = 0.0;
    double perceived_threat = 0.0;
    double reciprocity = 0.0;
    double boundary_risk = 0.0;

    double confidence = 0.0;
    std::uint64_t updated_at_ms = 0;
    std::vector<KnowledgeUnitId> evidence_units;
};
```

`evidence_units` is mandatory for explainability and correction.

## Profile direction

`AffectiveAgentMemory` should be an overlay on `AgentLongTermMemory`:

```cpp
inline MemoryProfileSpec AffectiveAgentMemory() {
    MemoryProfileSpec spec = MemoryProfiles::AgentLongTermMemory();
    spec.name = "affective_agent_ltm";

    spec.enable_speaker = true;
    spec.enable_conversation_episode = true;
    spec.enable_affective_episodes = true;
    spec.enable_goal_attribution = true;
    spec.enable_outcome_tracking = true;
    spec.enable_relationship_state = true;
    spec.enable_sensitive_inference_policy = true;

    return spec;
}
```

This profile is not a universal default. It is for agents that need durable
relationship and affective episode memory.

## Retrieval roadmap

Affect-aware retrieval should start as rerank/evaluator policy over normal
hybrid retrieval, not as a new vector database.

Useful features:

- same target entity;
- same impacted goal;
- similar controllability/certainty/responsibility;
- unresolved episode match;
- same coping strategy;
- similar prediction error;
- relationship evidence proximity;
- capped mood-congruence.

Candidate scoring can extend existing hybrid retrieval:

```text
score =
  w_semantic * semantic_similarity
+ w_lexical  * lexical_similarity
+ w_target   * target_match
+ w_goal     * goal_overlap
+ w_appraise * appraisal_similarity
+ w_unres    * unresolvedness
+ w_error    * prediction_error_similarity
+ capped(w_mood * mood_congruence)
- w_repeat   * repetition_penalty
```

The exact weights belong to runtime policy, not the storage layer.

## Compaction roadmap

Affective compaction must preserve causal structure. `MergeJob` and
`SummaryPromotionJob` need affective guardrails before they are enabled for
`AffectiveEpisodes`:

- do not merge if actor intention, target, goal impact, responsibility,
  controllability, outcome, relationship target, or resolution state differs;
- summaries must retain trigger, appraisal, goals, peak affect, action,
  outcome, prediction error, and unresolvedness;
- relationship summaries must retain evidence links.

## Implementation ladder

### E0 — Prototype through Custom

No public API freeze. Use `KnowledgeUnitKind::Custom` and typed metadata for:

- event;
- appraisal;
- affect before/after;
- goal impacts;
- action;
- outcome.

Add deterministic scenario and replay tests in a sibling affect-runtime project
or examples, not in core memory first.

### E1 — Formal components

Add typed component contracts once the fields stabilize:

- `AppraisalComponent`;
- `AffectSnapshotComponent`;
- `GoalImpactComponent`;
- `ActionOutcomeComponent`;
- `SalienceComponent`;
- `RetentionPolicyComponent`.

No affect-aware retrieval yet.

### E2 — Retrieval and relationship state

Add:

- `RelationshipStateComponent`;
- secondary indexes by `target_entity_id` and `goal_id`;
- appraisal similarity;
- unresolved episode retrieval;
- capped mood-congruent rerank.

### E3 — Compaction and outcome learning

Add:

- affective episode summaries;
- coping effectiveness statistics;
- prediction-error aggregation;
- relationship evidence-chain maintenance.

## Non-goals

Do not add these to `agent-memory-cpp` for the affective lane:

- neural emotion classifier in core;
- reinforcement learning runtime;
- end-to-end affect controller;
- another vector database;
- dozens of durable `EmotionKind` enum values;
- special embeddings only for `happy/sad/angry`.

Those belong to adapters, experiments, or sibling runtime projects.
