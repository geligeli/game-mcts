#include "web/risk_match.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <variant>

#include "absl/strings/str_join.h"
#include "game_mcts/core/mcts/policies.h"
#include "game_mcts/core/util/overloaded.h"
#include "game_mcts/games/risk/risk_board.h"
#include "game_mcts/games/risk/strategies/risk_proposer.h"

namespace risk_web {

namespace {

using risk_game::FortifyAction;
using risk_game::InitialPlaceAction;
using risk_game::PlayerAction;
using risk_game::QueueAttackAction;
using risk_game::QueueDefenseAction;
using risk_game::ReinforceAction;
using risk_game::RiskAction;
using risk_game::RollDiceAction;

const mcts::tournament::ProposerPolicy<state_t,
                                       risk_game::RiskProposer<kPlayers>>
    kRandom;

std::string Quoted(const std::string &text) {
  std::string out = "\"";
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (static_cast<unsigned char>(c) < 0x20) {
      // The captions' terminal colours among them: the page strips those.
      constexpr char kHex[] = "0123456789abcdef";
      out += "\\u00";
      out += kHex[(c >> 4) & 0xf];
      out += kHex[c & 0xf];
    } else {
      out += c;
    }
  }
  return out + "\"";
}

std::string KindOf(const RiskAction &action) {
  return std::visit(overloaded{
                        [](const InitialPlaceAction &) { return "place"; },
                        [](const PlayerAction &act) {
                          return act.attack_action_.has_value() ? "attack"
                                                                : "reinforce";
                        },
                        [](const QueueDefenseAction &) { return "defend"; },
                        [](const FortifyAction &) { return "fortify"; },
                        [](const RollDiceAction &) { return "roll"; },
                    },
                    action);
}

}  // namespace

RiskMatch::RiskMatch(std::vector<policy_t> bots, int human_seat, int max_rounds,
                     uint32_t seed)
    : bots_(std::move(bots)),
      human_(human_seat),
      max_rounds_(max_rounds),
      gen_(seed),
      renderer_(max_rounds),
      view_(tournament_broker::ViewJson(
          state_, tournament_broker::Rounds(state_), max_rounds, {})) {}

bool RiskMatch::Apply(const RiskAction &action, std::string *error) {
  std::string reason;
  if (!state_.is_valid_action(action, reason)) {
    *error = reason;
    return false;
  }
  const state_t before = state_;
  state_.apply_action_in_place(action);
  renderer_.Render(before, action, state_);
  view_ = renderer_.view();
  events_.push_back({.kind = KindOf(action),
                     .player = before.current_player_,
                     .caption = renderer_.caption(),
                     .view = view_});
  return true;
}

void RiskMatch::Settle() {
  std::string ignored;
  while (
      !tournament_broker::ResultOf(state_, max_rounds_, renderer_.eliminated())
           .over_) {
    if (state_.is_chance_node()) {
      Apply(state_.sample_chance_action(gen_), &ignored);
    } else if (state_.queued_attack_.has_value() &&
               !state_.queued_defense_.has_value() &&
               state_.current_player_ == human_) {
      const int target = state_.queued_attack_->target;
      Apply(QueueDefenseAction{.num_defend_dice_ =
                                   std::min<int>(2, state_.map_[target].units)},
            &ignored);
    } else {
      return;
    }
  }
}

RiskAction RiskMatch::BotAction() {
  if (fast_defense_ && state_.queued_attack_.has_value() &&
      !state_.queued_defense_.has_value()) {
    const int target = state_.queued_attack_->target;
    return QueueDefenseAction{.num_defend_dice_ =
                                  std::min<int>(2, state_.map_[target].units)};
  }
  const int seat = state_.current_player_;
  const RiskAction action =
      bots_[seat < human_ ? seat : seat - 1](state_, gen_).action;
  std::string reason;
  return state_.is_valid_action(action, reason) ? action
                                                : kRandom(state_, gen_).action;
}

bool RiskMatch::PlaceReserves(const placement_t &reinforce,
                              std::string *error) {
  if (state_.reserves_[human_] == 0 || !state_.first_attack_of_turn_) {
    return true;
  }
  return Apply(PlayerAction{.reinforce_action_ =
                                ReinforceAction{.units_to_place_ = reinforce},
                            .attack_action_ = std::nullopt},
               error);
}

std::string RiskMatch::State() const {
  std::string phase;
  const tournament_broker::RiskResult result =
      tournament_broker::ResultOf(state_, max_rounds_, renderer_.eliminated());
  if (result.over_) {
    phase = "over";
  } else if (state_.current_player_ != human_) {
    phase = "bot";
  } else if (state_.initial_placement_) {
    phase = "place";
  } else if (state_.reserves_[human_] > 0 && state_.first_attack_of_turn_) {
    phase = "reinforce";
  } else {
    phase = "attack";
  }
  return "{\"view\":" + view_ + ",\"phase\":\"" + phase +
         "\",\"seat\":" + std::to_string(human_) +
         ",\"reserves\":" + std::to_string(state_.reserves_[human_]) +
         ",\"result\":" + Quoted(result.text_) + ",\"places\":[" +
         absl::StrJoin(result.places_, ",") + "]}";
}

std::string RiskMatch::Answer(std::string error) {
  std::string json = "{\"events\":[";
  for (std::size_t i = 0; i < events_.size(); ++i) {
    const Event &e = events_[i];
    json += (i > 0 ? "," : "") + std::string("{\"k\":\"") + e.kind +
            "\",\"p\":" + std::to_string(e.player) +
            ",\"c\":" + Quoted(e.caption) + ",\"v\":" + e.view + "}";
  }
  events_.clear();
  std::string state = State();
  if (!error.empty()) {
    state.pop_back();
    state += ",\"error\":" + Quoted(error) + "}";
  }
  return json + "],\"state\":" + state + "}";
}

std::string RiskMatch::Place(int territory) {
  if (state_.current_player_ != human_) {
    return Answer("not your move");
  }
  std::string error;
  if (Apply(InitialPlaceAction{.territory_ = territory}, &error)) {
    Settle();
  }
  return Answer(error);
}

std::string RiskMatch::QuickSetup() {
  std::string ignored;
  while (state_.initial_placement_) {
    Apply(kRandom(state_, gen_).action, &ignored);
  }
  Settle();
  // One event: the dealt board, not eighty placements.
  if (!events_.empty()) {
    events_.erase(events_.begin(), events_.end() - 1);
    events_.back().kind = "setup";
    events_.back().caption = "The board is dealt";
  }
  return Answer();
}

std::string RiskMatch::Attack(int source, int target,
                              const placement_t &reinforce, bool blitz,
                              int move) {
  if (state_.current_player_ != human_ || state_.initial_placement_) {
    return Answer("not your move");
  }
  std::string error;
  // Placed on their own: the rules check an attack's dice against the armies
  // there before a reinforcement riding on the same action.
  if (!PlaceReserves(reinforce, &error)) {
    return Answer(error);
  }
  do {
    const PlayerAction attack{
        .reinforce_action_ = std::nullopt,
        .attack_action_ = QueueAttackAction{
            .source_ = source,
            .target = target,
            .num_attack_dice_ =
                std::min(3, static_cast<int>(state_.map_[source].units) - 1),
            .num_move_on_conquest_ = move}};
    if (!Apply(attack, &error)) {
      break;
    }
    while (state_.current_player_ != human_ && !state_.is_chance_node() &&
           !tournament_broker::ResultOf(state_, max_rounds_,
                                        renderer_.eliminated())
                .over_ &&
           Apply(BotAction(), &error)) {
      // the policy's defence
    }
    Settle();
  } while (blitz && state_.current_player_ == human_ &&
           state_.map_[target].owner != human_ &&
           state_.map_[source].units > 1);
  return Answer(error);
}

std::string RiskMatch::Fortify(int source, int target, int units,
                               const placement_t &reinforce) {
  if (state_.current_player_ != human_ || state_.initial_placement_) {
    return Answer("not your move");
  }
  std::string error;
  if (!PlaceReserves(reinforce, &error)) {
    return Answer(error);
  }
  if (Apply(
          FortifyAction{
              .source_ = source, .target = target, .num_units = units},
          &error)) {
    Settle();
  }
  return Answer(error);
}

std::string RiskMatch::BotStep() {
  if (state_.current_player_ == human_ || state_.is_chance_node() ||
      tournament_broker::ResultOf(state_, max_rounds_, renderer_.eliminated())
          .over_) {
    Settle();
    return Answer();
  }
  std::string error;
  Apply(BotAction(), &error);
  Settle();
  return Answer(error);
}

std::string BoardJson() {
  std::string names = "[";
  std::string neighbors = "[";
  for (const risk_game::CountryData &country : risk_game::kBoard) {
    names += (names.size() > 1 ? "," : "") + Quoted(std::string(country.name_));
    neighbors += neighbors.size() > 1 ? ",[" : "[";
    for (std::size_t i = 0; i < country.neighbor_count_; ++i) {
      neighbors += (i > 0 ? "," : "") +
                   std::to_string(static_cast<int>(country.neighbors_[i]));
    }
    neighbors += "]";
  }
  std::string continents = "[";
  for (const risk_game::ContinentData &continent : risk_game::kContinents) {
    continents +=
        (continents.size() > 1 ? "," : "") + std::string("{\"name\":") +
        Quoted(std::string(continent.name_)) +
        ",\"bonus\":" + std::to_string(continent.bonus_reinforcements_) +
        ",\"members\":[";
    for (std::size_t i = 0; i < continent.country_count_; ++i) {
      continents += (i > 0 ? "," : "") +
                    std::to_string(static_cast<int>(continent.countries_[i]));
    }
    continents += "]}";
  }
  return "{\"names\":" + names + "],\"neighbors\":" + neighbors +
         "],\"continents\":" + continents + "]}";
}

}  // namespace risk_web
