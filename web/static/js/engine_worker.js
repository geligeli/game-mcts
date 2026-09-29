// The play page's engine: a candidate's WebAssembly (web/bot_wasm.cc) in a
// dedicated worker, so the page stays responsive while the policy thinks and
// its own threads (pthreads, nested workers) can block.
//
// Messages in: {id, cmd, args}; cmd "load" takes {url} and answers the board,
// any other calls the export rk_<cmd> with args. Messages out: {id, ok, value}
// or {id, ok: false, error}.

let bot = null;

const SIGNATURES = {
  new: ['number', 'number', 'number', 'string'],
  state: [],
  place: ['number'],
  quick_setup: [],
  attack: ['number', 'number', 'string', 'number', 'number'],
  fortify: ['number', 'number', 'number', 'string'],
  bot_step: [],
  board: [],
};

self.onmessage = async (event) => {
  const {id, cmd, args = []} = event.data;
  try {
    let value;
    if (cmd === 'load') {
      const {default: createBot} = await import(args.url);
      bot = await createBot();
      value = JSON.parse(bot.ccall('rk_board', 'string', [], []));
    } else if (cmd === 'fast_defense') {
      bot.ccall('rk_fast_defense', null, ['number'], args);
      value = null;
    } else {
      const started = performance.now();
      value = JSON.parse(
          bot.ccall('rk_' + cmd, 'string', SIGNATURES[cmd], args));
      value.ms = performance.now() - started;
    }
    self.postMessage({id, ok: true, value});
  } catch (error) {
    self.postMessage({id, ok: false, error: String(error && error.stack || error)});
  }
};
