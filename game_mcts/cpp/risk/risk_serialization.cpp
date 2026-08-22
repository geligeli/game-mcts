#include "game_mcts/cpp/risk/risk_serialization.h"

#include <cstddef>
#include <cstdint>
#include <limits>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "game_mcts/cpp/mcts/overloaded.h"

namespace mcts {

namespace {

namespace rproto = risk_game::proto;

auto TerritoryToProto(const risk_game::Territory &territory)
    -> rproto::Territory {
  rproto::Territory proto;
  proto.set_owner(territory.owner);
  proto.set_units(territory.units);
  return proto;
}

auto TerritoryFromProto(const rproto::Territory &proto,
                        int num_players) -> risk_game::Territory {
  CHECK_GE(proto.owner(), -1);
  CHECK_LT(proto.owner(), num_players);
  CHECK_LE(proto.units(), std::numeric_limits<uint16_t>::max());
  return risk_game::Territory{.owner = static_cast<int8_t>(proto.owner()),
                              .units = static_cast<uint16_t>(proto.units())};
}

auto QueueAttackToProto(const risk_game::QueueAttackAction &action)
    -> rproto::QueueAttackAction {
  rproto::QueueAttackAction proto;
  proto.set_source(action.source);
  proto.set_target(action.target);
  proto.set_num_attack_dice(action.num_attack_dice);
  return proto;
}

auto QueueAttackFromProto(const rproto::QueueAttackAction &proto)
    -> risk_game::QueueAttackAction {
  return risk_game::QueueAttackAction{
      .source = proto.source(),
      .target = proto.target(),
      .num_attack_dice = proto.num_attack_dice()};
}

auto QueueDefenseToProto(const risk_game::QueueDefenseAction &action)
    -> rproto::QueueDefenseAction {
  rproto::QueueDefenseAction proto;
  proto.set_num_defend_dice(action.num_defend_dice);
  return proto;
}

auto QueueDefenseFromProto(const rproto::QueueDefenseAction &proto)
    -> risk_game::QueueDefenseAction {
  return risk_game::QueueDefenseAction{.num_defend_dice =
                                           proto.num_defend_dice()};
}

auto ReinforceToProto(const risk_game::ReinforceAction &action)
    -> rproto::ReinforceAction {
  rproto::ReinforceAction proto;
  proto.mutable_units_to_place()->Reserve(
      static_cast<int>(action.units_to_place.size()));
  for (uint16_t units : action.units_to_place) {
    proto.add_units_to_place(units);
  }
  return proto;
}

auto ReinforceFromProto(const rproto::ReinforceAction &proto)
    -> risk_game::ReinforceAction {
  CHECK_EQ(proto.units_to_place_size(),
           static_cast<int>(risk_game::kNumTerritories));
  risk_game::ReinforceAction action;
  for (int i = 0; i < proto.units_to_place_size(); ++i) {
    CHECK_LE(proto.units_to_place(i), std::numeric_limits<uint16_t>::max());
    action.units_to_place[static_cast<std::size_t>(i)] =
        static_cast<uint16_t>(proto.units_to_place(i));
  }
  return action;
}

auto PlayerActionToProto(const risk_game::PlayerAction &action)
    -> rproto::PlayerAction {
  rproto::PlayerAction proto;
  if (action.reinforce_action.has_value()) {
    *proto.mutable_reinforce_action() =
        ReinforceToProto(*action.reinforce_action);
  }
  if (action.attack_action.has_value()) {
    *proto.mutable_attack_action() = QueueAttackToProto(*action.attack_action);
  }
  return proto;
}

auto PlayerActionFromProto(const rproto::PlayerAction &proto)
    -> risk_game::PlayerAction {
  risk_game::PlayerAction action;
  if (proto.has_reinforce_action()) {
    action.reinforce_action = ReinforceFromProto(proto.reinforce_action());
  }
  if (proto.has_attack_action()) {
    action.attack_action = QueueAttackFromProto(proto.attack_action());
  }
  return action;
}

auto RollDiceToProto(const risk_game::RollDiceAction &action)
    -> rproto::RollDiceAction {
  rproto::RollDiceAction proto;
  for (int roll : action.attacker_rolls) {
    proto.add_attacker_rolls(roll);
  }
  for (int roll : action.defender_rolls) {
    proto.add_defender_rolls(roll);
  }
  return proto;
}

auto RollDiceFromProto(const rproto::RollDiceAction &proto)
    -> risk_game::RollDiceAction {
  CHECK_EQ(proto.attacker_rolls_size(), 3);
  CHECK_EQ(proto.defender_rolls_size(), 2);
  risk_game::RollDiceAction action;
  for (int i = 0; i < 3; ++i) {
    action.attacker_rolls[static_cast<std::size_t>(i)] =
        proto.attacker_rolls(i);
  }
  for (int i = 0; i < 2; ++i) {
    action.defender_rolls[static_cast<std::size_t>(i)] =
        proto.defender_rolls(i);
  }
  return action;
}

}  // namespace

template <std::size_t NUM_PLAYERS>
auto GameSerializationTraits<risk_game::RiskState<NUM_PLAYERS>>::StateToProto(
    const risk_game::RiskState<NUM_PLAYERS> &state) -> state_proto_t {
  state_proto_t proto;
  proto.mutable_reserves()->Reserve(static_cast<int>(NUM_PLAYERS));
  for (uint16_t reserve : state.m_reserves) {
    proto.add_reserves(reserve);
  }
  proto.mutable_territories()->Reserve(
      static_cast<int>(risk_game::kNumTerritories));
  for (const risk_game::Territory &territory : state.m_map) {
    *proto.add_territories() = TerritoryToProto(territory);
  }
  proto.set_initial_placement(state.m_initial_placement);
  proto.set_num_initial_placements(state.m_num_initial_placements);
  proto.set_first_attack_of_turn(state.m_first_attack_of_turn);
  proto.set_current_player(state.m_current_player);
  proto.set_turn_count(state.turn_count);
  if (state.queued_attack.has_value()) {
    *proto.mutable_queued_attack() = QueueAttackToProto(*state.queued_attack);
  }
  if (state.queued_defense.has_value()) {
    *proto.mutable_queued_defense() =
        QueueDefenseToProto(*state.queued_defense);
  }
  proto.set_num_players(NUM_PLAYERS);
  return proto;
}

template <std::size_t NUM_PLAYERS>
auto GameSerializationTraits<risk_game::RiskState<NUM_PLAYERS>>::StateFromProto(
    const state_proto_t &proto) -> risk_game::RiskState<NUM_PLAYERS> {
  CHECK_EQ(proto.num_players(), NUM_PLAYERS);
  CHECK_EQ(proto.territories_size(),
           static_cast<int>(risk_game::kNumTerritories));
  CHECK_EQ(proto.reserves_size(), static_cast<int>(NUM_PLAYERS));
  CHECK_GE(proto.current_player(), -1);
  CHECK_LT(proto.current_player(), static_cast<int>(NUM_PLAYERS));

  risk_game::RiskState<NUM_PLAYERS> state;
  for (int p = 0; p < proto.reserves_size(); ++p) {
    CHECK_LE(proto.reserves(p), std::numeric_limits<uint16_t>::max());
    state.m_reserves[static_cast<std::size_t>(p)] =
        static_cast<uint16_t>(proto.reserves(p));
  }
  for (int i = 0; i < proto.territories_size(); ++i) {
    state.m_map[static_cast<std::size_t>(i)] =
        TerritoryFromProto(proto.territories(i), NUM_PLAYERS);
  }
  state.m_initial_placement = proto.initial_placement();
  state.m_num_initial_placements = proto.num_initial_placements();
  state.m_first_attack_of_turn = proto.first_attack_of_turn();
  state.m_current_player = static_cast<int8_t>(proto.current_player());
  state.turn_count = proto.turn_count();
  if (proto.has_queued_attack()) {
    state.queued_attack = QueueAttackFromProto(proto.queued_attack());
  }
  if (proto.has_queued_defense()) {
    state.queued_defense = QueueDefenseFromProto(proto.queued_defense());
  }
  return state;
}

template <std::size_t NUM_PLAYERS>
auto GameSerializationTraits<risk_game::RiskState<NUM_PLAYERS>>::ActionToProto(
    const risk_game::RiskAction &action) -> action_proto_t {
  action_proto_t proto;
  std::visit(overloaded{
                 [&](const risk_game::InitialPlaceAction &a) {
                   proto.mutable_initial_place()->set_territory(a.territory);
                 },
                 [&](const risk_game::PlayerAction &a) {
                   *proto.mutable_player_action() = PlayerActionToProto(a);
                 },
                 [&](const risk_game::QueueDefenseAction &a) {
                   *proto.mutable_queue_defense() = QueueDefenseToProto(a);
                 },
                 [&](const risk_game::FortifyAction &a) {
                   auto *fortify = proto.mutable_fortify();
                   fortify->set_source(a.source);
                   fortify->set_target(a.target);
                   fortify->set_num_units(a.num_units);
                 },
                 [&](const risk_game::RollDiceAction &a) {
                   *proto.mutable_roll_dice() = RollDiceToProto(a);
                 },
             },
             action);
  return proto;
}

template <std::size_t NUM_PLAYERS>
auto GameSerializationTraits<risk_game::RiskState<NUM_PLAYERS>>::
    ActionFromProto(const action_proto_t &proto) -> risk_game::RiskAction {
  switch (proto.action_case()) {
    case action_proto_t::kInitialPlace:
      return risk_game::RiskAction{risk_game::InitialPlaceAction{
          .territory = proto.initial_place().territory()}};
    case action_proto_t::kPlayerAction:
      return risk_game::RiskAction{
          PlayerActionFromProto(proto.player_action())};
    case action_proto_t::kQueueDefense:
      return risk_game::RiskAction{
          QueueDefenseFromProto(proto.queue_defense())};
    case action_proto_t::kFortify:
      return risk_game::RiskAction{
          risk_game::FortifyAction{.source = proto.fortify().source(),
                                   .target = proto.fortify().target(),
                                   .num_units = proto.fortify().num_units()}};
    case action_proto_t::kRollDice:
      return risk_game::RiskAction{RollDiceFromProto(proto.roll_dice())};
    case action_proto_t::ACTION_NOT_SET:
      break;
  }
  LOG(FATAL) << "RiskAction proto has no action set";
}

template struct GameSerializationTraits<risk_game::RiskState<2>>;
template struct GameSerializationTraits<risk_game::RiskState<3>>;
template struct GameSerializationTraits<risk_game::RiskState<4>>;
template struct GameSerializationTraits<risk_game::RiskState<5>>;
template struct GameSerializationTraits<risk_game::RiskState<6>>;

}  // namespace mcts
