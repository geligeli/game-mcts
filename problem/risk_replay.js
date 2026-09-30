// Risk's replay module (arena_problem's replay_module), for two seats or
// three: draws each step's view -- the JSON snapshot problem/risk_view.h
// writes -- on the map in risk_map.svg, and under it each seat's territories,
// armies and reinforcements over the whole game. The arena's replay page calls init() once, with every
// view, and render() per frame. Names, captions and views are data:
// textContent, never innerHTML.

const SVG = 'http://www.w3.org/2000/svg';
// Red, green and gold, as web/'s 3D board has the three seats.
const OWNER = ['#c33', '#3a3', '#c90'];
const OWNER_LIT = ['#f77', '#7d7', '#fc4'];
const UNOWNED = '#999';
const DIE = ['', '⚀', '⚁', '⚂', '⚃', '⚄', '⚅'];
const WIDTH = 900;
// The charts' margins, in viewBox units (WIDTH wide).
const CHART = {left: 34, right: 8, top: 6, bottom: 18};

const STYLE = `
.risk{max-width:${WIDTH}px;font-family:sans-serif}
.risk svg{width:100%;height:auto;background:#36c;display:block}
.risk .territory{stroke:#222;stroke-width:.6;stroke-linejoin:round}
.risk .territory.hit{stroke:#fff;stroke-width:2}
.risk .territory.won{stroke:#fff;stroke-width:3.5}
.risk .sea{stroke:#fff;stroke-width:1;stroke-dasharray:3 2;opacity:.7}
.risk .army{font:bold 11px sans-serif;text-anchor:middle;dominant-baseline:central;
  fill:#fff;stroke:#000;stroke-width:2.5px;paint-order:stroke;pointer-events:none}
.risk .arrow{stroke:#fff;stroke-width:3;marker-end:url(#risk-head)}
.risk .bar{display:flex;gap:1.5em;align-items:center;margin:.3em 0;min-height:1.6em}
.risk .seat{font-weight:bold}
.risk .dice{font-size:1.6em;letter-spacing:.05em}
.risk .dice .lost{opacity:.35}
.risk .result{font-weight:bold;font-size:1.2em;color:#b00}
.risk .chart{background:none;margin-top:.2em}
.risk .chart text{font:11px sans-serif;fill:#666}
.risk .chart .grid{stroke:#ddd;stroke-width:1}
.risk .chart .setup{fill:#f1f1f1}
.risk .chart .line{fill:none;stroke-width:2;stroke-linejoin:round}
.risk .chart .cursor{stroke:#333;stroke-width:1}
.risk .chart circle{stroke:#fff;stroke-width:1.5}`;

const state = {};

function svg(name, attributes = {}) {
  const node = document.createElementNS(SVG, name);
  for (const [key, value] of Object.entries(attributes)) {
    node.setAttribute(key, value);
  }
  return node;
}

function span(text, className, color) {
  const node = document.createElement('span');
  node.textContent = text;
  if (className) node.className = className;
  if (color) node.style.color = color;
  return node;
}

export async function init(stage, game, views) {
  const response = await fetch(new URL('risk_map.svg', import.meta.url));
  const parsed = new DOMParser().parseFromString(
      await response.text(), 'image/svg+xml');
  const map = document.importNode(parsed.documentElement, true);

  const defs = svg('defs');
  const head = svg('marker', {id: 'risk-head', viewBox: '0 0 10 10', refX: 8,
                              refY: 5, markerWidth: 4, markerHeight: 4,
                              orient: 'auto-start-reverse'});
  head.append(svg('path', {d: 'M0 0L10 5L0 10z', fill: '#fff'}));
  defs.append(head);
  map.prepend(defs);

  state.paths = [];
  state.labels = [];
  const labels = svg('g');
  for (let t = 0; t < 42; ++t) {
    const path = map.querySelector(`#t${t}`);
    const label = svg('text', {class: 'army', x: path.dataset.x,
                               y: path.dataset.y});
    labels.append(label);
    state.paths.push(path);
    state.labels.push(label);
  }
  state.arrows = svg('g');
  map.append(state.arrows, labels);

  const root = document.createElement('div');
  root.className = 'risk';
  const style = document.createElement('style');
  style.textContent = STYLE;
  state.top = document.createElement('div');
  state.top.className = 'bar';
  state.bottom = document.createElement('div');
  state.bottom.className = 'bar';
  root.append(style, state.top, map, state.bottom);
  state.players = game.players;
  state.seats = game.players.map((_, seat) => seat);
  // An older arena hands init no views: no chart then.
  if (views) {
    state.charts = charts(views.map(decode));
    root.append(...state.charts.flatMap(chart => chart.nodes));
  }
  stage.replaceChildren(root);
}

// Two ends of an arrow, or two stubs off the map's edges when the shortest
// way between them is across it (Alaska and Kamchatka).
function drawArrow(from, to) {
  const [x1, y1] = [+from.dataset.x, +from.dataset.y];
  const [x2, y2] = [+to.dataset.x, +to.dataset.y];
  const segments = Math.abs(x2 - x1) <= WIDTH / 2
      ? [[x1, y1, x2, y2]]
      : x1 < x2 ? [[x1, y1, 0, y1], [WIDTH, y2, x2, y2]]
                : [[x1, y1, WIDTH, y1], [0, y2, x2, y2]];
  for (const [ax, ay, bx, by] of segments) {
    const length = Math.hypot(bx - ax, by - ay) || 1;
    const [ux, uy] = [(bx - ax) / length, (by - ay) / length];
    // Clear of the army counts at a territory's end, not at the map's edge.
    const start = ax === x1 && ay === y1 ? 10 : 0;
    const end = bx === x2 && by === y2 ? 12 : 0;
    state.arrows.append(svg('line', {
      class: 'arrow', x1: ax + ux * start, y1: ay + uy * start,
      x2: bx - ux * end, y2: by - uy * end,
    }));
  }
}

function decode(bytes) {
  return JSON.parse(new TextDecoder().decode(bytes));
}

// Territories and armies per seat.
function counts(view) {
  const territories = state.seats.map(() => 0);
  const armies = state.seats.map(() => 0);
  for (let t = 0; t < 42; ++t) {
    const owner = view.o[t];
    if (owner !== '-') {
      ++territories[+owner];
      armies[+owner] += view.u[t];
    }
  }
  return {territories, armies};
}

// 1, 2 or 5 times a power of ten, at least |value|.
function niceMax(value) {
  const power = 10 ** Math.floor(Math.log10(Math.max(value, 1)));
  return [1, 2, 5, 10].map(k => k * power).find(k => k >= value);
}

// A chart over every view: a heading with each seat's value at the view shown,
// the claim phase (round 0) shaded, gridlines, round ticks and a cursor that
// show() moves. |marks| draws the data, |valueAt(i, seat)| is the readout and,
// with |dots|, where the cursor marks each seat.
function timeChart(decoded, {title, height, max, marks, valueAt, dots}) {
  const {left, right, top, bottom} = CHART;
  const last = Math.max(decoded.length - 1, 1);
  const x = i => left + i / last * (WIDTH - left - right);
  const y = k => top + (max - k) / max * (height - top - bottom);

  const heading = document.createElement('div');
  heading.className = 'bar';
  const readouts = state.seats.map(seat => span('', 'seat', OWNER[seat]));
  heading.append(span(`${title}:`), ...readouts);

  const chart = svg('svg', {class: 'chart', viewBox: `0 0 ${WIDTH} ${height}`,
                            role: 'img', 'aria-label': title});
  const setup = decoded.findIndex(view => view.r > 0);
  if (setup > 0) {
    chart.append(svg('rect', {class: 'setup', x: x(0), y: top,
                              width: x(setup) - x(0),
                              height: height - top - bottom}));
  }
  for (const k of [0, max / 2, max]) {
    chart.append(svg('line', {class: 'grid', x1: left, x2: WIDTH - right,
                              y1: y(k), y2: y(k)}));
    const label = svg('text', {x: left - 5, y: y(k) + 4, 'text-anchor': 'end'});
    label.textContent = k;
    chart.append(label);
  }
  // A tick where round 1 and every fifth round begin.
  decoded.forEach((view, i) => {
    if (i === 0 || view.r === decoded[i - 1].r) return;
    if (view.r !== 1 && view.r % 5 !== 0) return;
    chart.append(svg('line', {class: 'grid', x1: x(i), x2: x(i),
                              y1: height - bottom, y2: height - bottom + 4}));
    // Kept inside the chart at its right edge.
    const anchor = x(i) > WIDTH - right - 20 ? 'end' : 'middle';
    const label = svg('text', {x: x(i), y: height - 3, 'text-anchor': anchor});
    label.textContent = `R${view.r}`;
    chart.append(label);
  });
  marks(chart, x, y);

  const cursor = svg('line', {class: 'cursor', y1: top, y2: height - bottom});
  const markers = dots ? state.seats.map(seat => svg('circle', {
                           r: 4, fill: OWNER[seat]}))
                       : [];
  chart.append(cursor, ...markers);
  return {
    nodes: [heading, chart],
    show(i) {
      cursor.setAttribute('x1', x(i));
      cursor.setAttribute('x2', x(i));
      markers.forEach((marker, seat) => {
        marker.setAttribute('cx', x(i));
        marker.setAttribute('cy', y(valueAt(i, seat)));
      });
      readouts.forEach((readout, seat) => {
        readout.textContent =
            `P${seat} ${state.players[seat] ?? ''} ${valueAt(i, seat)}`;
      });
    },
  };
}

// Territories, armies (on the board and still to place) and the reinforcements
// each seat receives as its turn begins: its reserves jump from 0.
function charts(decoded) {
  const data = decoded.map((view, i) => {
    const {territories, armies} = counts(view);
    const before = decoded[i - 1];
    return {
      territories,
      armies: armies.map((count, seat) => count + view.rv[seat]),
      gained: state.seats.map(seat => view.r > 0 && before &&
                                          view.rv[seat] > before.rv[seat]
                                      ? view.rv[seat] - before.rv[seat]
                                      : 0),
    };
  });
  // Each seat's latest reinforcement as of each view, for the readout.
  let latest = state.seats.map(() => 0);
  const lastGained = data.map(d => {
    latest = latest.map((gain, seat) => d.gained[seat] || gain);
    return latest;
  });
  const lines = pick => (chart, x, y) => {
    for (const seat of state.seats) {
      chart.append(svg('polyline', {
        class: 'line', stroke: OWNER[seat],
        points: data.map((d, i) => `${x(i).toFixed(1)},${y(pick(d)[seat])}`)
                    .join(' '),
      }));
    }
  };
  const bars = (chart, x, y) => {
    data.forEach((d, i) => d.gained.forEach((gain, seat) => {
      if (!gain) return;
      chart.append(svg('rect', {x: x(i) - 1.5, y: y(gain), width: 3,
                                height: y(0) - y(gain), fill: OWNER[seat]}));
    }));
  };
  return [
    timeChart(decoded, {
      title: 'Territories held', height: 110, max: 42,
      marks: lines(d => d.territories), dots: true,
      valueAt: (i, seat) => data[i].territories[seat],
    }),
    timeChart(decoded, {
      title: 'Armies, on the board and to place', height: 110,
      max: niceMax(Math.max(...data.flatMap(d => d.armies))),
      marks: lines(d => d.armies), dots: true,
      valueAt: (i, seat) => data[i].armies[seat],
    }),
    timeChart(decoded, {
      title: 'Reinforcements as each turn begins', height: 90,
      max: niceMax(Math.max(1, ...data.flatMap(d => d.gained))),
      marks: bars, dots: false,
      valueAt: (i, seat) => `+${lastGained[i][seat]}`,
    }),
  ];
}

function seatLine(view) {
  const {territories, armies} = counts(view);
  const nodes = [span(`Round ${view.r}${view.m ? '/' + view.m : ''}`)];
  for (const seat of state.seats) {
    const name = state.players[seat] ?? `P${seat}`;
    const mover = view.p === seat ? '▶ ' : '';
    nodes.push(span(`${mover}P${seat} ${name}`, 'seat', OWNER[seat]),
               span(`${territories[seat]} territories, ${armies[seat]} armies` +
                    (view.rv[seat] ? `, ${view.rv[seat]} to place` : '')));
  }
  return nodes;
}

// Highest against highest, a tie to the defender: the loser's die is faded.
function diceLine(view) {
  if (!view.d) return [];
  const [attacker, defender] = view.d;
  // The source is still the attacker's; the target may be theirs by now, so
  // the defender is the view's own (a view from before df had two seats).
  const seat = view.a ? +view.o[view.a[0]] : 0;
  const defending = view.df ?? 1 - seat;
  const node = span('', 'dice');
  const pairs = Math.min(attacker.length, defender.length);
  attacker.forEach((face, i) => node.append(span(
      DIE[face], i < pairs && face <= defender[i] ? 'lost' : '', OWNER[seat])));
  node.append(span(' vs '));
  defender.forEach((face, i) => node.append(span(
      DIE[face], i < pairs && attacker[i] > face ? 'lost' : '',
      OWNER[defending])));
  const nodes = [node];
  if (view.c !== undefined) nodes.push(span('Conquered!', 'result'));
  return nodes;
}

export function render(stage, view, step) {
  const v = decode(view);
  const touched = new Set(v.h ?? []);
  for (let t = 0; t < 42; ++t) {
    const owner = v.o[t];
    const path = state.paths[t];
    path.style.fill = owner === '-' ? UNOWNED
        : (touched.has(t) ? OWNER_LIT : OWNER)[+owner];
    path.classList.toggle('hit', touched.has(t));
    path.classList.toggle('won', v.c === t);
    state.labels[t].textContent = owner === '-' ? '' : v.u[t];
  }
  // Touched territories paint over their neighbours' borders.
  for (const t of touched) state.paths[t].parentNode.append(state.paths[t]);
  state.arrows.replaceChildren();
  if (v.a) drawArrow(state.paths[v.a[0]], state.paths[v.a[1]]);
  state.top.replaceChildren(...seatLine(v));
  state.bottom.replaceChildren(
      ...diceLine(v), ...(v.w ? [span(v.w, 'result')] : []));
  if (state.charts && step.view !== undefined) {
    for (const chart of state.charts) chart.show(step.view);
  }
}
