// The play page: a person against a candidate in each other seat, whose own
// policy runs as WebAssembly in engine_worker.js. The first opponent's module
// holds the engine (web/risk_match.h), the referee; a second opponent's
// decides for its seat, the engine's outside seat. This page only turns
// clicks into their calls and the engine's answers into animation.
//
// ?bots=<id>,<id>&seat=0|1|2&setup=quick|draft&fast=1|0&rounds=40[&autoplay=1]
// (?bot=<id> seats one in both), an id a candidate or builtin:<name>.
//
// The engine answers at once and never waits for the board: a presenter plays
// its steps back, faster the more are waiting, so clicking through a battle
// is click, click, click.

import {World, TEAMS} from './risk3d.js';
import {Engine, placementString} from './engine.js';
import {ordinal} from './record.js';
import {sfx} from './sfx.js';
import {$, updateSeats, showDice, setCaption, banner, hideBanner, soundButtons,
        progress, doneLoading, failLoading, holdings} from './hud.js';

const params = new URLSearchParams(location.search);
const seat = Number(params.get('seat') || 0);
// The other seats' opponents, in seat order.
const opponents = (params.get('bots') || params.get('bot') || 'reference').split(',');
if (opponents.length < 2) opponents.push(opponents[0]);
// The builtins are one module, which params choose among.
const moduleOf = (id) => (id.startsWith('builtin:') ? 'builtin' : id);
const paramsOf = (id) => (id.startsWith('builtin:') ? `builtin=${id.slice(8)}` : '');
const rounds = Number(params.get('rounds') ?? 40);
const autoplay = params.get('autoplay') === '1';

const world = new World($('stage'));
window.world = world;  // for the console, and tests
let engine, board;
let outside = null;        // the second opponent's module, if it is another
let state = null;          // the engine's latest answer's state: the truth
let placement = {};        // armies placed but not yet sent
let selected = -1;         // attack source, or fortify source
let mode = 'attack';       // attack | fortify
let lastAttack = null;     // {source, target} for Space
let blitz = false;
// On a conquest: everything but one army, or only the dice (the rules' least).
let moveAll = true;
try { moveAll = localStorage.getItem('risk2.moveAll') !== '0'; } catch {}
const MOVE_ALL = 1 << 16;  // QueueAttackAction::kMoveAll
let busy = false;          // an engine call is out
const others = TEAMS.map((_, s) => s).filter((s) => s !== seat);
const names = TEAMS.map((_, s) => (s === seat ? 'You' : opponents[others.indexOf(s)]));
// The second opponent's seat, when another module plays it.
const outsideSeat = opponents[1] !== opponents[0] ? others[1] : -1;
const botsTurn = () => state.phase === 'bot' || state.phase === 'outside';

// ---- The presenter: plays engine steps on the board, in order. ----

const queue = [];
let presented = null;      // the view the board shows, or is animating to
let presenting = null;

function present(events) {
  queue.push(...events);
  if (!presenting) presenting = drain().finally(() => { presenting = null; });
  return presenting;
}

async function drain() {
  while (queue.length) {
    const event = queue.shift();
    const {v: view, c: caption} = event;
    if (event.k === 'attack' || event.k === 'defend') {
      presented = view;  // no board change; the roll that follows shows it
      continue;
    }
    setCaption(caption);
    updateSeats(view, names);
    // A backlog plays faster: the board catches up with the engine.
    const speed = Math.min(8, 1.6 + queue.length * 1.2);
    if (view.d) sfx.play('throw', {volume: 0.7});
    await world.play(presented, view, {speed});
    presented = view;
  }
}

// ---- Engine calls ----

async function call(cmd, ...args) {
  busy = true;
  try {
    const answer = await engine.call(cmd, ...args);
    state = answer.state;
    if (state.error) toast(state.error);
    present(answer.events);
    return answer;
  } finally {
    busy = false;
  }
}

let toastTimer = 0;
function toast(text) {
  const node = $('toast');
  node.textContent = text;
  node.classList.remove('hidden');
  sfx.play('deny');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => node.classList.add('hidden'), 2600);
}

// ---- Rules the page needs to offer moves (the engine enforces them) ----

const nameOf = (t) => board.names[t].replace(/_/g, ' ');
const owner = (t) => (state.view.o[t] === '-' ? -1 : Number(state.view.o[t]));
const units = (t) => state.view.u[t] + (placement[t] || 0);
const placed = () => Object.values(placement).reduce((a, b) => a + b, 0);
const remaining = () => (state.phase === 'reinforce' ? state.reserves - placed() : 0);
const myTurn = () => ['place', 'reinforce', 'attack'].includes(state.phase);
const canAttackFrom = (t) => owner(t) === seat && units(t) >= 2;
const targetsOf = (t) => board.neighbors[t].filter((n) => owner(n) !== seat);

function refresh() {
  if (!state) return;
  const phase = state.phase;
  const reinforcing = phase === 'reinforce';
  const attacking = (phase === 'attack' || (reinforcing && remaining() === 0)) && mode === 'attack';
  const fortifying = myTurn() && phase !== 'place' && mode === 'fortify';
  $('reserve').hidden = !reinforcing;
  $('reserveCount').textContent = remaining();
  $('undo').disabled = !reinforcing || !placed();
  $('endattacks').disabled = !(phase === 'attack' || (reinforcing && remaining() === 0)) || mode === 'fortify';
  $('endturn').disabled = !(phase === 'attack' || (reinforcing && remaining() === 0));
  $('blitz').classList.toggle('on', blitz);
  $('moveall').classList.toggle('on', moveAll);
  world.setPending(placement);

  const title = $('phase'), hint = $('hint');
  const marks = {selected: -1, targets: new Set(), sources: new Set()};
  if (phase === 'over') {
    title.textContent = 'The war is over';
    hint.textContent = state.result;
  } else if (botsTurn()) {
    title.textContent = `${names[state.view.p]}'s turn`;
    hint.textContent = 'Its own code is choosing, in your browser.';
  } else if (phase === 'place') {
    title.textContent = 'Claim the land';
    hint.textContent = state.view.o.includes('-')
        ? 'Click an unclaimed territory to claim it.'
        : 'Click one of your territories to add an army.';
    for (let t = 0; t < 42; ++t) {
      if (state.view.o.includes('-') ? owner(t) < 0 : owner(t) === seat) marks.sources.add(t);
    }
  } else if (reinforcing && remaining() > 0) {
    title.textContent = 'Reinforce';
    hint.innerHTML = 'Click your territories to place armies. <b>Shift</b> +5, right-click −1.';
    for (let t = 0; t < 42; ++t) if (owner(t) === seat) marks.sources.add(t);
  } else if (fortifying) {
    title.textContent = 'Fortify';
    hint.innerHTML = selected < 0
        ? 'Choose where armies march <b>from</b>, or <b>End turn</b>.'
        : `From <b>${nameOf(selected)}</b> to which territory of yours?`;
    for (let t = 0; t < 42; ++t) {
      if (owner(t) === seat && (selected < 0 ? units(t) >= 2 : t !== selected)) marks.sources.add(t);
    }
    marks.selected = selected;
  } else if (attacking) {
    title.textContent = 'Attack';
    hint.innerHTML = selected < 0
        ? 'Choose a territory to attack <b>from</b> (2+ armies).'
        : `Click an enemy next to <b>${nameOf(selected)}</b>: one roll per click` +
          (blitz ? ', <b>blitz</b> is on.' : ', hold or <b>Shift</b> to blitz.');
    for (let t = 0; t < 42; ++t) if (canAttackFrom(t) && targetsOf(t).length) marks.sources.add(t);
    if (selected >= 0) {
      marks.selected = selected;
      targetsOf(selected).forEach((t) => marks.targets.add(t));
    }
  }
  world.setMarks(marks);
}

// ---- Turn flow ----

async function afterMove() {
  if (!state) return;
  if (botsTurn()) await botTurn();
  if (state.phase === 'over') return finish();
  if (myTurn() && state.phase !== 'place' && mode !== 'attack') mode = 'attack';
  refresh();
  if (autoplay) setTimeout(autoMove, 30);
}

let thinkingTimer = 0;
async function botTurn() {
  selected = -1;
  mode = 'attack';
  refresh();
  while (botsTurn()) {
    banner(`${names[state.view.p]} is thinking…`, 'its own C++, as WebAssembly');
    $('banner').prepend(Object.assign(document.createElement('span'), {className: 'spinner'}));
    if (state.phase === 'outside') {
      const snapshot = await engine.call('snapshot');
      const decision = await outside.call('decide', snapshot.bytes);
      await call('act', decision.bytes);
    } else {
      await call('bot_step');
    }
  }
  hideBanner();
  await presenting;
  if (myTurn()) {
    sfx.play('turn');
    banner('Your turn', state.phase === 'reinforce' ? `${state.reserves} armies to place` : '', 1300);
  }
}

async function attack(source, target, all) {
  if (busy) return;
  const sending = placementString(placement);
  lastAttack = {source, target};
  const answer = await call('attack', source, target, sending, all ? 1 : 0,
                            moveAll ? MOVE_ALL : 0);
  // Placed once the engine has no reserves left to place.
  if (state.phase !== 'reinforce') placement = {};
  // Conquered, and nothing left to attack with: carry on from the new land.
  if (owner(target) === seat) {
    selected = canAttackFrom(source) && targetsOf(source).length ? source
             : canAttackFrom(target) && targetsOf(target).length ? target : -1;
  } else if (!canAttackFrom(source)) {
    selected = -1;
  }
  refresh();
  if (state.phase !== 'attack' && state.phase !== 'reinforce') await afterMove();
  return answer;
}

async function fortify(source, target, count) {
  if (busy) return;
  const sending = placementString(placement);
  await call('fortify', source, target, count, sending);
  if (state.error) return refresh();
  placement = {};
  selected = -1;
  mode = 'attack';
  await afterMove();
}

function finish() {
  refresh();
  const {territories, armies} = holdings(state.view);
  const {places} = state;
  const first = places[seat] === 0;
  const draw = first && places.filter((p) => p === 0).length > 1;
  const won = first && !draw;
  // Only the last place is a defeat: outlasting a rival counts.
  const last = places[seat] === Math.max(...places);
  $('resultTitle').textContent = draw ? 'A draw' : won ? 'Victory'
      : last ? 'Defeat' : `${ordinal(places[seat])} place`;
  $('resultTitle').style.color = draw || !last && !won ? 'var(--gold-hi)' : won ? 'var(--good)' : 'var(--bad)';
  $('resultText').textContent = `You placed ${ordinal(places[seat])}. ${state.result}`;
  const stats = $('resultStats');
  stats.textContent = '';
  for (const s of [...places.keys()].sort((a, b) => places[a] - places[b])) {
    const row = [`${ordinal(places[s])} ${names[s]}`, `${territories[s]} territories`, `${armies[s]} armies`]
        .map((textContent) => Object.assign(document.createElement('div'), {textContent}));
    row[0].style.color = TEAMS[s].css;
    stats.append(...row);
  }
  sfx.play(won ? 'win' : 'lose');
  sfx.loop('drums', false);
  setTimeout(() => $('result').classList.remove('hidden'), 900);
  document.title = `PASS ${ordinal(places[seat])}: ${state.result}`;
}

// ---- Input ----

let pressed = null;  // {t, at, shift}
let rightPress = null;

function onPress(e) {
  if (e.button === 2) rightPress = {x: e.clientX, y: e.clientY};
  if (e.button !== 0) return;
  pressed = {t: world.pick(e.clientX, e.clientY), at: performance.now(), shift: e.shiftKey};
}

async function onClick(e) {
  if (!pressed || world.dragged || !state) return;
  const {t, at, shift} = pressed;
  pressed = null;
  if (t < 0 || !myTurn()) return;
  const held = performance.now() - at > 350;
  const phase = state.phase;
  if (phase === 'place') {
    sfx.play('click');
    await call('place', t);
    return afterMove();
  }
  if (phase === 'reinforce' && remaining() > 0) {
    if (owner(t) !== seat) return toast('Place armies on your own territories');
    const add = Math.min(remaining(), shift ? 5 : 1);
    placement[t] = (placement[t] || 0) + add;
    sfx.play('drop', {volume: 0.5});
    world.fx.text(world.anchors[t], `+${add}`, '#f3d995');
    return refresh();
  }
  if (mode === 'fortify') {
    if (owner(t) !== seat) return;
    if (selected < 0 || t === selected) {
      if (units(t) < 2) return toast('Pick a territory with at least 2 armies');
      selected = t === selected ? -1 : t;
      sfx.play('select');
      return refresh();
    }
    return openFortify(selected, t, e);
  }
  // Attack.
  if (owner(t) === seat) {
    if (t === selected) selected = -1;
    else if (canAttackFrom(t)) selected = t;
    else return toast('Attack from a territory with at least 2 armies');
    sfx.play('select');
    return refresh();
  }
  if (selected < 0 || !board.neighbors[selected].includes(t)) {
    const from = board.neighbors[t].filter((n) => canAttackFrom(n));
    if (from.length) {
      selected = from.reduce((a, b) => (units(a) >= units(b) ? a : b));
    } else {
      return toast('None of your armies can reach it');
    }
  }
  attack(selected, t, blitz || shift || held);
}

function onRightClick(e) {
  e.preventDefault();
  // Releasing a right-drag (the camera's orbit) is not a click.
  const orbited = rightPress && Math.hypot(e.clientX - rightPress.x, e.clientY - rightPress.y) > 6;
  if (!state || state.phase !== 'reinforce' || orbited) return;
  const t = world.pick(e.clientX, e.clientY);
  if (t >= 0 && placement[t]) {
    placement[t] -= 1;
    if (!placement[t]) delete placement[t];
    sfx.play('click');
    refresh();
  }
}

let hovered = -1;
function onMove(e) {
  if (!state || !board) return;
  const t = world.pick(e.clientX, e.clientY);
  const tip = $('tip');
  if (t !== hovered) {
    hovered = t;
    world.setMarks({hover: t});
  }
  if (t < 0) return tip.classList.add('hidden');
  const o = owner(t);
  const continent = board.continents.find((c) => c.members.includes(t));
  tip.textContent = '';
  const title = document.createElement('b');
  title.textContent = nameOf(t);
  const line = document.createElement('div');
  line.textContent = `${o < 0 ? 'Unclaimed' : o === seat ? 'Yours' : names[o]} · ${state.view.u[t]} armies` +
                     (placement[t] ? ` (+${placement[t]})` : '');
  const cont = document.createElement('div');
  cont.className = 'muted';
  cont.textContent = `${continent.name} (+${continent.bonus} if held whole)`;
  tip.append(title, line, cont);
  if (o >= 0) title.style.color = TEAMS[o].css;
  tip.style.left = `${e.clientX}px`;
  tip.style.top = `${e.clientY}px`;
  tip.classList.remove('hidden');
}

function openFortify(source, target, e) {
  const box = $('fortify');
  const max = units(source) - 1;
  $('fortifyText').textContent = `${nameOf(source)} → ${nameOf(target)}`;
  const range = $('fortifyRange');
  range.max = max;
  range.value = max;
  $('fortifyCount').textContent = max;
  range.oninput = () => { $('fortifyCount').textContent = range.value; };
  box.style.left = `${Math.min(innerWidth - 250, e.clientX + 12)}px`;
  box.style.top = `${Math.min(innerHeight - 160, e.clientY + 12)}px`;
  box.classList.remove('hidden');
  $('fortifyGo').onclick = () => {
    box.classList.add('hidden');
    fortify(source, target, Number(range.value));
  };
  $('fortifyCancel').onclick = () => box.classList.add('hidden');
  range.focus();
}

function endTurn() {
  if (!state || $('endturn').disabled) return;
  if (!$('fortify').classList.contains('hidden')) return $('fortifyGo').onclick();
  fortify(0, 0, 0);
}

function onKey(e) {
  if (e.target.tagName === 'INPUT' && e.key !== 'Enter') return;
  const key = e.key.toLowerCase();
  if (key === ' ') {
    e.preventDefault();
    if (lastAttack && canAttackFrom(lastAttack.source) && owner(lastAttack.target) !== seat) {
      attack(lastAttack.source, lastAttack.target, blitz);
    }
  } else if (key === 'b') {
    blitz = !blitz;
    refresh();
  } else if (key === 'a') {
    $('moveall').click();
  } else if (key === 'e') {
    $('endattacks').click();
  } else if (key === 'enter') {
    endTurn();
  } else if (key === 'escape') {
    selected = -1;
    $('fortify').classList.add('hidden');
    $('helpbox').classList.add('hidden');
    refresh();
  } else if (key === 'u') {
    $('undo').click();
  } else if (key === '?') {
    $('helpbox').classList.remove('hidden');
  }
}

// ---- The human side, played by the page itself (?autoplay=1, for tests) ----

async function autoMove() {
  if (!state || busy || !myTurn()) return;
  if (state.phase === 'place') {
    const free = [...state.view.o].indexOf('-');
    await call('place', free >= 0 ? free : [...state.view.o].indexOf(String(seat)));
    return afterMove();
  }
  // Everything on the strongest border territory, then blitz its weakest
  // neighbour while that looks good, then end the turn.
  const fronts = [];
  for (let t = 0; t < 42; ++t) if (owner(t) === seat && targetsOf(t).length) fronts.push(t);
  const front = fronts.reduce((a, b) => (units(a) >= units(b) ? a : b));
  if (state.phase === 'reinforce' && remaining() > 0) {
    placement[front] = (placement[front] || 0) + remaining();
    refresh();
  }
  for (let i = 0; i < 3 && myTurn(); ++i) {
    const source = fronts.filter((t) => owner(t) === seat)
        .reduce((a, b) => (units(a) >= units(b) ? a : b), front);
    const targets = targetsOf(source);
    if (!targets.length || units(source) < 3) break;
    const target = targets.reduce((a, b) => (state.view.u[a] <= state.view.u[b] ? a : b));
    if (units(source) <= state.view.u[target] + 1) break;
    await attack(source, target, true);
  }
  if (myTurn()) await fortify(0, 0, 0);
}

// ---- Start ----

async function main() {
  $('loadtitle').textContent = `Summoning ${[...new Set(opponents)].join(' and ')}…`;
  let fraction = 0;
  const load = (id) => Engine.load(moduleOf(id)).then((e) => { progress((fraction += 0.15)); return e; });
  const loading = Promise.all([load(opponents[0]), outsideSeat >= 0 ? load(opponents[1]) : null]);
  await world.load((f) => progress(Math.min(0.99, f * 0.7 + fraction)));
  [engine, outside] = await loading;
  board = engine.board;
  progress(1);
  soundButtons(world);
  sfx.preload();
  engine.call('fast_defense', params.get('fast') === '0' ? 0 : 1);
  const seed = crypto.getRandomValues(new Uint32Array(1))[0];
  // A new game answers its state alone: nothing has been played.
  state = await engine.call('new', seat, rounds, seed, paramsOf(opponents[0]), outsideSeat);
  if (outside) await outside.call('policy', (seed ^ 0x9e3779b9) >>> 0, paramsOf(opponents[1]));
  presented = state.view;
  world.setView(state.view);
  updateSeats(state.view, names);
  document.title = `You vs ${[...new Set(opponents)].join(' & ')} · risk3`;
  doneLoading();
  sfx.loop('drums', true);

  const canvas = world.renderer.domElement;
  canvas.addEventListener('pointerdown', onPress);
  canvas.addEventListener('pointerup', (e) => setTimeout(() => onClick(e), 0));
  canvas.addEventListener('contextmenu', onRightClick);
  canvas.addEventListener('pointermove', onMove);
  canvas.addEventListener('pointerleave', () => $('tip').classList.add('hidden'));
  addEventListener('keydown', onKey);
  $('blitz').onclick = () => { blitz = !blitz; refresh(); };
  $('moveall').onclick = () => {
    moveAll = !moveAll;
    try { localStorage.setItem('risk2.moveAll', moveAll ? '1' : '0'); } catch {}
    refresh();
  };
  $('undo').onclick = () => { placement = {}; sfx.play('click'); refresh(); };
  $('endattacks').onclick = () => {
    if ($('endattacks').disabled) return;
    mode = 'fortify';
    selected = -1;
    sfx.play('select');
    refresh();
  };
  $('endturn').onclick = endTurn;
  $('rematch').onclick = () => location.reload();
  $('resultClose').onclick = () => $('result').classList.add('hidden');
  $('help').onclick = () => $('helpbox').classList.remove('hidden');
  $('helpClose').onclick = () => $('helpbox').classList.add('hidden');

  if (params.get('setup') !== 'draft') {
    await call('quick_setup');
    await presenting;
  }
  await afterMove();
  document.body.dataset.ready = '1';
}

window.play = {get state() { return state; }, get board() { return board; },
               get placement() { return placement; }, get busy() { return busy; }};

main().catch(failLoading);
