"""
Round-trip counting for parallel tempering runs.

A "round trip" is defined as the hottest replica (initially at chain 0,
the reference/most-diffuse end) travelling all the way to chain N-1 (the
target/coldest end) and then returning to chain 0.

Both mhmc.parallel_tempering and mhmc_skl.optimized_parallel_tempering
return a swap_history of shape (num_steps, n_chains) where each row is a
permutation array such that new_data = old_data[perm], i.e. perm[i]=j means
position i received the replica that was previously at position j.
"""

import numpy as np


def count_round_trips(swap_history):
    """Count complete round trips of the hottest replica.

    Tracks the replica that starts at position 0 (hottest chain) through the
    sequence of swaps and counts how many times it completes the journey:
        position 0  →  position N-1  →  position 0

    Parameters
    ----------
    swap_history : array-like, shape (num_steps, n_chains)
        Permutation arrays from a parallel tempering run.  Row t is the
        permutation applied at step t: new_data[i] = old_data[perm[i]].

    Returns
    -------
    n_round_trips : int
    positions : ndarray, shape (num_steps + 1,)
        Chain position of the tracked replica at every step (including step 0).
    """
    swap_history = np.asarray(swap_history, dtype=np.int32)
    num_steps, n_chains = swap_history.shape

    # Precompute all inverse permutations at once.
    # inv_perm[t, j] = position that the replica from position j moves to.
    inv_perms = np.argsort(swap_history, axis=1)

    # Walk the replica that starts at position 0.
    positions = np.empty(num_steps + 1, dtype=np.int32)
    positions[0] = 0
    for t in range(num_steps):
        positions[t + 1] = inv_perms[t, positions[t]]

    # State machine: count hot(0) → cold(N-1) → hot(0) transitions.
    n_round_trips = 0
    looking_for_cold = True   # True  → next target is position N-1
                               # False → next target is position 0
    for p in positions:
        if looking_for_cold and p == n_chains - 1:
            looking_for_cold = False
        elif not looking_for_cold and p == 0:
            n_round_trips += 1
            looking_for_cold = True

    return n_round_trips, positions
