// A stored game, as the coordinator keeps it (GameRecord in the arena's
// tournament_broker.proto), decoded without a protobuf library: just the
// fields a replay needs.
//
//   GameRecord { 1 game_id, 2 game, 3 player_names*, 5 steps*, 6 result,
//                7 winning_player, 8 termination_reason, 11 initial_view,
//                13 places*, 14 forfeits* }
//   Step       { 1 player, 4 view (empty: unchanged), 5 caption }
//   Forfeit    { 1 seat, 2 reason, 3 move }
//
// Views are problem/risk_view.h's JSON; a step's view is carried forward when
// empty.

function reader(bytes) {
  let pos = 0;
  const varint = () => {
    let result = 0, shift = 0, byte;
    do {
      byte = bytes[pos++];
      result += (byte & 0x7f) * 2 ** shift;
      shift += 7;
    } while (byte & 0x80);
    return result;
  };
  return {
    done: () => pos >= bytes.length,
    varint,
    field() {
      const key = varint();
      const wire = key & 7, number = Math.floor(key / 8);
      if (wire === 0) return {number, value: varint()};
      if (wire === 2) {
        const length = varint();
        const value = bytes.subarray(pos, pos + length);
        pos += length;
        return {number, bytes: value};
      }
      if (wire === 1) { pos += 8; return {number}; }
      if (wire === 5) { pos += 4; return {number}; }
      throw new Error(`unsupported wire type ${wire}`);
    },
  };
}

const text = new TextDecoder();

// Signed int32 from a varint (negative values arrive as 64-bit two's
// complement, i.e. >= 2^63).
const int32 = (v) => (v >= 2 ** 31 ? v - 2 ** 64 : v);

// "1st", "2nd", ... for a place counted from 0.
export const ordinal = (place) => `${place + 1}${['st', 'nd', 'rd'][place] ?? 'th'}`;

// |places| by seat, as "1st a · 2nd b · 3rd c".
export const standings = (players, places) => places
    .map((place, seat) => [place, seat]).sort((a, b) => a[0] - b[0])
    .map(([place, seat]) => `${ordinal(place)} ${players[seat]}`).join(' · ');

export function decodeRecord(buffer) {
  const r = reader(new Uint8Array(buffer));
  // winning_player 0 is proto3's default, so it never arrives.
  const game = {id: '', game: '', players: [], steps: [], result: 0, winner: 0, reason: '',
                initialView: null, places: [], forfeits: []};
  while (!r.done()) {
    const f = r.field();
    if (f.number === 1) game.id = text.decode(f.bytes);
    else if (f.number === 2) game.game = text.decode(f.bytes);
    else if (f.number === 3) game.players.push(text.decode(f.bytes));
    else if (f.number === 6) game.result = f.value;
    else if (f.number === 7) game.winner = int32(f.value);
    else if (f.number === 8) game.reason = text.decode(f.bytes);
    else if (f.number === 11) game.initialView = JSON.parse(text.decode(f.bytes));
    else if (f.number === 5) {
      const s = reader(f.bytes);
      const step = {player: 0, view: null, caption: ''};
      while (!s.done()) {
        const g = s.field();
        if (g.number === 1) step.player = int32(g.value);
        else if (g.number === 4 && g.bytes.length) step.view = JSON.parse(text.decode(g.bytes));
        else if (g.number === 5) step.caption = text.decode(g.bytes);
      }
      game.steps.push(step);
    } else if (f.number === 13 && f.bytes) {
      const packed = reader(f.bytes);
      while (!packed.done()) game.places.push(packed.varint());
    } else if (f.number === 13) {
      game.places.push(f.value);
    } else if (f.number === 14) {
      const s = reader(f.bytes);
      const forfeit = {seat: 0, reason: '', move: 0};
      while (!s.done()) {
        const g = s.field();
        if (g.number === 1) forfeit.seat = g.value;
        else if (g.number === 2) forfeit.reason = text.decode(g.bytes);
        else if (g.number === 3) forfeit.move = g.value;
      }
      game.forfeits.push(forfeit);
    }
  }
  // Every step gets the board it left.
  let view = game.initialView;
  for (const step of game.steps) {
    step.view = step.view || view;
    view = step.view;
  }
  return game;
}

// What a step did, from its caption's shape and its view's marks: "roll" when
// it has dice, "setup" in initial placement, else a coarse kind.
export function kindOf(prev, view) {
  if (view.d) return 'roll';
  if (view.r === 0 && prev && prev.r === 0) return 'place';
  if (view.a) return 'attack';
  return 'move';
}
