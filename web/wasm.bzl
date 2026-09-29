"""A candidate's policy as a WebAssembly module for the play page.

    wasm_bot(name = "claude-v18")

in web/bots/BUILD (stage.py writes it) compiles web/bots/<name>/ -- the
submission's files, staged there -- around //web:bot_wasm.cc, into <name>.js
and <name>.wasm (target :<name>_wasm): an ES module factory, createBot(),
with pthreads.

Every target is manual: only an explicit build of web/bots reaches emscripten.
"""

load("@emsdk//emscripten_toolchain:wasm_rules.bzl", "wasm_cc_binary")
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")

# The module runs in a dedicated Web Worker (web/static/js/engine_worker.js),
# which may block while the policy's own threads work: pthreads are nested
# workers, drawn from a pool made up front so a policy's std::thread starts
# without a round trip to the page.
WASM_LINKOPTS = [
    "-O3",
    "-sMODULARIZE=1",
    "-sEXPORT_ES6=1",
    "-sEXPORT_NAME=createBot",
    "-sENVIRONMENT=web,worker",
    "-sPTHREAD_POOL_SIZE=8",
    "-sALLOW_MEMORY_GROWTH=1",
    "-sINITIAL_MEMORY=268435456",
    "-sMAXIMUM_MEMORY=2147483648",
    # MCTS recurses as deep as a game goes; the 64 KiB default is too little.
    "-sSTACK_SIZE=8388608",
    "-sDEFAULT_PTHREAD_STACK_SIZE=4194304",
    "-sEXPORTED_RUNTIME_METHODS=ccall,UTF8ToString",
]

def wasm_bot(name, header = "strategy.h"):
    cc_library(
        name = name + "_strategy",
        hdrs = native.glob([name + "/*.h"]),
        srcs = native.glob([name + "/*.cc"], allow_empty = True),
        deps = [
            "//bots:bot_api",
            "@abseil-cpp//absl/log",
            "@abseil-cpp//absl/log:check",
            "@abseil-cpp//absl/strings",
        ],
        # Not our code: its warnings are its author's, not a build failure.
        copts = ["-Wno-error"],
        tags = ["manual"],
    )
    # Named as the candidate: the loader refers to its files by this name, the
    # wasm as <name>.wasm and itself, for its pthread workers, as <name>.
    cc_binary(
        name = name,
        srcs = ["//web:bot_wasm.cc"],
        copts = ["-Wno-error"],
        local_defines = [
            "CANDIDATE_ENTRY_HEADER=\\\"web/bots/%s/%s\\\"" % (name, header),
        ],
        linkopts = WASM_LINKOPTS,
        deps = [
            ":" + name + "_strategy",
            "//bots:bot_api",
            "//web:risk_match",
        ],
        tags = ["manual"],
    )
    wasm_cc_binary(
        name = name + "_wasm",
        cc_target = ":" + name,
        threads = "emscripten",
        outputs = [name + ".js", name + ".wasm"],
        tags = ["manual"],
    )
