import jax
import jax.numpy as jnp
import jax.typing as jtp
from collections.abc import Callable
from functools import partial


def make_metropolis_hastings_step(unnormalized_log_p_fn: Callable[..., jtp.ArrayLike], perterb_fn: Callable, x):
    """Create a Metropolis-Hastings MCMC step function.

    Uses an exponential acceptance criterion scaled by temperature,
    equivalent to the standard MH ratio in log-space.

    Args:
        unnormalized_log_p_fn: Function mapping a state x to its
            unnormalized log probability.
        perterb_fn: Function (key, x) -> x' that proposes a new state
            by perturbing the current one.
        x: Initial state.

    Returns:
        step_fn: JIT-compiled function (key, step_state, step_index, temp)
            that performs one MH step.
        step_state: Initial step state dict with keys 'x' and 'log_p'.
    """
    def mhmcmc_step_fn(key: jtp.ArrayLike, step_state, step_index: int, temp: float):
        perterb_key, accept_key = jax.random.split(key, 2)
        next_x = perterb_fn(perterb_key, step_state['x'])
        next_log_p = unnormalized_log_p_fn(next_x)
        accept = step_state['log_p'] - \
            jax.random.exponential(accept_key)*temp < next_log_p
        return {
            'x': jax.lax.select(accept, next_x, step_state['x']),
            'log_p':  jax.lax.select(accept, next_log_p, step_state['log_p'])
        }
    log_p = unnormalized_log_p_fn(x)
    step_state = {
        'x': x,
        'log_p': log_p,
    }
    return mhmcmc_step_fn, step_state


def make_discrete_gibbs_step(unnormalized_log_p_fn: Callable, sample_from_axis_fn: Callable, x: jtp.ArrayLike, num_axes=None, axis_nr_to_idx_fn=None):
    """Create a discrete Gibbs sampling step function.

    Cycles through axes sequentially, resampling one coordinate at a time.

    Args:
        unnormalized_log_p_fn: Function mapping a state x to its
            unnormalized log probability.
        sample_from_axis_fn: Function (key, x, axis_idx, temp) that
            returns a new state with the given axis resampled.
        x: Initial state (used to determine number and shape of axes).

    Returns:
        step_fn: Function (key, step_state, step_index, temp) that
            performs one Gibbs step.
        step_state: Initial step state dict with keys 'x', 'log_p',
            and 'axis_nr'.
    """
    num_axes = num_axes or x.size
    # axes_shape = x.shape
    axis_nr_to_idx_fn = axis_nr_to_idx_fn or (lambda i: jnp.unravel_index(i, x.shape))

    def gibbs_step(key: jtp.ArrayLike, step_state, step_index: int, temp=1.0):
        axis_nr = jnp.mod(step_index, num_axes)
        axis_idx = axis_nr_to_idx_fn(axis_nr)
        next_x = sample_from_axis_fn(key, step_state['x'], axis_idx, temp)
        return {
            'x': next_x,
            'log_p': unnormalized_log_p_fn(next_x),
            'axis_nr': axis_nr,
        }
    step_state = {
        'x': x,
        'log_p': unnormalized_log_p_fn(x),
        'axis_nr': 0,
    }
    return gibbs_step, step_state


def single_chain_scan_fn(key: jtp.ArrayLike, step_fn, visit_fn=None, temp=1.0):
    """Build a JIT-compiled scan body for running a single MCMC chain.

    Args:
        key: PRNG key. Subkeys are derived via fold_in at each step.
        step_fn: Step function (key, step_state, step_index, temp) -> step_state.
        visit_fn: Optional callback (step_state, visit_state, i) -> visit_state
            called before each step to accumulate statistics.
        temp: Temperature for the chain.

    Returns:
        A JIT-compiled scan function suitable for use with jax.lax.scan
        via run_single_chain.
    """
    def f(carry, _):
        step_state, visit_state, i, = carry
        subkey = jax.random.fold_in(key, i)
        if visit_fn is not None:
            visit_state = visit_fn(step_state, visit_state, i)
        step_state = step_fn(subkey, step_state, i, temp)
        return (step_state, visit_state, i+1), None
    return jax.jit(f)


@partial(jax.jit, static_argnums=(0,2))
def run_single_chain(scan_fn, step_state, num_samples: int, step_nr=None, visit_state=None):
    """Run a single MCMC chain for a given number of steps.

    Args:
        scan_fn: Scan body function produced by single_chain_scan_fn.
        step_state: Initial step state dict.
        num_samples: Number of MCMC steps to run.
        step_nr: Starting step index (allows resuming a chain).
        visit_state: Initial visitor accumulator state.

    Returns:
        step_state: Final step state after all samples.
        visit_state: Final visitor accumulator state.
        step_nr: Step index after the run.
    """
    step_nr = jnp.int32(0) if step_nr is None else step_nr
    (step_state, visit_state, step_nr), _ = jax.lax.scan(
        scan_fn, (step_state, visit_state, step_nr), xs=None, length=num_samples, unroll=32)
    return step_state, visit_state, step_nr


def make_permutation(key, num_chains, step_idx, do_swap_fn):
    """Build a permutation array for replica-exchange swaps.

    On even steps, proposes swaps between pairs (0,1), (2,3), ...;
    on odd steps, between pairs (1,2), (3,4), ... Each proposed swap
    is accepted or rejected independently via do_swap_fn.

    Args:
        key: PRNG key for swap acceptance decisions.
        num_chains: Total number of parallel tempering chains.
        step_idx: Current step index (parity determines swap partners).
        do_swap_fn: Function (key, src, dst) -> (accept,) that decides
            whether to swap two chains.

    Returns:
        Permutation array of shape (num_chains,) mapping each position
        to its new source index.
    """
    zero_when_even = jnp.mod(step_idx, 2)
    src = jax.lax.select(zero_when_even == 0, jnp.arange(
        0, num_chains-1, 2), jnp.arange(1, num_chains, 2))
    dst = jnp.mod(src+1, num_chains)
    flat_indices = jnp.array([src, dst]).transpose(1, 0).reshape(-1)
    def maybe_swap(key, s, d):
        return jax.lax.select(do_swap_fn(key, s, d).squeeze(), jnp.array([d, s]),  jnp.array([s, d]))
    r = jax.vmap(maybe_swap)(jax.random.split(
        key, len(src)), src, dst).reshape(-1)
    idx = jnp.arange(num_chains)
    return idx.at[flat_indices].set(r)


def count_cycles(indices):
    """Count how many times chain index 0 completes a full round-trip
    through all positions and back.

    Tracks a sensor that alternates between the last and first positions.
    A cycle is counted each time index 0 returns to position 0 after
    having visited the last position.

    Args:
        indices: Array of shape (num_steps, num_chains) tracking which
            original chain occupies each position after replica-exchange
            permutations.

    Returns:
        cycles: Total number of completed round-trips.
        num_cycles_plt: Array of cumulative cycle counts at each step,
            useful for plotting mixing diagnostics.
    """
    num_cycles = 0
    last_pos = 0

    def f(carry, x):
        (num_cycles, last_pos) = carry
        sensor_pos = jax.lax.select(last_pos == 0, len(x)-1, 0)
        tripped = x[sensor_pos] == 0
        num_cycles = num_cycles + jnp.logical_and(tripped, sensor_pos == 0)
        last_pos = jax.lax.select(tripped, sensor_pos, last_pos)
        return (num_cycles, last_pos), num_cycles
    (cycles, _), num_cycles_plt = jax.lax.scan(
        f, (num_cycles, last_pos), indices)
    return cycles, num_cycles_plt


def replicate_tree(n: int, t):
    """Broadcast a pytree to have n copies along a new leading axis.

    Args:
        n: Number of replicas.
        t: A JAX pytree whose leaves are arrays.

    Returns:
        A pytree with the same structure where each leaf has shape
        (n, *original_shape).
    """
    return jax.tree.map(lambda x: jnp.broadcast_to(x, [n, *x.shape]), t)


def parallel_tempering_scan_fn(key: jtp.ArrayLike,
                               step_fn: Callable,
                               step_states,
                               temperatures: jtp.ArrayLike,
                               visit_fn=None,
                               visit_state=None):
    """Run parallel tempering (replica-exchange) Monte Carlo.

    Runs multiple MCMC chains at different temperatures, periodically
    proposing swaps between adjacent-temperature chains using the
    Metropolis criterion.

    Args:
        key: PRNG key.
        step_fn: Step function (key, step_state, step_index, temp) -> step_state,
            must be vmap-compatible across chains.
        step_states: Batched initial step state dict (leading axis = num_chains).
        temperatures: Array of temperatures, one per chain.
        num_samples: Number of sweep iterations to run.
        visit_fn: Optional callback (step_states, visit_state, indices, i) -> visit_state
            called after each sweep to accumulate statistics.
        visit_state: Initial visitor accumulator state.


    """
    num_chains = temperatures.size

    def scan_body(carry, _):
        # step_states, visit_state, indices, step_nr, key = carry
        key = carry['key']
        step_states = carry['step_states']
        visit_state = carry['visit_state']
        step_nr = carry['step_nr']
        indices = carry['indices']
        t = carry['temperatures']

        key, step_key, permute_key = jax.random.split(key, 3)
        # run one sample of each chain at their respective temperatures
        vstep = jax.vmap(step_fn, in_axes=(0, 0, None, 0))
        step_states = vstep(jax.random.split(
            step_key, t.size), step_states, step_nr, t)

        BETA=1/t
        LOG_P = step_states['log_p']
        log_alphas = jnp.minimum(0, -jnp.diff(BETA.squeeze())*jnp.diff(LOG_P.squeeze()))

        def do_swap(key, a, b):
            ratio = log_alphas[a]
            accept_swap = jnp.log(jax.random.uniform(key)) < ratio
            is_neighbor = jnp.abs(a - b) == 1
            res = jnp.logical_and(is_neighbor, accept_swap)
            return res

        permutation = make_permutation(
            permute_key, num_chains, step_nr, do_swap)

        parallel_tempering_info = {
            'permutation': permutation,
            'log_alphas': log_alphas,
        }

        step_states['log_p'] = step_states['log_p'][permutation]
        step_states['x'] = step_states['x'][permutation, ...]
        indices = indices[permutation]

        # Record the states
        if visit_fn is not None:
            visit_state = visit_fn(
                step_states, visit_state, parallel_tempering_info, step_nr)

        new_carry = {
            'step_states': step_states,
            'visit_state': visit_state,
            'indices': indices,
            'step_nr': step_nr + 1,
            'key': key,
            'temperatures': t,
        }
        return new_carry, indices
    state = {
        'step_states': step_states,
        'visit_state': visit_state,
        'indices': jnp.arange(num_chains),
        'step_nr': 0,
        'key': key,
        'temperatures': temperatures,
    }
    return scan_body, state


def run_parallel_tempering(step_fn, step_state,
                           T_max: float,
                           T_min: float,
                           num_chains: int,
                           num_cycles: int,
                           num_samples_per_cycle: int,
                           visit_fn=None,
                           visit_state=None,
                           cycle_callback=None):
    assert T_max > T_min, "T_max must be greater than T_min"
    assert num_chains > 1, "num_chains must be greater than 1"
    assert num_cycles > 0, "num_cycles must be positive"
    assert num_samples_per_cycle > 0, "num_samples_per_cycle must be positive"

    beta_min = 1.0 / T_max
    beta_max = 1.0 / T_min
    betas = jnp.linspace(beta_min, beta_max, num_chains)
    temperatures = 1.0 / betas
    cycle_counter_state = {
        'num_cycles': jnp.int32(0),
        'last_pos': jnp.int32(0),
    }
    visit_state = {
        'rejections': jnp.zeros(len(temperatures)-1),
        'log_alphas': jnp.zeros((num_cycles*num_samples_per_cycle, len(temperatures)-1)),
        'count': 0,
        'custom_visit_state': visit_state,
        'cycle_counter_state': cycle_counter_state,
    }
    step_states = replicate_tree(num_chains, step_state)

    def cycle_counter(cycle_counter_state, x):
        num_cycles = cycle_counter_state['num_cycles']
        last_pos = cycle_counter_state['last_pos']
        sensor_pos = jax.lax.select(last_pos == 0, len(x)-1, 0)
        tripped = x[sensor_pos] == 0
        num_cycles = num_cycles + jnp.logical_and(tripped, sensor_pos == 0)
        last_pos = jax.lax.select(tripped, sensor_pos, last_pos)
        cycle_counter_state = {
            'num_cycles': num_cycles,
            'last_pos': last_pos,
        }
        return cycle_counter_state, num_cycles

    def collect_statistics(step_state, visit_state, parallel_tempering_info, step_nr):
        visit_state['cycle_counter_state'], _ = cycle_counter(visit_state['cycle_counter_state'], parallel_tempering_info['permutation'])
        visit_state['rejections'] = visit_state['rejections'] + (1-jnp.exp(parallel_tempering_info['log_alphas']))
        visit_state['count'] += 1
        if visit_fn is not None:
            visit_state['custom_visit_state'] = visit_fn(step_state, visit_state['custom_visit_state'], parallel_tempering_info, visit_state['cycle_counter_state'], step_nr)
        return visit_state

    def optimize_schedule(temperatures, visit_state):
        rejection_rates = visit_state['rejections']/visit_state['count'] + 1e-8
        cum_barrier = jnp.concatenate([jnp.array([0.0]), jnp.cumsum(rejection_rates)])
        cum_barrier_norm = cum_barrier / cum_barrier[-1]
        N = rejection_rates.shape[0]
        target_barrier = jnp.linspace(0.0, 1.0, N + 1)
        return jnp.interp(target_barrier, cum_barrier_norm, temperatures)

    key = jax.random.key(0)

    scan_body, state = parallel_tempering_scan_fn(key, step_fn, step_states, temperatures, visit_fn=collect_statistics, visit_state=visit_state)
    scan_body = jax.jit(scan_body)
    indices = None
    for _ in range(num_cycles):
        state['temperatures'] = temperatures
        state['visit_state']['rejections'] = jnp.zeros_like(state['visit_state']['rejections'])
        state['visit_state']['count'] = 0
        state, indices = jax.lax.scan(scan_body, state, jnp.arange(num_samples_per_cycle))
        if cycle_callback:
            cycle_callback(state, indices)
        temperatures = optimize_schedule(temperatures, state['visit_state'])

    return state, indices

