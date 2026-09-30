// The 3D viewer: plays a stored game (?dir=<data dir>&id=<game id>) step by
// step on the board, with its dice and captions.

import {World, TEAMS} from './risk3d.js';
import {decodeRecord, standings} from './record.js';
import {sfx} from './sfx.js';
import {$, updateSeats, showDice, setCaption, banner, hideBanner, soundButtons,
        progress, doneLoading, failLoading, holdings} from './hud.js';

const params = new URLSearchParams(location.search);
const world = new World($('stage'));
window.world = world;  // for the console
let game, views, index = 0, playing = false, speed = 2;

// Seconds a step gets at 1x: dice and conquests are the show; the rest is
// bookkeeping.
function pace(prev, view) {
  if (view.d) return view.c !== undefined ? 1.5 : 1.0;
  if (view.r === 0) return 0.14;
  let moved = 0;
  for (let t = 0; t < 42; ++t) if (prev.u[t] !== view.u[t] || prev.o[t] !== view.o[t]) ++moved;
  if (!moved) return view.a ? 0.18 : 0.08;
  return moved === 2 && view.a ? 0.9 : 0.6;
}

function hud() {
  const view = views[index];
  updateSeats(view, game.players);
  setCaption(index ? game.steps[index - 1].caption : 'The board before the first move');
  $('scrub').value = index;
  $('stepno').textContent = `${index} / ${views.length - 1}`;
  $('skip').style.display = view.r === 0 && index < views.length - 1 ? '' : 'none';
  drawSpark();
  if (index === views.length - 1) {
    const text = view.w || game.reason || 'The record ends here';
    const winner = game.result === 2 ? game.players[game.winner] : '';
    const forfeits = game.forfeits.map(
        (f) => `${game.players[f.seat]} forfeited (${f.reason}) at move ${f.move}`);
    banner(winner ? `${winner} wins` : 'Game over',
           [text, standings(game.players, game.places), ...forfeits].filter(Boolean).join('\n'));
  } else {
    hideBanner();
  }
}

function jump(i) {
  index = Math.max(0, Math.min(views.length - 1, i));
  world.setView(views[index]);
  hud();
}

async function step(direction = 1) {
  if (direction < 0) return jump(index - 1);
  if (index >= views.length - 1) return;
  const prev = views[index];
  const view = views[++index];
  hud();
  if (view.d && view.a) sfx.play('throw', {volume: speed > 4 ? 0.4 : 1});
  await world.play(prev, view, {speed: Math.max(1, speed)});
}

async function run() {
  while (playing && index < views.length - 1) {
    const prev = views[index], view = views[index + 1];
    const started = performance.now();
    step();
    const ms = (pace(prev, view) / speed) * 1000;
    await new Promise((resolve) => setTimeout(resolve, Math.max(16, ms - (performance.now() - started))));
  }
  setPlaying(false);
}

function setPlaying(on) {
  playing = on && index < views.length - 1;
  $('play').textContent = playing ? '⏸' : '▶';
  sfx.loop('drums', playing);
  if (playing) run();
}

function drawSpark() {
  const canvas = $('spark');
  const w = canvas.clientWidth, h = canvas.clientHeight;
  if (!w) return;
  if (canvas.width !== w * devicePixelRatio) {
    canvas.width = w * devicePixelRatio;
    canvas.height = h * devicePixelRatio;
  }
  const g = canvas.getContext('2d');
  g.setTransform(devicePixelRatio, 0, 0, devicePixelRatio, 0, 0);
  g.clearRect(0, 0, w, h);
  const n = views.length - 1 || 1;
  const x = (i) => (i / n) * w;
  const y = (v) => h - 3 - (v / 42) * (h - 6);
  // Round boundaries, faint.
  g.fillStyle = 'rgba(214,180,106,0.18)';
  for (const i of roundStarts) g.fillRect(x(i), 0, 1, h);
  for (const seat of spark[0].keys()) {
    g.beginPath();
    spark.forEach((v, i) => (i ? g.lineTo(x(i), y(v[seat])) : g.moveTo(x(i), y(v[seat]))));
    g.strokeStyle = TEAMS[seat].css;
    g.lineWidth = 1.6;
    g.stroke();
  }
  g.fillStyle = '#f3d995';
  g.fillRect(x(index) - 1, 0, 2, h);
}

let spark = [], roundStarts = [];

async function main() {
  const dir = params.get('dir'), id = params.get('id');
  if (!dir || !id) throw new Error('no game: open one from the lobby');
  const record = fetch(`api/game/${encodeURIComponent(dir)}/${encodeURIComponent(id)}`)
      .then((r) => {
        if (!r.ok) throw new Error(`game ${id}: ${r.status}`);
        return r.arrayBuffer();
      });
  await world.load((f) => progress(f * 0.9));
  game = decodeRecord(await record);
  progress(1);
  views = [game.initialView, ...game.steps.map((s) => s.view)];
  spark = views.map((v) => holdings(v).territories);
  views.forEach((v, i) => {
    if (i && v.r !== views[i - 1].r) roundStarts.push(i);
  });
  document.title = `${game.players.join(' vs ')} · ${game.game}`;
  world.onDice = (dice, lostA, lostD, attacker, defender) =>
    showDice(dice, attacker, defender, {rolling: speed <= 4});
  $('scrub').max = views.length - 1;
  soundButtons(world);
  sfx.preload();
  jump(0);
  doneLoading();

  $('play').onclick = () => setPlaying(!playing);
  $('next').onclick = () => { setPlaying(false); step(1); };
  $('prev').onclick = () => { setPlaying(false); step(-1); };
  $('first').onclick = () => { setPlaying(false); jump(0); };
  $('last').onclick = () => { setPlaying(false); jump(views.length - 1); };
  $('skip').onclick = () => jump(views.findIndex((v) => v.r > 0));
  $('scrub').oninput = (e) => { setPlaying(false); jump(Number(e.target.value)); };
  $('speed').onchange = (e) => { speed = Number(e.target.value); };
  addEventListener('keydown', (e) => {
    if (e.target.tagName === 'SELECT') return;
    if (e.key === ' ') { e.preventDefault(); setPlaying(!playing); }
    else if (e.key === 'ArrowRight') { setPlaying(false); step(1); }
    else if (e.key === 'ArrowLeft') { setPlaying(false); step(-1); }
    else if (e.key === 'Home') jump(0);
    else if (e.key === 'End') jump(views.length - 1);
    else if (e.key === 'PageDown' || e.key === 'ArrowDown') {
      const next = roundStarts.find((i) => i > index);
      if (next) jump(next);
    } else if (e.key === 'PageUp' || e.key === 'ArrowUp') {
      const before = roundStarts.filter((i) => i < index);
      jump(before.length ? before[before.length - 1] : 0);
    } else if (e.key === '+' || e.key === '=') {
      const s = $('speed');
      s.selectedIndex = Math.min(s.options.length - 1, s.selectedIndex + 1);
      s.onchange({target: s});
    } else if (e.key === '-') {
      const s = $('speed');
      s.selectedIndex = Math.max(0, s.selectedIndex - 1);
      s.onchange({target: s});
    }
  });
  addEventListener('resize', drawSpark);
  if (params.get('autoplay')) {
    speed = Number(params.get('autoplay'));
    jump(Math.max(0, views.findIndex((v) => v.r > 0)));
    setPlaying(true);
  }
  document.body.dataset.ready = '1';
}

main().catch(failLoading);
