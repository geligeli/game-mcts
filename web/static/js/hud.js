// HUD pieces the viewer and the play page share: seats, dice, captions,
// banners, the sound buttons.

import {TEAMS} from './risk3d.js';
import {sfx} from './sfx.js';

export const $ = (id) => document.getElementById(id);

// Territories and armies by seat; a view has reserves for every seat.
export function holdings(view) {
  const territories = view.rv.map(() => 0), armies = view.rv.map(() => 0);
  for (let t = 0; t < 42; ++t) {
    const owner = view.o[t];
    if (owner !== '-') {
      ++territories[owner];
      armies[owner] += view.u[t];
    }
  }
  return {territories, armies};
}

export function updateSeats(view, names) {
  const {territories, armies} = holdings(view);
  // The page has a panel for every seat of the season; an older game may use
  // fewer.
  for (let seat = 0, panel; (panel = $(`seat${seat}`)); ++seat) {
    panel.hidden = seat >= view.rv.length;
    if (panel.hidden) continue;
    $(`name${seat}`).textContent = names[seat];
    $(`name${seat}`).title = `${names[seat]} (${TEAMS[seat].name})`;
    // Reserves are the mover's, to place before its first attack.
    const reserves = view.p === seat && view.rv[seat] ? ` · +${view.rv[seat]} to place` : '';
    $(`stats${seat}`).textContent =
        `${territories[seat]} territories · ${armies[seat]} armies${reserves}`;
    panel.classList.toggle('active', view.p === seat);
  }
  const round = $('round');
  round.textContent = '';
  // r counts finished rounds. Setup is before the first: land unclaimed, or
  // every side still holding its setup armies.
  const setup = view.r === 0 && (view.o.includes('-') || view.rv.every((n) => n > 0));
  if (setup) {
    round.append('Setup');
  } else {
    round.append(`Round ${view.m ? Math.min(view.r + 1, view.m) : view.r + 1}`);
    if (view.m) {
      const small = document.createElement('small');
      small.textContent = `of ${view.m}`;
      round.append(small);
    }
  }
}

const PIPS = {1: [4], 2: [0, 8], 3: [0, 4, 8], 4: [0, 2, 6, 8], 5: [0, 2, 4, 6, 8], 6: [0, 2, 3, 5, 6, 8]};

function die(face, seat, lost) {
  const node = document.createElement('div');
  node.className = `die ${seat}${lost ? ' lost' : ''}`;
  for (let i = 0; i < 9; ++i) {
    const pip = document.createElement('i');
    if (PIPS[face].includes(i)) pip.style.visibility = 'visible';
    node.append(pip);
  }
  return node;
}

// Shows a roll: the attacker's dice on the left, which dice lost dimmed.
let diceTimer = 0;
export function showDice([attack, defend], attacker, defender, {rolling = true} = {}) {
  const bar = $('dice');
  bar.textContent = '';
  const lostA = new Set(), lostD = new Set();
  for (let i = 0; i < Math.min(attack.length, defend.length); ++i) {
    if (attack[i] > defend[i]) lostD.add(i); else lostA.add(i);
  }
  const side = (faces, seat, lost) => {
    const node = document.createElement('div');
    node.className = 'side';
    faces.forEach((f, i) => node.append(die(f, `p${seat}`, lost.has(i))));
    return node;
  };
  const vs = document.createElement('span');
  vs.className = 'vs';
  vs.textContent = 'vs';
  bar.append(side(attack, attacker, lostA), vs, side(defend, defender, lostD));
  if (rolling) {
    bar.querySelectorAll('.die').forEach((d) => d.classList.add('rolling'));
    setTimeout(() => bar.querySelectorAll('.die').forEach((d) => d.classList.remove('rolling')), 180);
  }
  bar.classList.remove('hidden');
  clearTimeout(diceTimer);
  diceTimer = setTimeout(() => bar.classList.add('hidden'), 2600);
}

// Captions come from the terminal renderer (risk_render.h): drop its colours.
export const plain = (text) => (text || '').replace(/\x1b\[[0-9;]*m/g, '');

export function setCaption(text) {
  $('caption').textContent = plain(text);
}

let bannerTimer = 0;
export function banner(title, detail = '', ms = 0) {
  const node = $('banner');
  node.textContent = title;
  if (detail) {
    const small = document.createElement('small');
    small.textContent = detail;
    node.append(small);
  }
  node.classList.remove('hidden');
  clearTimeout(bannerTimer);
  if (ms) bannerTimer = setTimeout(() => node.classList.add('hidden'), ms);
}

export function hideBanner() {
  $('banner').classList.add('hidden');
}

export function soundButtons(world) {
  const sound = $('sound'), music = $('music');
  const paint = () => {
    sound.textContent = sfx.muted ? '🔇' : '🔊';
    music.style.opacity = sfx.music ? 1 : 0.45;
  };
  sound.onclick = () => { sfx.setMuted(!sfx.muted); paint(); };
  music.onclick = () => { sfx.setMusic(!sfx.music); paint(); };
  $('camera').onclick = () => world.resetCamera();
  addEventListener('keydown', (e) => {
    if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT') return;
    if (e.key === 'm' || e.key === 'M') sound.onclick();
    if (e.key === 'c' || e.key === 'C') world.resetCamera();
  });
  paint();
}

export function progress(fraction, text) {
  $('loadbar').style.width = `${Math.round(fraction * 100)}%`;
  if (text) $('loadtext').textContent = text;
}

export function doneLoading() {
  $('loading').classList.add('hidden');
}

export function failLoading(error) {
  $('loadtext').textContent = String(error.message || error);
  $('loadtext').style.color = '#ff9b8e';
  document.title = `FAIL ${error.message || error}`;
  console.error(error);
}
