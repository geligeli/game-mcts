# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

The parent repo (`/large_nfs/risk-game-ai/`) uses **Bazel**. There are no Bazel BUILD files in `ascii_rendering/` yet — the module is currently a collection of standalone Python scripts.

Pre-commit hooks (defined in `../.pre-commit-config.yaml`):
- `clang-format` with Google style for C++ files
- `jupyter nbconvert --ClearOutputPreprocessor.enabled=True --inplace` clears notebook outputs on commit

## Architecture

This module converts a Risk board (loaded from COCO JSON segmentation) into ASCII art via JAX-based parallel tempering optimization.

**Data flow:**
1. `risk_board_polygons.LoadBoardPolygons()` — loads COCO JSON, applies perspective transform, returns polygon list normalized to a 30×20 unit board
2. `image_from_polygons.polysToImage()` — builds polygon adjacency graph, greedy-colors it, rasterizes to image (300 DPI, 9-color palette)
3. `glyphs.py` — defines ASCII box-drawing glyphs with 8-direction connection ports; `RenderGlyph()` rasterizes a character using Consolas font; `glyph_addition()` merges two glyphs by OR-ing their port sets
4. `chamfer_distance.chamfer_distance3()` + `edt.distance_transform_2d()` — compares a candidate ASCII mask against the target mask; EDT is a JAX-native separable 1D parabola-envelope algorithm, vmap'd over rows/columns
5. `mhmc.parallel_tempering()` or `mhmc_skl.optimized_parallel_tempering()` — samples glyph assignments via replica exchange MCMC; user supplies `loss_fn` (typically chamfer distance) and `perturbation_fn`

**Two sampler variants:**
- `mhmc.py` — standard parallel tempering with even/odd checkerboard replica swaps
- `mhmc_skl.py` — Non-Reversible Parallel Tempering (NRPT): persistent swap velocities, deterministic even-odd (DEO) masking, automatic β-schedule tuning via `tune_schedule_bisection()`, spline energy paths via `compute_spline_energy()`

**Key dependencies:** JAX/jax.numpy, OpenCV (`cv2`), NumPy, PIL (font rendering), Optax (used in `mhmc_skl.py`)

**`glyph_matcher.py` is an empty placeholder** — the glyph-matching logic has not been implemented yet.

## Algorithm Reference

`mhmc_skl.py` implements the algorithm from **"Parallel Tempering on Optimized Paths"** (Syed, Romaniello, Campbell, Bouchard-Côté, ICML 2021, arXiv:2102.07720). A detailed summary is in `parallel_tempering_optimized_paths.md`.

Key ideas:
- Standard PT uses a **linear path** `πt ∝ π0^(1-t) · π1^t` which degrades exponentially when reference and target are nearly mutually singular
- **Non-Reversible PT (NRPT):** deterministic even/odd (DEO) swap masking creates a persistent directional flow, dominating reversible PT; asymptotic round trip rate `τ∞ = (2 + 2Λ)⁻¹` where `Λ = ∫λ(t)dt` is the global communication barrier
- **Spline paths:** parameterize the path as a K-knot linear spline in 2D exponent space `η(t) = (η0(t), η1(t))` so `πt ∝ π0^{η0(t)} · π1^{η1(t)}`; optimized paths can break the barrier ceiling of any linear path
- **PathOptNRPT (Algorithm 2):** coordinate-descent loop alternating between (a) schedule tuning via bisection on a monotone cubic spline fit to empirical rejection rates, and (b) gradient descent on knot parameters using symmetric KL divergence as a surrogate objective (more stable than the true barrier when rejection rates ≈ 1)
