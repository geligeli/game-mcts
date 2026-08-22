#!/usr/bin/env python3
import os
# os.environ['XLA_PYTHON_CLIENT_PREALLOCATE'] = 'false'
# os.environ['XLA_FLAGS'] = '--xla_gpu_autotune_level=0'

'''
docker run \
    --gpus all \
    --ipc=host \
    --ulimit memlock=-1 \
    --ulimit stack=67108864 \
    --mount "source=/large_nfs/risk-game-ai,target=/large_nfs/risk-game-ai,type=bind" \
    --mount "source=/home/geli/.bazelcache,target=/home/ubuntu/.cache,type=bind" \
    takumi.city/jax-training:latest \
        bash -c \
    'cd /large_nfs/risk-game-ai; bazel run //game_mcts/cpp/risk/ascii_rendering:create_template_strings -- --width-start 48 --width-end 120 --text-width 3 --output-prefix /large_nfs/risk-game-ai/ascii_rendering/templates/tmpl'

# If you run on a 1080 card, use this image instead:
docker run \
    --gpus all \
    --ipc=host \
    --ulimit memlock=-1 \
    --ulimit stack=67108864 \
    --mount "source=/large_nfs/risk-game-ai,target=/large_nfs/risk-game-ai,type=bind" \
    --mount "source=/home/geli/.bazelcache,target=/home/ubuntu/.cache,type=bind" \
    takumi.city/jax-training-cuda12:latest \
    bash -c \
    'cd /large_nfs/risk-game-ai; bazel run //game_mcts/cpp/risk/ascii_rendering:create_template_strings -- --width-start 48 --width-end 120 --text-width 2 --output-prefix /large_nfs/risk-game-ai/ascii_rendering/templates/tmpl'
'''


import numpy as np
import jax.numpy as jnp
import jax
import jax.typing as jtp
import cv2
import ctypes
import ascii_rendering.risk_board_polygons as risk_board_polygons
import tqdm
import sys
from string import Template
from ascii_rendering import glyphs as glyph_module
from ascii_rendering import mhmc
from functools import partial
import os
import pathlib
os.environ.setdefault("XLA_PYTHON_CLIENT_PREALLOCATE", "false")


_lib_gpu = ctypes.cdll.LoadLibrary(
    "./cpp/jax_kernels/connected_components.so")
jax.ffi.register_ffi_target(
    "count_components",
    jax.ffi.pycapsule(_lib_gpu.CountComponents),
    platform="gpu",
)


@partial(jax.jit, static_argnums=(1,))
def count_components(cells: jnp.ndarray, n: int) -> jnp.ndarray:
    """Return int32 array of length n: counts[v] = #components with pixel value v."""
    out_shape = jax.ShapeDtypeStruct((n,), jnp.int32)
    return jax.ffi.ffi_call(
        "count_components", out_shape, vmap_method="broadcast_all")(cells, n=np.int32(n))


_POLYGONS = risk_board_polygons.LoadBoardPolygons()

_GLYPH_FOR_INDEX = [
    ' ',
    '▌',
    '▘',
    '▌',
    '▀',
    '▛',
    '▀',
    '▛',
]

NUM_TERRITORIES = len(_POLYGONS)


@jax.jit
def _compute_edges(cells: jtp.ArrayLike) -> jtp.ArrayLike:
    padded_cells = jnp.pad(cells, ((1, 0), (1, 0)), constant_values=0)
    shifted_up_left = padded_cells[:-1, :-1]
    shifted_up = padded_cells[:-1, 1:]
    shifted_left = padded_cells[1:, :-1]

    def compute_index(cell, up, left, up_left):
        '''
        bit pattern for computing edges
        12
        0?

        '''
        idx = jnp.int32(cell != left)+jnp.int32(cell != up) * \
            4+jnp.int32(cell != up_left)*2
        return idx
    return jax.vmap(jax.vmap(compute_index))(cells, shifted_up, shifted_left, shifted_up_left)


def _assemble_template_string(lines, optimized_cells: jtp.ArrayLike, text_coordinates_dicts: dict, text_width: int):
    reset_code = '\033[0m'
    cells_np = np.array(optimized_cells)
    colored_lines = []
    for x, line in enumerate(lines):
        colored_line = ''
        last_color = None
        skip_char = 0
        for y, char in enumerate(line):
            if skip_char > 0:
                skip_char -= 1
                continue
            if (x, y) in text_coordinates_dicts:
                # assert char == ' ', f"Expected space at {xy_typle} but got '{char}'"
                territory_nr = str(text_coordinates_dicts[(x, y)])
                skip_char = text_width-1
                char = f'${{UNITS_{territory_nr}}}'

            bg_code = '\033[4${COLOR_'+str(cells_np[x, y])+'}m'
            if last_color != bg_code:
                colored_line += bg_code + char
            else:
                colored_line += char
            last_color = bg_code
        colored_lines.append(colored_line + reset_code)
    return '\n'.join(colored_lines)


KERNEL_TYPE = jnp.float16


def _text_position_kernel(text_width):
    kernel_size = max(text_width, 2)*2-1
    center = kernel_size//2
    kernel = jnp.zeros((kernel_size, kernel_size), dtype=KERNEL_TYPE)
    kernel = kernel.at[center-1, center-1:center+text_width].set(1)
    kernel = kernel.at[center, center-1:center+text_width].set(1)
    kernel = kernel[None, None, ...]
    return kernel


@partial(jax.jit, static_argnames=['text_width'])
def _text_position_scores(cells: jtp.ArrayLike, text_width: int):
    img_nchw = (cells[None, None, :, :] == jnp.arange(1, NUM_TERRITORIES+1, dtype=jnp.uint8)[:, None,
                # pyright: ignore[reportIndexIssue, reportAttributeAccessIssue]
                                                                                             None, None]).astype(KERNEL_TYPE)
    kernel = _text_position_kernel(text_width)
    return jax.lax.conv(
        img_nchw, kernel,
        window_strides=(1, 1),
        padding="SAME",
    )


@partial(jax.jit, static_argnames=['text_width'])
def _text_coorinates(cells: jtp.ArrayLike, text_width: int) -> jtp.ArrayLike:
    def find_text_position(scores_map):
        return jnp.array(jnp.unravel_index(jnp.argmax(scores_map), scores_map.shape))
    scores = _text_position_scores(cells, text_width)[:, 0, :, :]
    return jax.vmap(find_text_position, in_axes=(0,))(scores)


def MakeTemplateStringFromCells(cells: jtp.ArrayLike, text_width=3) -> str:
    edges = _compute_edges(cells)
    lines = [''.join(_GLYPH_FOR_INDEX[i] for i in l) for l in edges]
    text_coordinates = _text_coorinates(cells, text_width)
    assert not (text_coordinates == jnp.array([0, 0])).all(
        axis=1).any(), "Text not found for some cells"
    text_coordinates_dicts = {tuple(l.tolist()): int(
        c) for l, c in zip(text_coordinates, range(1, 43))}
    return _assemble_template_string(lines, cells, text_coordinates_dicts, text_width)


def PrepareCells(num_tiles_w: int):
    width = glyph_module.tile_w * num_tiles_w
    height = width * risk_board_polygons.HEIGHT / risk_board_polygons.WIDTH
    num_tiles_h = round(height/glyph_module.tile_h)
    height = num_tiles_h * glyph_module.tile_h
    scale_factor = width / risk_board_polygons.WIDTH
    img = np.zeros((int(height),
                    int(width)), dtype=np.uint8)
    for i, p in enumerate(_POLYGONS):
        img = cv2.fillPoly(
            img, [(p*scale_factor).astype(np.int32)], color=i+1)
    img = jnp.array(img)

    def tile_image(img):
        return img.reshape((num_tiles_h, glyph_module.tile_h, num_tiles_w, glyph_module.tile_w)).transpose((0, 2, 1, 3))
    tiled_image = tile_image(img)

    def PreprocessCell(cell):
        return jnp.argmax(jnp.bincount(cell.flatten(), length=43)).astype(jnp.uint8)
    return jax.vmap(jax.vmap(PreprocessCell))(tiled_image), tiled_image


def OptimizeCells(cells: jtp.ArrayLike,
                  tiled_image: jtp.ArrayLike,
                  text_width: int,
                  batch_size: int = 1024,
                  num_batches: int = 100,
                  profile_dir=None,
                  profile_batches=None,
                  temperature: float = 0.1,
                  quiet=False):
    def tile_weight(idx):
        return (tiled_image == idx).mean(axis=(2, 3))
    OWNER_WEIGHTS = jax.vmap(tile_weight)(
        jnp.arange(0, NUM_TERRITORIES+1, dtype=jnp.uint8)).transpose(1, 2, 0)

    def OwnerLoss(cell, owner_weights):
        return 1-owner_weights[cell]

    expected_text_position_kernel_response = _text_position_kernel(
        text_width).sum().astype(jnp.float32)

    @jax.jit
    def loss_maps(cells):
        result = {}
        result["owner_loss"] = jax.vmap(
            jax.vmap(OwnerLoss, in_axes=(0, 0)), in_axes=(0, 0))(cells, OWNER_WEIGHTS)
        result['Text Loss'] = expected_text_position_kernel_response - \
            _text_position_scores(cells, text_width).max(
                axis=(1, 2, 3)).astype(jnp.float32)
        components = count_components(cells, NUM_TERRITORIES+1)
        non_single_components = jnp.sum(
            components[1:] != 1, dtype=jnp.float32)*10.0
        result["Non Connected"] = non_single_components
        return result

    @jax.jit
    def loss_fn(cells):
        loss_map_dict = loss_maps(cells)
        loss = 0.0
        for k in loss_map_dict:
            loss = loss + loss_map_dict[k].sum()
        return loss

    def gibbs_log_probs_at_k(cells, i, j, k):
        return -loss_fn(cells.at[i, j].set(k))

    gibbs_log_probs_at_k = jax.vmap(
        gibbs_log_probs_at_k, in_axes=(None, None, None, 0))

    @jax.jit
    def gibbs_log_probs(cells, i, j):
        log_probs = gibbs_log_probs_at_k(
            cells, i, j, jnp.arange(NUM_TERRITORIES+1, dtype=jnp.uint8))
        return log_probs - log_probs.max()

    def sample_from_axis_fn(key, cells, axis_idx, temp):
        log_probs = gibbs_log_probs(cells, axis_idx[0], axis_idx[1])
        log_probs = log_probs/temp
        return cells.at[*axis_idx].set(jax.random.categorical(key, log_probs).astype(jnp.uint8))

    step_fn, step_state = mhmc.make_discrete_gibbs_step(
        lambda x: -loss_fn(x),
        sample_from_axis_fn,
        cells)

    def visitor(step_state, visit_state, i):
        best_temp_loss_val = -step_state['log_p']
        visit_state['best_image'] = jax.lax.select(
            best_temp_loss_val < visit_state['best_loss'], step_state['x'], visit_state['best_image'])
        visit_state['best_loss'] = jnp.minimum(
            visit_state['best_loss'], best_temp_loss_val)
        visit_state['loss_maps'] = loss_maps(visit_state['best_image'])
        return visit_state

    key = jax.random.key(0)
    temp = temperature
    scan_fn = mhmc.single_chain_scan_fn(key, step_fn, visitor, temp)
    step_nr = jnp.int32(0)

    visit_state = {
        'best_loss': jnp.inf,
        'best_image': jnp.zeros_like(cells),
        'loss_maps': loss_maps(cells),
    }

    if profile_batches is not None:
        profile_start = 1
        profile_end = profile_start + profile_batches
    else:
        profile_start = profile_end = -1

    pbar = tqdm.trange(num_batches*batch_size,
                       desc="MCMC Cycles", file=sys.stdout, disable=quiet)
    frame = ""

    for batch_idx in range(num_batches):
        if profile_dir and batch_idx == profile_start:
            options = jax.profiler.ProfileOptions()
            options.python_tracer_level = 0
            options.host_tracer_level = 1
            options.device_tracer_level = 2
            options.gpu_enable_nvtx_tracking = True
            options.gpu_enable_cupti_activity_graph_trace = True
            jax.profiler.start_trace(profile_dir, profiler_options=options)

        with jax.profiler.StepTraceAnnotation(f"batch_{batch_idx}", batch_idx=batch_idx):
            step_state, visit_state, step_nr = mhmc.run_single_chain(
                scan_fn, step_state, batch_size, step_nr, visit_state)

        if profile_dir and batch_idx == profile_end:
            jax.profiler.stop_trace()
            if not quiet:
                print(f"Profile trace saved to {profile_dir}")

        if not quiet:
            sub_losses = ' '.join(
                [f'{k}:{v.sum():.2f}' for k, v in visit_state['loss_maps'].items()])
            tstring = MakeTemplateStringFromCells(
                visit_state['best_image'], text_width)
            frame = DemoTemplateString(tstring, text_width)
            sys.stdout.write("\033[1;H\033[2J")
            pbar.set_postfix({"loss": f"{visit_state['best_loss']:.2f}"})
            pbar.update(batch_size)
            sys.stdout.write("\n"+sub_losses+"\n")
            sys.stdout.write("\n"+frame+"\n")
            sys.stdout.flush()
    pbar.close()
    if not quiet:
        sys.stdout.write("\n" + frame+"\n")
        sys.stdout.flush()
    return MakeTemplateStringFromCells(visit_state['best_image'], text_width)


def DemoTemplateString(template_string: str, text_width: int):
    t = Template(template_string)
    unit_dict = {f'UNITS_{i}': f'{i:0{text_width}}' for i in range(1, 43)}
    colors = {f'COLOR_{i}': str(i % 6) for i in range(43)}
    colors['COLOR_0'] = '4'
    return t.substitute({**unit_dict, **colors})


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(
        description="Risk board ASCII template generator")
    subparsers = parser.add_subparsers(dest="command", required=True)

    # optimize subcommand
    opt_parser = subparsers.add_parser(
        "optimize", help="Run MCMC optimization and produce a template string")
    opt_parser.add_argument("--width", type=int, default=80,
                            help="Number of tiles wide (default: 80)")
    opt_parser.add_argument("--text-width", type=int, default=3,
                            help="Width reserved for unit text (default: 3)")
    opt_parser.add_argument("--batch-size", type=int,
                            default=1024, help="MCMC batch size (default: 1024)")
    opt_parser.add_argument("--num-batches", type=int, default=100,
                            help="Number of MCMC batches (default: 100)")
    opt_parser.add_argument("--output", "-o", type=str, default=None,
                            help="File path to save the template string")
    opt_parser.add_argument("--profile", type=str, default=None,
                            metavar="DIR", help="Enable JAX profiling, saving trace to DIR")
    opt_parser.add_argument("--profile-batches", type=int, default=5,
                            help="Number of batches to profile (centered in the run, default: 5)")
    opt_parser.add_argument("--temperature", type=float, default=0.1,
                            help="Gibbs sampler temperature (default: 0.1)")
    opt_parser.add_argument(
        "--quiet", "-q", action="store_true", help="Suppress progress output")

    # prepare subcommand
    prep_parser = subparsers.add_parser(
        "prepare", help="Generate a template string without optimization")
    prep_parser.add_argument(
        "--width", type=int, default=80, help="Number of tiles wide (default: 80)")
    prep_parser.add_argument("--text-width", type=int, default=3,
                             help="Width reserved for unit text (default: 3)")
    prep_parser.add_argument("--output", "-o", type=str, required=True,
                             help="File path to save the template string")

    # display subcommand
    disp_parser = subparsers.add_parser(
        "display", help="Display a stored template string")
    disp_parser.add_argument(
        "input", type=str, help="File path of the stored template string")
    disp_parser.add_argument("--text-width", type=int, default=3,
                             help="Width reserved for unit text (default: 3)")

    args = parser.parse_args()

    if args.command == "optimize":
        cells, tiled_image = PrepareCells(args.width)
        template_string = OptimizeCells(
            cells, tiled_image,
            text_width=args.text_width,
            batch_size=args.batch_size,
            num_batches=args.num_batches,
            profile_dir=args.profile,
            profile_batches=args.profile_batches if args.profile else None,
            temperature=args.temperature,
            quiet=args.quiet,
        )
        if args.output:
            with open(args.output, 'w') as f:
                f.write(template_string)
            cells_name = pathlib.Path(args.output).with_suffix('.npz')
            jnp.save(cells_name, cells, allow_pickle=False)
            print(f"Template saved to {args.output} and {cells_name}")

    elif args.command == "prepare":
        cells, _ = PrepareCells(args.width)
        template_string = MakeTemplateStringFromCells(cells, args.text_width)
        with open(args.output, 'w') as f:
            f.write(template_string)
        cells_name = pathlib.Path(args.output).with_suffix('.npz')
        jnp.save(cells_name, cells, allow_pickle=False)
        print(f"Template saved to {args.output} and {cells_name}")

    elif args.command == "display":
        with open(args.input, 'r') as f:
            template_string = f.read()
        # clear terminal before displaying
        sys.stdout.write("\033[1;H\033[2J")
        print(DemoTemplateString(template_string, args.text_width))
