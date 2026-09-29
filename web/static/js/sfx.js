// Sound: CC0 samples (web/CREDITS.md) through WebAudio. Each cue is a group
// of variants, one picked at random per play and pitched a little, so a
// hundred sword clashes do not sound like one.
//
//   sfx.play('clash');  sfx.loop('drums', true);  sfx.setMuted(true);

const AUDIO = new URL('../assets/audio/', import.meta.url);
const n = (prefix, count, pad = 0, from = 1) =>
  Array.from({length: count}, (_, i) => `${prefix}${String(i + from).padStart(pad, '0')}.ogg`);

const CUES = {
  shake: {files: ['dice-shake-1.ogg'], volume: 0.5},
  throw: {files: ['dice-throw-1.ogg', 'dice-throw-2.ogg', 'dice-throw-3.ogg'], volume: 0.6},
  clash: {files: n('sword_clash_', 6), volume: 0.55},
  swing: {files: n('sword_', 4), volume: 0.45},
  cannon: {files: n('cannon_0', 3), volume: 0.5},
  boom: {files: n('bang_0', 4), volume: 0.45},
  hit: {files: n('impactPunch_heavy_00', 3, 0, 0), volume: 0.5},
  metal: {files: n('impactMetal_heavy_00', 3, 0, 0), volume: 0.35},
  step: {files: n('footstep_grass_00', 4, 0, 0), volume: 0.35},
  drop: {files: ['chop.ogg'], volume: 0.35},
  draw: {files: ['drawKnife1.ogg'], volume: 0.4},
  click: {files: ['click1.ogg'], volume: 0.4},
  hover: {files: ['rollover2.ogg'], volume: 0.18},
  select: {files: ['switch7.ogg'], volume: 0.45},
  deny: {files: ['click3.ogg'], volume: 0.4},
  conquer: {files: ['jingles_HIT05.ogg'], volume: 0.4},
  win: {files: ['jingles_STEEL00.ogg'], volume: 0.55},
  lose: {files: ['jingles_PIZZI01.ogg'], volume: 0.55},
  turn: {files: ['jingles_HIT00.ogg'], volume: 0.3},
  drums: {files: ['drums.wav'], volume: 0.16},
};

class Sfx {
  constructor() {
    this.context = null;
    this.buffers = new Map();  // file -> Promise<AudioBuffer>
    this.loops = new Map();
    this.last = new Map();     // cue -> time last played
    try {
      this.muted = localStorage.getItem('risk2.muted') === '1';
      this.music = localStorage.getItem('risk2.music') !== '0';
    } catch {
      this.muted = false;
      this.music = true;
    }
    // Browsers start audio only after a gesture.
    const unlock = () => {
      this.ensure();
      this.context.resume();
      if (this.music && this.wantDrums) this.loop('drums', true);
    };
    addEventListener('pointerdown', unlock, {once: true});
    addEventListener('keydown', unlock, {once: true});
  }

  ensure() {
    if (!this.context) {
      this.context = new AudioContext();
      this.master = this.context.createGain();
      this.master.gain.value = this.muted ? 0 : 1;
      this.master.connect(this.context.destination);
    }
  }

  buffer(file) {
    if (!this.buffers.has(file)) {
      this.buffers.set(file, fetch(new URL(file, AUDIO))
          .then((r) => r.arrayBuffer())
          .then((data) => this.context.decodeAudioData(data))
          .catch(() => null));
    }
    return this.buffers.get(file);
  }

  // Loads every cue now, so the first battle is not silent.
  preload() {
    this.ensure();
    for (const cue of Object.values(CUES)) cue.files.forEach((f) => this.buffer(f));
  }

  async play(name, {volume = 1, rate = 1, delay = 0} = {}) {
    if (this.muted) return;
    const cue = CUES[name];
    this.ensure();
    // A burst of the same cue within 40 ms is one sound.
    const now = performance.now();
    if (now - (this.last.get(name) || 0) < 40) return;
    this.last.set(name, now);
    const buffer = await this.buffer(cue.files[Math.floor(Math.random() * cue.files.length)]);
    if (!buffer || this.context.state !== 'running') return;
    const source = this.context.createBufferSource();
    source.buffer = buffer;
    source.playbackRate.value = rate * (0.92 + Math.random() * 0.16);
    const gain = this.context.createGain();
    gain.gain.value = cue.volume * volume;
    source.connect(gain).connect(this.master);
    source.start(this.context.currentTime + delay);
  }

  async loop(name, on) {
    this.wantDrums = on;
    this.ensure();
    const playing = this.loops.get(name);
    if (!on || !this.music) {
      if (playing) playing.stop();
      this.loops.delete(name);
      return;
    }
    if (playing || this.context.state !== 'running') return;
    const buffer = await this.buffer(CUES[name].files[0]);
    if (!buffer || this.loops.has(name)) return;
    const source = this.context.createBufferSource();
    source.buffer = buffer;
    source.loop = true;
    const gain = this.context.createGain();
    gain.gain.value = CUES[name].volume;
    source.connect(gain).connect(this.master);
    source.start();
    this.loops.set(name, source);
  }

  setMuted(muted) {
    this.muted = muted;
    this.ensure();
    this.master.gain.value = muted ? 0 : 1;
    try { localStorage.setItem('risk2.muted', muted ? '1' : '0'); } catch {}
  }

  setMusic(on) {
    this.music = on;
    try { localStorage.setItem('risk2.music', on ? '1' : '0'); } catch {}
    this.loop('drums', on);
  }
}

export const sfx = new Sfx();
