// The page's side of engine_worker.js: one promise per call.
//
//   const engine = await Engine.load('agy-v08');  // engine.board: names, ...
//   const answer = await engine.call('attack', 3, 4, placement, 1);

export class Engine {
  static async load(botId) {
    const engine = new Engine();
    engine.board = await engine.call(
        'load', {url: new URL(`../../bots/${botId}.js`, import.meta.url).href});
    return engine;
  }

  constructor() {
    this.worker = new Worker(new URL('./engine_worker.js', import.meta.url),
                             {type: 'module'});
    this.pending = new Map();
    this.next = 0;
    this.worker.onmessage = ({data}) => {
      const {resolve, reject} = this.pending.get(data.id);
      this.pending.delete(data.id);
      data.ok ? resolve(data.value) : reject(new Error(data.error));
    };
  }

  call(cmd, ...args) {
    const id = this.next++;
    return new Promise((resolve, reject) => {
      this.pending.set(id, {resolve, reject});
      // "load" takes one object; the rk_ exports take positional args.
      this.worker.postMessage(
          {id, cmd, args: cmd === 'load' ? args[0] : args});
    });
  }

  terminate() {
    this.worker.terminate();
  }
}

// A placement as the engine takes it: "n0,...,n41".
export function placementString(placement) {
  return Array.from({length: 42}, (_, t) => placement[t] || 0).join(',');
}
