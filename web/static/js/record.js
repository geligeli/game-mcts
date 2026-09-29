// A stored game, as the coordinator keeps it (GameRecord in the arena's
// tournament_broker.proto), decoded without a protobuf library: just the
// fields a replay needs.
//
//   GameRecord { 1 game_id, 3 player_names*, 5 steps*, 6 result,
//                7 winning_player, 8 termination_reason, 11 initial_view }
//   Step       { 1 player, 4 view (empty: unchanged), 5 caption }
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

export function decodeRecord(buffer) {
  const r = reader(new Uint8Array(buffer));
  const game = {id: '', players: [], steps: [], result: 0, winner: -1, reason: '', initialView: null};
  while (!r.done()) {
    const f = r.field();
    if (f.number === 1) game.id = text.decode(f.bytes);
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
