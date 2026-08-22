# Parallel Tempering on Optimized Paths

**Paper:** Syed, Romaniello, Campbell, Bouchard-Côté (ICML 2021)
**arXiv:** 2102.07720

---

## Core Idea

Standard PT uses a **linear path** (power posterior): `πt ∝ π0^(1-t) · π1^t`. When `π0` and `π1` are nearly mutually singular (e.g., well-separated narrow Gaussians), this path performs exponentially worse than optimal. The paper generalizes PT to **arbitrary parametric paths** and provides a practical optimization algorithm (PathOptNRPT) that can break the fundamental performance ceiling of linear paths.

---

## Background: Non-Reversible Parallel Tempering (NRPT)

### Setup

- **Reference:** `π0` — tractable (easy to sample)
- **Target:** `π1` — intractable
- **Path:** `πt` for `t ∈ [0,1]` interpolating `π0 → π1`
- **Schedule:** `TN = (t0, t1, ..., tN)` with `0 = t0 ≤ ... ≤ tN = 1`
- Run `N+1` chains targeting the product `πt0 · πt1 · ... · πtN`

### Algorithm 1: NRPT

Each iteration `m`:
1. **Local Exploration:** Update each chain `n` independently with any MCMC kernel targeting `πtn` (parallelizable).
2. **Communication:** Apply pairwise swaps using **DEO (Deterministic Even-Odd) masking**:
   - If `m` is even: swap pairs `(0,1), (2,3), ...`  (even set)
   - If `m` is odd: swap pairs `(1,2), (3,4), ...` (odd set)
   - Swap `(xn, xn+1)` accepted with MH probability:
     ```
     αn = min(1,  πtn(xn+1)·πtn+1(xn) / πtn(xn)·πtn+1(xn+1) )
     ```
3. **Track rejection rates:** `rn += (1 - αn)` per iteration; normalize at end.

NRPT is **non-reversible** (Okabe 2001) and dominates reversible PT. Even chains and odd chains alternate, creating a persistent directional flow of samples.

### Performance Metric: Round Trip Rate

A **round trip** = a sample originating at `π0` reaches `π1` then returns to `π0`.

**Round trip rate** (assuming stationarity and efficient local exploration):
```
τ(TN) = ( 2 + 2·Σ_{n=0}^{N-1}  r(tn, tn+1) / (1 - r(tn, tn+1)) )^{-1}
```

where `r(t, t')` is the expected swap rejection probability between chains at `t` and `t'`.

**Asymptotic (N → ∞) round trip rate:**
```
τ∞ = (2 + 2Λ)^{-1}
```
where `Λ ≥ 0` is the **global communication barrier** — a fixed constant of the pair `(π0, π1)`. For linear paths, `Λ` is often large, capping performance regardless of `N`.

---

## The Problem with Linear Paths

**Proposition 1:** For `π0 = N(μ0, σ²)`, `π1 = N(μ1, σ²)`, `z = |μ1 - μ0|/σ`:

1. Linear path: `τ∞ = Θ(1/z)` → decays linearly with separation
2. Optimal Gaussian path: `τ∞ = Ω(1/log z)` → decays only logarithmically

So the linear path can be **exponentially worse** than optimal. The fix: allow the intermediate distributions to have **inflated variance**, maintaining mutual overlap, even when transitioning between separated distributions.

---

## General Annealing Paths

### Definition

An **annealing path** is any continuous map `t ↦ πt` from `[0,1]` to probability densities, with `πt(x)` continuous in `t` for all `x`. Write `πt(x) = (1/Zt) exp(Wt(x))`.

Generalizations beyond the linear path:
- **Nonlinear exponents:** `πt ∝ π0^{η0(t)} · π1^{η1(t)}` for arbitrary continuous `η0, η1`
- **Mixture path:** `πt ∝ (1-t)π0 + t·π1`

### Global Communication Barrier for General Paths

Define the **secant communication barrier** between `t` and `t'`:
```
Λ(t, t') = (1/2) ∫₀¹ E[|A_{t,t'}(Xs, Xs')|] ds
```
where `Xs, Xs' ~ exp((1-s)Wt + s·Wt') / Zs` (the linear path between `πt` and `πt'`), and:
```
A_{t,t'}(x, x') = (Wt'(x) - Wt(x)) - (Wt'(x') - Wt(x'))
```

**Lemma 1:** `|r(t, t') - Λ(t, t')| ≤ C|t - t'|³`
(The secant barrier is a good O(|t-t'|³) approximation to the true rejection rate for nearby chains.)

**Theorem 2:** Under mild regularity conditions (Wt piecewise C², bounded derivatives), as `||TN|| → 0`:
```
τ(TN) → (2 + 2·Λ)^{-1}
```
where the global communication barrier is:
```
Λ = ∫₀¹ λ(t) dt
```
and the **instantaneous rejection rate** is:
```
λ(t) = lim_{Δt→0} r(t+Δt, t) / |Δt|
      = (1/2) E[ |dWt/dt(Xt) - dWt/dt(Xt')| ]    Xt, Xt' iid ~ πt
```

This generalizes the barrier from linear paths to any annealing path. Optimizing `Λ` optimizes `τ∞`.

---

## Path Optimization: Algorithm PathOptNRPT

### Algorithm 2: PathOptNRPT

**Input:** initial state `x`, path family `{πt^φ}`, initial `φ`, `N` chains, `M` PT iterations per scan, `S` tuning steps, learning rate `γ`

```
TN ← uniform schedule (0, 1/N, 2/N, ..., 1)
for s = 1 to S:
    {xm}, {rn} ← NRPT(x, πt^φ, TN, M)
    λ^φ ← CommunicationBarrier(TN, {rn})   # estimate instantaneous rate
    TN ← UpdateSchedule(λ^φ, N)            # tune β-schedule
    φ ← φ - γ · ∇φ [ Σ_{n} SKL(πtn^φ, πtn+1^φ) ]   # gradient step on SKL surrogate
    x ← xM
return φ, TN
```

**Coordinate-descent** alternating between schedule tuning and path tuning.

### Schedule Tuning (UpdateSchedule)

For fixed `φ`, the round trip rate is maximized when **all rejection rates are equal**. The optimal schedule satisfies:
```
∀n ∈ {1,...,N-1}:   (1/Λ^φ) ∫₀^{tn} λ^φ(s) ds = n/N
```

**Implementation:**
1. From empirical rejection rates `r(tn, tn+1)`, approximate `t ↦ ∫₀^t λ^φ(s) ds` via a **monotone cubic spline** (fitted to the cumulative rejection rates).
2. Solve for each `tn` using **bisection search**.

This is the same adaptive β-schedule tuning from Syed et al. (2019).

### Path Tuning: SKL Surrogate Objective

**Problem:** Directly optimizing the round trip rate `τ^φ(TN)` in early iterations is unreliable — when the path is poor, most rejection rates `≈ 1`, giving gradient estimates with near-zero signal-to-noise ratio.

**Solution:** Use the **symmetric KL divergence** as a surrogate objective.

**Derivation:** By two applications of Jensen's inequality:
```
(2/N) · Λ^φ(TN)²  ≤  Σ_{n=0}^{N-1} SKL(πtn^φ, πtn+1^φ)
```

where `SKL(p, q) = KL(p||q) + KL(q||p)` is the symmetric KL divergence, and the RHS equals the Fisher information path integral between adjacent chains (Dabak & Johnson, 2002).

**Gradient step:** Minimize `Σ_n SKL(πtn^φ, πtn+1^φ)` w.r.t. `φ` (stochastic gradients via Monte Carlo).

**Monitoring:** Track both the SKL surrogate and the true non-asymptotic communication barrier `Σ_n r(tn,tn+1)/(1-r(tn,tn+1))` during optimization to confirm SKL improvements translate to round trip improvements.

---

## Spline Annealing Path Family

### Exponential Annealing Path Family

Paths of the form:
```
πt ∝ π0^{η0(t)} · π1^{η1(t)} = exp(η(t)^T W(x))
```

where `W(x) = (W0(x), W1(x))` are reference/target log-densities, and `η(t) = (η0(t), η1(t))` are continuous functions with:
- `η(0) = (1, 0)` (start at reference)
- `η(1) = (0, 1)` (end at target)

The domain Ω = `{ξ ∈ R² : ∫ exp(ξᵀW(x))dx < ∞}` is **convex**. The linear path `η(t) = (1-t, t)` is a special case.

### K-Knot Linear Spline Family

Parameterize `η` as a **linear spline** with `K+1` knots `φ = (φ0, ..., φK) ∈ (R²)^{K+1}`:

For `t ∈ [(k-1)/K, k/K]`:
```
η^φ(t) = (k - Kt)·φ_{k-1} + (Kt - k + 1)·φ_k
```

with fixed endpoints `φ0 = (1, 0)` and `φK = (0, 1)`.

**Properties:**
- **Valid:** Since Ω is convex, any linear spline within Ω forms a valid annealing path.
- **Contains linear path:** `K=1` with `φ1 = (1-t, t)` recovers the standard path.
- **Approximation power:** With `K` knots, best-case approximation error is `O(M / K²)`.
- **Monotonicity constraint:** Enforce `1 = φ_{0,0} ≥ φ_{1,0} ≥ ... ≥ 0` and `0 = φ_{0,1} ≤ ... ≤ φ_{K,1} = 1` so the path always moves reference→target. This is a convex constraint set.

**Enforcing monotonicity during optimization:**
Instead of projecting (which can cause knot collisions), after each gradient step:
1. Identify a monotone subsequence of knots containing endpoints.
2. Remove non-monotone jumps.
3. Linearly interpolate the remaining knots with even spacing.

**Log-space optimization:** Take gradient steps in log-transformed knot coordinates to ensure strict positivity.

### Geometric Interpretation

For a 1D Gaussian example (Fig. 2), the optimized spline path in (η0, η1) space takes a **convex curved shape**:
- Start: `(1, 0)` = pure reference
- **Inflate variance** (move away from the diagonal)
- Shift mean from μ0 to μ1
- **Deflate variance** back to match target
- End: `(0, 1)` = pure target

More knots = smoother transition, but even `K=2` gives most of the gain.

---

## Key Quantities and Formulas Summary

| Symbol | Meaning |
|--------|---------|
| `πt` | Annealing path distribution at index `t ∈ [0,1]` |
| `Wt(x) = log πt(x) + const` | Log-density (unnormalized) |
| `TN = (t0,...,tN)` | Discretization schedule |
| `r(t, t')` | Expected swap rejection rate between chains at `t, t'` |
| `τ(TN)` | Non-asymptotic round trip rate |
| `Λ` | Global communication barrier = `∫₀¹ λ(t) dt` |
| `λ(t)` | Instantaneous rejection rate at `t` |
| `SKL(p,q)` | Symmetric KL = `KL(p‖q) + KL(q‖p)` |
| `φ` | Spline knot parameters `(φ0,...,φK) ∈ (R²)^{K+1}` |
| `η(t)` | 2D path in exponent space, knots at `φ` |

**Instantaneous rejection rate for exponential family paths:**
```
λ(t) = (1/2) E[ |η'(t)ᵀ(W(Xt) - W(Xt'))| ]    Xt, Xt' iid ~ πt
```

**SKL gradient** (via stochastic estimate from current chain samples):
```
∇φ Σ_n SKL(πtn^φ, πtn+1^φ)
```
See Appendix D of the paper for the explicit derivation of these gradient estimates.

---

## Experiments and Results

| Problem | N chains | Budget | Notes |
|---------|----------|--------|-------|
| 1D Gaussian (`π0=N(-1,0.01²)`, `π1=N(1,0.01²)`) | 50 | 45k samples | Linear path gets 0 round trips |
| Beta-Binomial (prior vs posterior at 0.2 and 0.7) | 50 | 45k samples | Conjugate model |
| Galaxy data (6-component GMM, 94 latent vars) | 35 | 50k samples | Multi-modal, label switching |
| High-dim Gaussian (`d=1..256`) | `⌈15√d⌉` | 50k samples | `K=4` spline |

**Key findings:**
1. Spline path (any `K > 1`) substantially outperforms linear path.
2. Exceeds **theoretical upper bound** for round trip rate achievable with any linear path.
3. Largest gain: `K=1 → K=2`; diminishing returns for `K > 2`.
4. SKL surrogate is a reliable proxy — lower estimation variance than true barrier in early iterations when rejection rates ≈ 1.
5. In high dimensions, the gap between linear and spline path grows with `d` because linear path cannot adapt the schedule fast enough.

**Optimizer:** Adagrad, learning rate 0.2–0.3.
**Scan definition:** One full iteration of the outer loop in Algorithm 2.

---