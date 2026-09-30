// The Risk board in 3D, shared by the viewer (replay.js) and the play page
// (play.js).
//
// The 42 territories are extruded from problem/risk_map.svg, the same map the
// 2D replay draws, on a low-poly sea. Each holds a garrison: 1-4 animated
// figures by size (Knights for seat 0, Skeletons for seat 1, Barbarians for
// seat 2), a count badge, the owner's flag, and a tower or a castle once it
// grows big.
//
//   const world = new World(container);
//   await world.load(progress);
//   world.setView(view);                      // jump
//   await world.play(prevView, view, {speed}); // animate one step
//
// Views are problem/risk_view.h's JSON. play() works from what changed and
// the step's marks (a: arrow, d: dice, c: conquered), so a replay and a live
// game animate the same way. Every animation runs on world time, and
// fastForward() finishes them all at once: a new step never waits.

import * as THREE from 'three';
import {GLTFLoader} from 'three/addons/loaders/GLTFLoader.js';
import {SVGLoader} from 'three/addons/loaders/SVGLoader.js';
import {OrbitControls} from 'three/addons/controls/OrbitControls.js';
import * as SkeletonUtils from 'three/addons/utils/SkeletonUtils.js';
import {EffectComposer} from 'three/addons/postprocessing/EffectComposer.js';
import {RenderPass} from 'three/addons/postprocessing/RenderPass.js';
import {UnrealBloomPass} from 'three/addons/postprocessing/UnrealBloomPass.js';
import {OutputPass} from 'three/addons/postprocessing/OutputPass.js';
import {Fx} from './fx.js';
import {sfx} from './sfx.js';

const ASSETS = new URL('../assets/', import.meta.url);
const SCALE = 0.1;       // world units per SVG unit (the map is 900 x 600)
const LAND = 0.8;        // plate height
const BASE = 0.45;       // the continent slab under the plates
const FIGURE = 0.7;      // character scale

export const TEAMS = [
  {name: 'Crimson Knights', color: 0xe0513f, css: '#e0513f', land: 0xb24a3b,
   flag: 'flag_red', tower: 'building_tower_A_red', castle: 'building_castle_red', model: 'knight'},
  {name: 'Bone Legion', color: 0x3fb56a, css: '#3fb56a', land: 0x3d8f59,
   flag: 'flag_green', tower: 'building_tower_A_green', castle: 'building_castle_green', model: 'skeleton'},
  {name: 'Golden Horde', color: 0xe0b43f, css: '#e0b43f', land: 0xb08a34,
   flag: 'flag_yellow', tower: 'building_tower_A_yellow', castle: 'building_castle_yellow', model: 'barbarian'},
];
const NEUTRAL = 0x9c9580;

// Figures shown for an army: 1, 2-4, 5-9, 10+.
const figuresFor = (units) => (units <= 0 ? 0 : units <= 1 ? 1 : units <= 4 ? 2 : units <= 9 ? 3 : 4);
const FORMATION = [[0, 0.2], [-0.95, -0.4], [0.95, -0.4], [0, -1.05]];

const ease = {
  inOut: (k) => (k < 0.5 ? 2 * k * k : 1 - (-2 * k + 2) ** 2 / 2),
  out: (k) => 1 - (1 - k) ** 3,
  in: (k) => k * k * k,
  linear: (k) => k,
};

// Everything that moves over time: finishes early on demand.
class Tweens {
  constructor() {
    this.live = new Set();
    this.speed = 1;
  }

  add(seconds, update = () => {}, curve = ease.inOut) {
    return new Promise((resolve) => {
      const tween = {t: 0, seconds: Math.max(seconds, 1e-3), update, curve, resolve};
      update(0);
      this.live.add(tween);
    });
  }

  wait(seconds) {
    return this.add(seconds);
  }

  step(dt) {
    for (const tween of [...this.live]) {
      tween.t += dt * this.speed;
      const k = Math.min(1, tween.t / tween.seconds);
      tween.update(tween.curve(k));
      if (k >= 1) {
        this.live.delete(tween);
        tween.resolve();
      }
    }
  }

  finishAll() {
    // Finishing one can start another (a chain of awaits): drain.
    for (let guard = 0; guard < 50 && this.live.size; ++guard) {
      for (const tween of [...this.live]) {
        this.live.delete(tween);
        tween.update(1);
        tween.resolve();
      }
    }
  }
}

function seeded(seed) {
  let s = seed * 9301 + 49297;
  return () => {
    s = (s * 9301 + 49297) % 233280;
    return s / 233280;
  };
}

function toWorld(x, y, height = 0) {
  return new THREE.Vector3((x - 450) * SCALE, height, (y - 300) * SCALE);
}

function insidePolygon(point, polygon) {
  let inside = false;
  for (let i = 0, j = polygon.length - 1; i < polygon.length; j = i++) {
    const a = polygon[i], b = polygon[j];
    if ((a.y > point.y) !== (b.y > point.y) &&
        point.x < ((b.x - a.x) * (point.y - a.y)) / (b.y - a.y) + a.x) {
      inside = !inside;
    }
  }
  return inside;
}

// One character, with its own mixer.
class Figure {
  constructor(template, team) {
    this.team = team;
    this.root = SkeletonUtils.clone(template.scene);
    this.root.scale.setScalar(FIGURE);
    this.root.traverse((node) => {
      if (node.isMesh) node.frustumCulled = false;
    });
    if (template.attach) template.attach(this.root);
    this.mixer = new THREE.AnimationMixer(this.root);
    this.clips = template.clips;
    this.current = null;
    this.play('idle', {offset: Math.random()});
  }

  play(name, {loop = true, fade = 0.15, offset = 0, speed = 1, then = 'idle'} = {}) {
    const clip = this.clips[name];
    if (!clip) return 0;
    const action = this.mixer.clipAction(clip);
    action.reset();
    action.setLoop(loop ? THREE.LoopRepeat : THREE.LoopOnce, loop ? Infinity : 1);
    action.clampWhenFinished = !loop;
    action.timeScale = speed;
    if (offset) action.time = offset * clip.duration;
    if (this.current && this.current !== action) {
      action.crossFadeFrom(this.current, fade, false);
    }
    action.play();
    this.current = action;
    if (!loop && then) {
      clearTimeout(this.returnTimer);
      const onFinish = (event) => {
        if (event.action !== action) return;
        this.mixer.removeEventListener('finished', onFinish);
        if (this.current === action && !this.dead) this.play(then);
      };
      this.mixer.addEventListener('finished', onFinish);
    }
    return clip.duration / speed;
  }
}

// A territory's army: figures, badge, flag and building.
class Garrison {
  constructor(world, t, anchor) {
    this.world = world;
    this.t = t;
    this.anchor = anchor;
    this.group = new THREE.Group();
    this.group.position.copy(anchor);
    world.scene.add(this.group);
    this.figures = [];
    this.owner = -1;
    this.units = 0;
    this.pending = 0;
    this.flag = null;
    this.building = null;
    this.buildingKind = '';

    const ringMaterial = new THREE.MeshBasicMaterial({
      map: world.ringTexture, transparent: true, depthWrite: false, opacity: 0,
    });
    this.ring = new THREE.Mesh(new THREE.PlaneGeometry(4, 4), ringMaterial);
    this.ring.rotation.x = -Math.PI / 2;
    this.ring.position.y = 0.03;
    this.group.add(this.ring);

    this.badgeCanvas = document.createElement('canvas');
    this.badgeCanvas.width = 192;
    this.badgeCanvas.height = 96;
    this.badgeTexture = new THREE.CanvasTexture(this.badgeCanvas);
    this.badgeTexture.colorSpace = THREE.SRGBColorSpace;
    this.badge = new THREE.Sprite(new THREE.SpriteMaterial({
      map: this.badgeTexture, depthTest: false, transparent: true,
    }));
    this.badge.renderOrder = 10;
    this.badge.scale.set(2.8, 1.4, 1);
    this.badge.position.set(0, 2.9, 0);
    this.group.add(this.badge);
    this.drawBadge();
  }

  drawBadge() {
    const g = this.badgeCanvas.getContext('2d');
    g.clearRect(0, 0, 192, 96);
    if (this.owner < 0 && !this.units) {
      this.badgeTexture.needsUpdate = true;
      return;
    }
    const color = this.owner >= 0 ? TEAMS[this.owner].css : '#777';
    const label = String(this.units);
    const extra = this.pending ? `+${this.pending}` : '';
    g.font = 'bold 50px Palatino, Georgia, serif';
    const w = Math.max(64, g.measureText(label).width + (extra ? g.measureText(extra).width + 14 : 0) + 34);
    const x = (192 - w) / 2;
    g.fillStyle = 'rgba(10,12,16,0.82)';
    g.beginPath();
    g.roundRect(x, 16, w, 64, 32);
    g.fill();
    g.lineWidth = 6;
    g.strokeStyle = color;
    g.stroke();
    g.textBaseline = 'middle';
    g.textAlign = 'left';
    g.fillStyle = '#fff';
    const labelWidth = g.measureText(label).width;
    const start = 96 - (labelWidth + (extra ? g.measureText(extra).width + 14 : 0)) / 2;
    g.fillText(label, start, 50);
    if (extra) {
      g.fillStyle = '#f3d995';
      g.fillText(extra, start + labelWidth + 14, 50);
    }
    this.badgeTexture.needsUpdate = true;
  }

  setPending(n) {
    if (n !== this.pending) {
      this.pending = n;
      this.drawBadge();
    }
  }

  slot(i) {
    const [x, z] = FORMATION[i];
    return new THREE.Vector3(x, 0, z);
  }

  // Everyone faces |point| (world space).
  face(point) {
    for (const f of this.figures) {
      const p = new THREE.Vector3();
      f.root.getWorldPosition(p);
      f.root.lookAt(point.x, p.y, point.z);
    }
  }

  act(name, options) {
    this.figures.forEach((f, i) => {
      setTimeout(() => !f.dead && f.play(name, {loop: false, ...options}), i * 40);
    });
  }

  addFigure(owner, animate, from) {
    const world = this.world;
    const figure = new Figure(world.templates[TEAMS[owner].model], owner);
    const target = this.slot(this.figures.length);
    this.figures.push(figure);
    this.group.add(figure.root);
    figure.root.position.copy(target);
    figure.root.rotation.y = (Math.random() - 0.5) * 0.8;
    if (!animate) return;
    const tweens = world.tweens;
    if (from) {
      // Marches in from another territory, then cheers.
      const start = this.group.worldToLocal(from.clone());
      figure.root.position.copy(start);
      figure.root.lookAt(this.group.localToWorld(target.clone()));
      figure.play('run', {speed: 1.3});
      const arc = start.distanceTo(target) * 0.12;
      tweens.add(0.55, (k) => {
        figure.root.position.lerpVectors(start, target, k);
        figure.root.position.y = Math.sin(Math.PI * k) * arc;
      }, ease.linear).then(() => {
        if (figure.dead) return;
        figure.root.rotation.set(0, (Math.random() - 0.5) * 0.8, 0);
        figure.play('cheer', {loop: false});
      });
    } else if (owner === 1) {
      // Skeletons claw their way up.
      figure.root.position.y = -1.3;
      figure.play('spawn', {loop: false});
      world.fx.smoke(this.group.localToWorld(target.clone()), 2, 0.6, 0x6d6255);
      tweens.add(0.45, (k) => { figure.root.position.y = -1.3 * (1 - k); }, ease.out);
    } else {
      // Knights and barbarians drop in.
      figure.root.position.y = 5;
      tweens.add(0.3, (k) => { figure.root.position.y = 5 * (1 - k); }, ease.in).then(() => {
        if (figure.dead) return;
        world.fx.dust(this.group.localToWorld(target.clone()), 0xe6d7b0, 0.7);
        figure.play('land', {loop: false});
        sfx.play('drop', {volume: 0.5});
      });
    }
  }

  removeFigure(animate) {
    const figure = this.figures.pop();
    if (!figure) return;
    figure.dead = true;
    if (!animate) {
      this.group.remove(figure.root);
      return;
    }
    const duration = Math.min(figure.play('death', {loop: false, then: null}), 1.1);
    this.world.dying.add(figure);
    this.world.tweens.wait(duration * 0.9).then(() => this.world.tweens.add(0.35, (k) => {
      figure.root.position.y = -1.2 * k;
    })).then(() => {
      this.group.remove(figure.root);
      this.world.dying.delete(figure);
    });
  }

  setModel(slot, name, scale, position, animate) {
    const old = this[slot];
    if (old) {
      if (animate) {
        this.world.tweens.add(0.3, (k) => old.scale.setScalar(old.userData.scale * (1 - k)))
            .then(() => this.group.remove(old));
      } else {
        this.group.remove(old);
      }
    }
    this[slot] = null;
    if (!name) return;
    const model = this.world.props[name].clone();
    model.userData.scale = scale;
    model.position.copy(position);
    model.scale.setScalar(animate ? 0.001 : scale);
    this.group.add(model);
    this[slot] = model;
    if (animate) {
      this.world.tweens.add(0.4, (k) => model.scale.setScalar(Math.max(0.001, scale * k)), ease.out);
    }
  }

  // Brings the garrison to |owner| and |units|. |from|: where arriving
  // figures come from (a conquest or a fortify), in world space.
  sync(owner, units, {animate = false, from = null} = {}) {
    if (owner !== this.owner) {
      while (this.figures.length) this.removeFigure(animate);
      this.setModel('flag', owner >= 0 ? TEAMS[owner].flag : null, 5,
                    new THREE.Vector3(1.35, 0, 0.7), animate);
      this.buildingKind = '';
      this.setModel('building', null, 1, new THREE.Vector3(), animate);
      this.owner = owner;
      this.ring.material.opacity = owner >= 0 ? 0.55 : 0;
      if (owner >= 0) this.ring.material.color.setHex(TEAMS[owner].color);
    }
    this.units = units;
    const want = owner >= 0 ? figuresFor(units) : 0;
    while (this.figures.length > want) this.removeFigure(animate);
    while (this.figures.length < want) this.addFigure(owner, animate, from);
    const kind = owner < 0 ? '' : units >= 25 ? 'castle' : units >= 12 ? 'tower' : '';
    if (kind !== this.buildingKind) {
      this.buildingKind = kind;
      this.setModel('building', kind ? TEAMS[owner][kind] : null,
                    kind === 'castle' ? 0.5 : 0.65, new THREE.Vector3(-1.55, 0, -1.1), animate);
    }
    this.drawBadge();
  }
}

export class World {
  constructor(container) {
    this.container = container;
    this.tweens = new Tweens();
    this.dying = new Set();
    this.marks = {selected: -1, targets: new Set(), sources: new Set(), hover: -1};
    this.view = null;
    this.generation = 0;
    this.busy = false;
    this.shake = 0;
    this.clock = new THREE.Clock();

    const renderer = new THREE.WebGLRenderer({antialias: true, powerPreference: 'high-performance'});
    renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.05;
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    container.appendChild(renderer.domElement);
    this.renderer = renderer;

    const scene = new THREE.Scene();
    scene.fog = new THREE.Fog(0x0e1a2b, 95, 190);
    this.scene = scene;
    this.fx = new Fx(scene);

    this.camera = new THREE.PerspectiveCamera(38, 1, 0.5, 600);
    this.home = {position: new THREE.Vector3(0, 74, 60), target: new THREE.Vector3(0, 0, 4)};
    this.camera.position.copy(this.home.position);
    const controls = new OrbitControls(this.camera, renderer.domElement);
    controls.target.copy(this.home.target);
    controls.enableDamping = true;
    controls.dampingFactor = 0.08;
    controls.minDistance = 18;
    controls.maxDistance = 130;
    controls.minPolarAngle = 0.15;
    controls.maxPolarAngle = 1.2;
    controls.screenSpacePanning = false;
    controls.mouseButtons = {LEFT: null, MIDDLE: THREE.MOUSE.DOLLY, RIGHT: THREE.MOUSE.ROTATE};
    controls.touches = {ONE: THREE.TOUCH.PAN, TWO: THREE.TOUCH.DOLLY_ROTATE};
    this.controls = controls;
    // Left-drag pans (left-click is for territories); right-drag orbits.
    this.dragPan = null;
    const dom = renderer.domElement;
    dom.addEventListener('pointerdown', (e) => {
      // Touch pans through OrbitControls; the mouse's left button here.
      if (e.button === 0 && e.pointerType === 'mouse') {
        this.dragPan = {x: e.clientX, y: e.clientY, moved: false};
      }
    });
    addEventListener('pointerup', () => {
      this.lastDragMoved = !!(this.dragPan && this.dragPan.moved);
      this.dragPan = null;
    });
    dom.addEventListener('pointermove', (e) => {
      if (!this.dragPan) return;
      const dx = e.clientX - this.dragPan.x, dy = e.clientY - this.dragPan.y;
      if (!this.dragPan.moved && Math.hypot(dx, dy) < 6) return;
      this.dragPan.moved = true;
      this.dragPan.x = e.clientX;
      this.dragPan.y = e.clientY;
      this.pan(dx, dy);
    });
    dom.addEventListener('contextmenu', (e) => e.preventDefault());

    this.composer = new EffectComposer(renderer);
    this.composer.addPass(new RenderPass(scene, this.camera));
    this.bloom = new UnrealBloomPass(new THREE.Vector2(256, 256), 0.32, 0.5, 0.86);
    this.composer.addPass(this.bloom);
    this.composer.addPass(new OutputPass());

    new ResizeObserver(() => this.resize()).observe(container);
    this.resize();
  }

  // Did the last pointer press drag the map rather than click it?
  get dragged() {
    return this.lastDragMoved;
  }

  pan(dx, dy) {
    const distance = this.camera.position.distanceTo(this.controls.target);
    const scale = distance / this.renderer.domElement.clientHeight * 1.2;
    const forward = new THREE.Vector3().subVectors(this.controls.target, this.camera.position);
    forward.y = 0;
    forward.normalize();
    const right = new THREE.Vector3().crossVectors(forward, new THREE.Vector3(0, 1, 0));
    const move = right.multiplyScalar(-dx * scale).add(forward.multiplyScalar(dy * scale));
    const target = this.controls.target.clone().add(move);
    target.x = THREE.MathUtils.clamp(target.x, -48, 48);
    target.z = THREE.MathUtils.clamp(target.z, -32, 34);
    move.subVectors(target, this.controls.target);
    this.controls.target.add(move);
    this.camera.position.add(move);
  }

  resize() {
    const {clientWidth: w, clientHeight: h} = this.container;
    if (!w || !h) return;
    this.renderer.setSize(w, h, false);
    this.composer.setSize(w, h);
    this.camera.aspect = w / h;
    // Narrow screens see the whole map by backing off.
    this.camera.fov = w / h < 1.2 ? 52 : 38;
    this.camera.updateProjectionMatrix();
  }

  async load(progress = () => {}) {
    const gltf = new GLTFLoader();
    const load = (name) => gltf.loadAsync(new URL(`models/${name}`, ASSETS).href);
    const names = [...TEAMS.flatMap((team) => [team.flag, team.tower, team.castle]),
                   'trees_A_medium', 'trees_B_small', 'projectile_catapult'];
    let done = 0;
    const tick = (p) => p.then((v) => { progress(++done / (6 + names.length)); return v; });

    const [svgText, knight, skeleton, barbarian, blade, shield, ...props] = await Promise.all([
      tick(fetch(new URL('risk_map.svg', ASSETS)).then((r) => r.text())),
      tick(load('knight.glb')), tick(load('skeleton.glb')), tick(load('barbarian.glb')),
      tick(load('Skeleton_Blade.gltf')), tick(load('Skeleton_Shield_Large_A.gltf')),
      ...names.map((n) => tick(load(`${n}.gltf`).then((g) => [n, g]))),
    ]);
    this.props = {};
    for (const [name, g] of props) {
      g.scene.traverse((node) => {
        if (node.isMesh) {
          node.castShadow = true;
          node.receiveShadow = true;
        }
      });
      this.props[name] = g.scene;
    }
    this.templates = {knight: this.template(knight), skeleton: this.template(skeleton),
                      barbarian: this.template(barbarian)};
    // Knights and barbarians carry one weapon and a round shield out of the
    // pack's armoury.
    const keep = new Set(['1H_Sword', 'Round_Shield', '1H_Axe', 'Barbarian_Round_Shield']);
    for (const armed of [knight, barbarian]) {
      armed.scene.traverse((node) => {
        if (/(Sword|Shield|Axe|Mug)/.test(node.name) && !keep.has(node.name)) node.visible = false;
      });
    }
    // Skeletons come unarmed: a blade and a shield in their hand slots.
    this.templates.skeleton.attach = (root) => {
      const right = root.getObjectByName('handslot.r');
      const left = root.getObjectByName('handslot.l');
      if (right) right.add(blade.scene.clone());
      if (left) left.add(shield.scene.clone());
    };

    this.ringTexture = this.makeRingTexture();
    this.buildSky();
    this.buildLights();
    this.buildSea();
    this.buildMap(svgText);
    this.lastTime = performance.now();
    this.renderer.setAnimationLoop(() => this.frame());
  }

  template(gltf) {
    const byName = Object.fromEntries(gltf.animations.map((a) => [a.name, a]));
    const pick = (...names) => names.map((n) => byName[n]).find(Boolean);
    gltf.clips = {
      idle: pick('Idle_Combat', 'Idle', '2H_Melee_Idle'),
      attack: pick('1H_Melee_Attack_Chop', '1H_Melee_Attack_Slice_Diagonal'),
      shoot: pick('1H_Ranged_Shoot', 'Throw'),
      hit: pick('Hit_A', 'Hit_B'),
      block: pick('Block_Hit', 'Block'),
      death: pick('Death_A', 'Death_B'),
      run: pick('Running_A', 'Running_B'),
      walk: pick('Walking_A', 'Walking_B'),
      cheer: pick('Cheer'),
      land: pick('Jump_Land'),
      spawn: pick('Spawn_Ground_Skeletons', 'Spawn_Ground', 'Jump_Land'),
    };
    gltf.scene.traverse((node) => {
      if (node.isMesh) node.castShadow = true;
    });
    return gltf;
  }

  makeRingTexture() {
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = 128;
    const g = canvas.getContext('2d');
    const gradient = g.createRadialGradient(64, 64, 20, 64, 64, 62);
    gradient.addColorStop(0, 'rgba(255,255,255,0)');
    gradient.addColorStop(0.72, 'rgba(255,255,255,0.12)');
    gradient.addColorStop(0.86, 'rgba(255,255,255,0.85)');
    gradient.addColorStop(1, 'rgba(255,255,255,0)');
    g.fillStyle = gradient;
    g.fillRect(0, 0, 128, 128);
    const texture = new THREE.CanvasTexture(canvas);
    texture.colorSpace = THREE.SRGBColorSpace;
    return texture;
  }

  buildSky() {
    const canvas = document.createElement('canvas');
    canvas.width = 2;
    canvas.height = 256;
    const g = canvas.getContext('2d');
    const gradient = g.createLinearGradient(0, 0, 0, 256);
    gradient.addColorStop(0, '#0a1220');
    gradient.addColorStop(0.55, '#16304f');
    gradient.addColorStop(1, '#2b4d6e');
    g.fillStyle = gradient;
    g.fillRect(0, 0, 2, 256);
    const texture = new THREE.CanvasTexture(canvas);
    texture.colorSpace = THREE.SRGBColorSpace;
    this.scene.background = texture;
  }

  buildLights() {
    this.scene.add(new THREE.HemisphereLight(0xcfe3ff, 0x2a2016, 1.15));
    const sun = new THREE.DirectionalLight(0xffecd0, 2.6);
    sun.position.set(-38, 60, 30);
    sun.castShadow = true;
    sun.shadow.mapSize.set(2048, 2048);
    Object.assign(sun.shadow.camera, {left: -55, right: 55, top: 40, bottom: -40, near: 10, far: 160});
    sun.shadow.bias = -0.0006;
    sun.shadow.normalBias = 0.03;
    this.scene.add(sun);
    const rim = new THREE.DirectionalLight(0x8fb4ff, 0.5);
    rim.position.set(40, 25, -40);
    this.scene.add(rim);
  }

  buildSea() {
    const geometry = new THREE.PlaneGeometry(260, 200, 130, 100);
    geometry.rotateX(-Math.PI / 2);
    const material = new THREE.MeshStandardMaterial({
      color: 0x1d5a86, roughness: 0.35, metalness: 0.1, flatShading: true,
      transparent: true, opacity: 0.94,
    });
    this.seaTime = {value: 0};
    material.onBeforeCompile = (shader) => {
      shader.uniforms.time = this.seaTime;
      shader.vertexShader = 'uniform float time;\n' + shader.vertexShader.replace(
          '#include <begin_vertex>',
          `#include <begin_vertex>
           float w = sin(position.x * 0.35 + time * 0.9) * 0.16
                   + cos(position.z * 0.42 + time * 1.2) * 0.12
                   + sin((position.x + position.z) * 0.8 + time * 1.7) * 0.05;
           transformed.y += w;`);
    };
    const sea = new THREE.Mesh(geometry, material);
    sea.position.y = 0.05;
    sea.receiveShadow = true;
    this.scene.add(sea);
    // A deeper floor, so the far sea does not end in sky.
    const floor = new THREE.Mesh(new THREE.PlaneGeometry(600, 600),
                                 new THREE.MeshBasicMaterial({color: 0x0b2238}));
    floor.rotation.x = -Math.PI / 2;
    floor.position.y = -1.5;
    this.scene.add(floor);
  }

  buildMap(svgText) {
    const data = new SVGLoader().parse(svgText);
    const doc = new DOMParser().parseFromString(svgText, 'image/svg+xml');
    this.lands = [];
    this.garrisons = [];
    this.anchors = [];
    this.pickables = [];
    const baseMaterial = new THREE.MeshStandardMaterial({color: 0x3b3326, roughness: 0.95});
    const outline = new THREE.LineBasicMaterial({color: 0x1b1712, transparent: true, opacity: 0.55});

    for (const path of data.paths) {
      const node = path.userData.node;
      const match = /^t(\d+)$/.exec(node.getAttribute('id') || '');
      if (!match) continue;
      const t = Number(match[1]);
      const shapes = SVGLoader.createShapes(path);
      const place = (geometry, lift) => {
        geometry.translate(-450, -300, 0);
        geometry.rotateX(Math.PI / 2);
        geometry.scale(SCALE, SCALE, SCALE);
        geometry.translate(0, lift, 0);
        return geometry;
      };
      // The continent slab, then the plate on it, inset so borders show.
      const base = place(new THREE.ExtrudeGeometry(shapes, {
        depth: BASE / SCALE, bevelEnabled: false, curveSegments: 6,
      }).translate(0, 0, -BASE / SCALE), 0);
      const slab = new THREE.Mesh(base, baseMaterial);
      slab.receiveShadow = true;
      this.scene.add(slab);

      const top = place(new THREE.ExtrudeGeometry(shapes, {
        depth: LAND / SCALE, bevelEnabled: true, bevelThickness: 0.6, bevelSize: 1.3,
        bevelOffset: -1.6, bevelSegments: 2, curveSegments: 6,
      }).translate(0, 0, -LAND / SCALE), BASE - 0.25);
      const material = new THREE.MeshStandardMaterial({
        color: NEUTRAL, roughness: 0.82, metalness: 0.02, emissive: 0x000000,
      });
      const plate = new THREE.Mesh(top, material);
      plate.castShadow = true;
      plate.receiveShadow = true;
      plate.userData.t = t;
      this.scene.add(plate);
      this.pickables.push(plate);

      const edges = new THREE.LineSegments(new THREE.EdgesGeometry(top, 35), outline);
      this.scene.add(edges);

      const topY = BASE - 0.25 + LAND + 0.06;
      const anchor = toWorld(Number(node.getAttribute('data-x')),
                             Number(node.getAttribute('data-y')), topY);
      this.anchors[t] = anchor;
      this.lands[t] = {plate, material, color: new THREE.Color(NEUTRAL), name: node.getAttribute('data-name') || `#${t}`};
      this.garrisons[t] = new Garrison(this, t, anchor);
      this.scatter(t, shapes, anchor, topY);
    }
    this.buildSeaLanes(doc);
  }

  // A few trees per territory, away from its army.
  scatter(t, shapes, anchor, y) {
    const random = seeded(t + 7);
    // The pack's hills and mountains stand on a hex tile of their own: trees only.
    const kinds = ['trees_A_medium', 'trees_B_small'];
    const polygon = shapes[0].getPoints(4);
    const box = new THREE.Box2().setFromPoints(polygon);
    let placed = 0;
    for (let attempt = 0; attempt < 40 && placed < 3; ++attempt) {
      const p = new THREE.Vector2(box.min.x + random() * (box.max.x - box.min.x),
                                  box.min.y + random() * (box.max.y - box.min.y));
      const inset = 12;  // SVG units from the coast
      const clear = [[inset, 0], [-inset, 0], [0, inset], [0, -inset]]
          .every(([dx, dy]) => insidePolygon(new THREE.Vector2(p.x + dx, p.y + dy), polygon));
      const at = toWorld(p.x, p.y, y);
      if (!clear || at.distanceTo(anchor) < 2.4) continue;
      const kind = kinds[Math.floor(random() * kinds.length)];
      const model = this.props[kind].clone();
      model.position.copy(at);
      model.rotation.y = random() * Math.PI * 2;
      model.scale.setScalar(0.85 + random() * 0.3);
      this.scene.add(model);
      ++placed;
    }
  }

  buildSeaLanes(doc) {
    const material = new THREE.LineDashedMaterial({
      color: 0xe8f1ff, dashSize: 0.45, gapSize: 0.3, transparent: true, opacity: 0.75,
    });
    this.seaLanes = new Set();
    for (const line of doc.querySelectorAll('line.sea')) {
      const a = Number(line.getAttribute('data-a')), b = Number(line.getAttribute('data-b'));
      this.seaLanes.add(`${Math.min(a, b)}-${Math.max(a, b)}`);
      const p = toWorld(Number(line.getAttribute('x1')), Number(line.getAttribute('y1')), 0.9);
      const q = toWorld(Number(line.getAttribute('x2')), Number(line.getAttribute('y2')), 0.9);
      const mid = p.clone().lerp(q, 0.5);
      mid.y += 0.6 + p.distanceTo(q) * 0.15;
      const curve = new THREE.QuadraticBezierCurve3(p, mid, q);
      const geometry = new THREE.BufferGeometry().setFromPoints(curve.getPoints(24));
      const dashed = new THREE.Line(geometry, material);
      dashed.computeLineDistances();
      this.scene.add(dashed);
    }
  }

  isSeaLane(a, b) {
    return this.seaLanes.has(`${Math.min(a, b)}-${Math.max(a, b)}`);
  }

  landColor(t, owner) {
    const base = new THREE.Color(owner >= 0 ? TEAMS[owner].land : NEUTRAL);
    // A little variety per territory, so neighbours of one colour still read.
    const hsl = {};
    base.getHSL(hsl);
    const jitter = ((t * 37) % 11) / 11 - 0.5;
    return base.setHSL(hsl.h + jitter * 0.02, hsl.s * (0.92 + jitter * 0.1), hsl.l * (0.94 + jitter * 0.14));
  }

  recolor(t, owner, animate) {
    const land = this.lands[t];
    const target = this.landColor(t, owner);
    if (!animate) {
      land.color.copy(target);
      land.material.color.copy(target);
      return;
    }
    const start = land.color.clone();
    land.color.copy(target);
    this.tweens.add(0.5, (k) => {
      land.material.color.copy(start).lerp(target, k);
      // The flash of a territory changing hands.
      land.flash = 1 - k;
    });
  }

  // Jumps to |view|, with nothing animated.
  setView(view) {
    ++this.generation;
    this.busy = false;
    this.snap(view);
  }

  snap(view) {
    this.fastForward();
    for (let t = 0; t < 42; ++t) {
      const owner = view.o[t] === '-' ? -1 : Number(view.o[t]);
      if (this.garrisons[t].owner !== owner) this.recolor(t, owner, false);
      this.garrisons[t].sync(owner, view.u[t]);
    }
    this.clearArrow();
    this.view = view;
  }

  fastForward() {
    this.tweens.finishAll();
  }

  // Animates the step from |prev| to |view|. |speed| scales every duration.
  // A step still running is finished first: the board jumps to where it was
  // going, and what was left of it is dropped.
  async play(prev, view, {speed = 1} = {}) {
    if (this.busy && this.view) this.snap(this.view);
    const gen = ++this.generation;
    this.tweens.speed = speed;
    if (!prev) return this.setView(view);
    this.view = view;
    this.busy = true;
    const alive = () => gen === this.generation;
    try {
      await this.animate(prev, view, alive);
    } finally {
      if (alive()) this.busy = false;
    }
  }

  async animate(prev, view, alive) {
    if (view.d && view.a) return this.battle(prev, view, alive);
    const changed = [];
    for (let t = 0; t < 42; ++t) {
      if (prev.o[t] !== view.o[t] || prev.u[t] !== view.u[t]) changed.push(t);
    }
    // A fortify: armies leave one territory for another of the same owner.
    if (view.a && changed.length === 2) {
      const [from, to] = view.a;
      if (prev.o[from] === prev.o[to] && view.o[from] === prev.o[from] &&
          view.u[from] < prev.u[from]) {
        return this.march(prev, view, from, to, alive);
      }
    }
    if (view.a && !view.d) {
      this.showArrow(view.a[0], view.a[1], Number(view.o[view.a[0]]));
    }
    const many = changed.length > 6;
    changed.forEach((t, i) => {
      const owner = view.o[t] === '-' ? -1 : Number(view.o[t]);
      const grow = view.u[t] - prev.u[t];
      const go = () => {
        if (this.garrisons[t].owner !== owner) this.recolor(t, owner, true);
        this.garrisons[t].sync(owner, view.u[t], {animate: true});
        if (grow > 0 && !many) this.fx.text(this.anchors[t], `+${grow}`, '#f3d995');
      };
      if (many) this.tweens.wait(i * 0.025).then(() => alive() && go()); else go();
    });
    if (changed.length && !many) sfx.play('drop', {volume: 0.6});
    await this.tweens.wait(many ? 0.9 : 0.35);
  }

  async march(prev, view, from, to, alive) {
    const owner = Number(view.o[from]);
    const moved = prev.u[from] - view.u[from];
    this.showArrow(from, to, owner);
    const g = this.garrisons[from];
    g.face(this.anchors[to]);
    g.act('walk', {loop: true});
    sfx.play('step');
    await this.tweens.wait(0.2);
    if (!alive()) return;
    g.sync(owner, view.u[from], {animate: true});
    this.garrisons[to].sync(owner, view.u[to], {animate: true, from: this.anchors[from]});
    this.fx.text(this.anchors[to], `+${moved}`, '#f3d995');
    g.figures.forEach((f) => f.play('idle'));
    await this.tweens.wait(0.6);
    if (alive()) this.clearArrow();
  }

  async battle(prev, view, alive) {
    const [from, to] = view.a;
    const [attack, defend] = view.d;
    let lostA = 0, lostD = 0;
    for (let i = 0; i < Math.min(attack.length, defend.length); ++i) {
      if (attack[i] > defend[i]) ++lostD; else ++lostA;
    }
    const attacker = Number(prev.o[from]);
    const defender = Number(prev.o[to]);
    const conquered = view.c === to;
    const A = this.garrisons[from], D = this.garrisons[to];
    this.showArrow(from, to, attacker);
    A.face(this.anchors[to]);
    D.face(this.anchors[from]);
    const bySea = this.isSeaLane(from, to);
    A.act(bySea ? 'shoot' : 'attack', {speed: 1.4});
    sfx.play(bySea ? 'cannon' : 'swing');
    this.onDice?.(view.d, lostA, lostD, attacker, defender);

    // A volley: one shot per attacking die.
    const shots = attack.map((_, i) => this.tweens.wait(i * 0.06).then(() => this.shot(from, to, i)));
    await Promise.all(shots);
    if (!alive()) return;
    if (lostD) D.act('hit'); else D.act('block');
    if (lostA) A.act('hit');
    sfx.play(lostD ? 'clash' : 'metal');
    if (lostA) this.fx.text(this.anchors[from], `−${lostA}`, '#ff8a7a');
    if (lostD) this.fx.text(this.anchors[to], `−${lostD}`, '#ff8a7a');

    if (!conquered) {
      A.sync(attacker, view.u[from], {animate: true});
      D.sync(defender, view.u[to], {animate: true});
      await this.tweens.wait(0.25);
      if (alive()) this.clearArrow(0.3);
      return;
    }
    D.sync(defender, 0, {animate: true});
    await this.tweens.wait(0.18);
    if (!alive()) return;
    A.sync(attacker, view.u[from], {animate: true});
    this.recolor(to, attacker, true);
    D.sync(attacker, view.u[to], {animate: true, from: this.anchors[from]});
    this.fx.burst(this.anchors[to], TEAMS[attacker].color);
    sfx.play('conquer');
    this.shake = Math.max(this.shake, 0.35);
    await this.tweens.wait(0.6);
    if (alive()) this.clearArrow(0.3);
  }

  // One projectile from |from| to |to|, bursting on arrival.
  async shot(from, to, i) {
    const start = this.anchors[from].clone().add(new THREE.Vector3(0, 1.2, 0));
    const end = this.anchors[to].clone().add(new THREE.Vector3((i - 1) * 0.5, 0.4, (Math.random() - 0.5) * 0.6));
    const height = 2 + start.distanceTo(end) * 0.18;
    const stone = this.props.projectile_catapult.clone();
    stone.scale.setScalar(2.4);
    this.scene.add(stone);
    await this.tweens.add(0.3, (k) => {
      stone.position.lerpVectors(start, end, k);
      stone.position.y += Math.sin(Math.PI * k) * height;
      stone.rotation.x += 0.3;
    }, ease.linear);
    this.scene.remove(stone);
    this.fx.explosion(end, 0.7);
    sfx.play('boom', {volume: 0.7});
    this.shake = Math.max(this.shake, 0.18);
  }

  showArrow(from, to, owner) {
    this.clearArrow();
    const a = this.anchors[from].clone().add(new THREE.Vector3(0, 0.3, 0));
    const b = this.anchors[to].clone().add(new THREE.Vector3(0, 0.3, 0));
    // Alaska-Kamchatka goes the short way, over the map's edge.
    const wraps = Math.abs(a.x - b.x) > 45;
    const mid = a.clone().lerp(b, 0.5);
    mid.y += wraps ? 14 : 1.2 + a.distanceTo(b) * 0.22;
    if (wraps) mid.z -= 8;
    const curve = new THREE.QuadraticBezierCurve3(a, mid, b);
    const color = owner >= 0 ? TEAMS[owner].color : 0xf3d995;
    const material = new THREE.MeshBasicMaterial({color, transparent: true, opacity: 0.9, toneMapped: false});
    const tube = new THREE.Mesh(new THREE.TubeGeometry(curve, 40, 0.12, 8), material);
    const head = new THREE.Mesh(new THREE.ConeGeometry(0.36, 0.9, 12), material);
    const tip = curve.getPoint(1);
    const direction = curve.getTangent(1).normalize();
    head.position.copy(tip);
    head.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), direction);
    const arrow = new THREE.Group();
    arrow.add(tube, head);
    this.scene.add(arrow);
    this.arrow = arrow;
    this.tweens.add(0.2, (k) => { material.opacity = 0.9 * k; });
  }

  clearArrow(fade = 0) {
    const arrow = this.arrow;
    if (!arrow) return;
    this.arrow = null;
    const done = () => {
      this.scene.remove(arrow);
      arrow.traverse((n) => {
        if (n.isMesh) n.geometry.dispose();
      });
    };
    if (!fade) return done();
    const material = arrow.children[0].material;
    this.tweens.add(fade, (k) => { material.opacity = 0.9 * (1 - k); }).then(done);
  }

  // Highlights: {selected, targets: Set, sources: Set, hover}.
  setMarks(marks) {
    Object.assign(this.marks, marks);
  }

  setPending(placement) {
    for (let t = 0; t < 42; ++t) this.garrisons[t].setPending(placement[t] || 0);
  }

  pick(clientX, clientY) {
    const rect = this.renderer.domElement.getBoundingClientRect();
    const pointer = new THREE.Vector2(((clientX - rect.left) / rect.width) * 2 - 1,
                                      -((clientY - rect.top) / rect.height) * 2 + 1);
    const raycaster = new THREE.Raycaster();
    raycaster.setFromCamera(pointer, this.camera);
    const hit = raycaster.intersectObjects(this.pickables, false)[0];
    return hit ? hit.object.userData.t : -1;
  }

  // Screen position of territory |t|'s army, for tooltips.
  screenOf(t) {
    const p = this.anchors[t].clone().add(new THREE.Vector3(0, 2, 0)).project(this.camera);
    const rect = this.renderer.domElement.getBoundingClientRect();
    return {x: rect.left + (p.x + 1) / 2 * rect.width, y: rect.top + (1 - p.y) / 2 * rect.height};
  }

  resetCamera() {
    const from = this.camera.position.clone(), target = this.controls.target.clone();
    this.tweens.add(0.6, (k) => {
      this.camera.position.lerpVectors(from, this.home.position, k);
      this.controls.target.lerpVectors(target, this.home.target, k);
    });
  }

  frame() {
    const now = performance.now();
    const dt = Math.min(0.05, (now - this.lastTime) / 1000);
    this.lastTime = now;
    const time = now / 1000;
    this.tweens.step(dt);
    const animation = dt * Math.max(1, Math.min(this.tweens.speed, 3));
    for (const g of this.garrisons) {
      for (const f of g.figures) f.mixer.update(animation);
    }
    for (const f of this.dying) f.mixer.update(animation);
    this.fx.update(dt);
    this.seaTime.value = time;

    // Marks: a gold selection, pulsing targets, faint sources, hover.
    const pulse = 0.5 + 0.5 * Math.sin(time * 6);
    for (let t = 0; t < 42; ++t) {
      const land = this.lands[t];
      const e = land.material.emissive.setRGB(0, 0, 0);
      if (t === this.marks.selected) e.setRGB(0.45, 0.33, 0.08);
      else if (this.marks.targets.has(t)) e.setRGB(0.28 + 0.25 * pulse, 0.04, 0.02);
      else if (this.marks.sources.has(t)) e.setRGB(0.07, 0.06, 0.02);
      if (t === this.marks.hover) e.addScalar(0.08);
      if (land.flash > 0) e.addScalar(land.flash * 0.6);
    }

    this.controls.update();
    if (this.shake > 0.001) {
      const s = this.shake;
      const offset = new THREE.Vector3((Math.random() - 0.5) * s, (Math.random() - 0.5) * s, (Math.random() - 0.5) * s);
      this.camera.position.add(offset);
      this.composer.render();
      this.camera.position.sub(offset);
      this.shake *= Math.pow(0.02, dt);
    } else {
      this.composer.render();
    }
  }
}
