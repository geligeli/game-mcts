#ifndef GAME_MCTS_GAME_MCTS_CORE_MCTS_POLICIES_H
#define GAME_MCTS_GAME_MCTS_CORE_MCTS_POLICIES_H

// Stock TournamentPolicy adapters -- a proposer as a uniform-random player,
// and game-generic MCTS -- plus SerializedPolicy, which lifts any policy to
// serialized state bytes -> serialized action bytes: the shape a referee's
// builtin and a remote bot both speak.

#include <functional>
#include <random>
#include <string>
#include <string_view>
#include <utility>

#include "game_mcts/core/mcts/game_traits.h"
#include "game_mcts/core/mcts/mcts.inl"  // picker/runner definitions (auto return types)
#include "game_mcts/core/mcts/serialization.h"
#include "game_mcts/core/mcts/tournament.h"

namespace mcts::tournament {

// Uniformly random valid move drawn with the game's proposer.
template <mcts::Game G, mcts::ActionProposer<G> PROPOSER>
struct ProposerPolicy {
  PROPOSER proposer_{};

  PolicyDecision<G> operator()(const G &game, std::mt19937 &gen) const {
    const typename G::action_t action = proposer_.sample(game, gen);
    return {.action = action, .successor = game.apply_action(action)};
  }
};

// Game-generic MCTS: |PROPOSER| expands the tree, |ROLLOUT| evaluates
// playouts, and the move is the robust child.
template <mcts::Game G, mcts::ActionProposer<G> PROPOSER, typename ROLLOUT>
struct MctsPolicy {
  int iterations_ = 400;
  double widening_c_ = 2.0;
  double widening_alpha_ = 0.5;
  double exploration_c_ = 1.0;
  PROPOSER proposer_{};
  ROLLOUT rollout{};

  PolicyDecision<G> operator()(const G &game, std::mt19937 &gen) const {
    mcts::MctsRunner<G, PROPOSER, ROLLOUT> runner(game, proposer_, rollout);
    if constexpr (mcts::ChanceGame<G>) {
      auto picker = mcts::MctsStochasticNodePicker<G>(
          gen, widening_c_, widening_alpha_, exploration_c_);
      for (int i = 0; i < iterations_; ++i) {
        runner.OneIteration(picker, gen);
      }
    } else {
      auto picker = mcts::MctsNodePicker<G>(gen, widening_c_, widening_alpha_,
                                            exploration_c_);
      for (int i = 0; i < iterations_; ++i) {
        runner.OneIteration(picker, gen);
      }
    }
    const typename G::action_t action = runner.best_action();
    return {.action = action, .successor = game.apply_action(action)};
  }
};

using SerializedPolicyFn =
    std::function<std::string(std::string_view state_bytes, std::mt19937 &gen)>;

// A policy over serialized protos (mcts::GameSerializationTraits<G>).
template <mcts::SerializableGame G, TournamentPolicy<G> P>
SerializedPolicyFn SerializedPolicy(P policy) {
  return [policy = std::move(policy)](std::string_view state_bytes,
                                      std::mt19937 &gen) -> std::string {
    using traits = mcts::GameSerializationTraits<G>;
    typename traits::state_proto_t state_proto;
    state_proto.ParseFromArray(state_bytes.data(),
                               static_cast<int>(state_bytes.size()));
    const PolicyDecision<G> decision =
        policy(traits::StateFromProto(state_proto), gen);
    return traits::ActionToProto(decision.action).SerializeAsString();
  };
}

}  // namespace mcts::tournament

#endif  // GAME_MCTS_GAME_MCTS_CORE_MCTS_POLICIES_H
