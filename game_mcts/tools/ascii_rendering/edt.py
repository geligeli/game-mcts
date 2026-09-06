import jax
import jax.numpy as jnp

# DTYPE=jnp.float16
# INF=jnp.float16(6e4)

DTYPE=jnp.float32
INF=jnp.float32(1e12)

@jax.jit
def edt_1d_jax(f):
    n = f.shape[0]
    indices = jnp.arange(n, dtype=DTYPE)
    f_plus_q2 = f + indices**2
    def intersect(p1, p2):
        # Intersection formula: ((f[p2] + p2^2) - (f[p1] + p1^2)) / (2(p2-p1))
        return (f_plus_q2[p2] - f_plus_q2[p1]) / (2.0 * (p2 - p1))

    def compute_envelope(state, q):
        k, v, z = state
        
        def cond_fn(carry):
            curr_k = carry
            s = intersect(v[curr_k], q)
            # Pop if intersection s is to the left of the current boundary z[k]
            return (curr_k > 0) & (s <= z[curr_k])

        def body_fn(carry):
            return carry - 1

        # 1. Pop old parabolas
        k_final = jax.lax.while_loop(cond_fn, body_fn, k)
        
        # 2. Compute new intersection with the survivor
        s_final = intersect(v[k_final], q)
        
        # 3. Push new parabola
        k_next = k_final + 1
        v = v.at[k_next].set(q)
        z = z.at[k_next].set(s_final)
        
        # Mark the immediate next boundary as infinite to maintain local sortedness
        z = z.at[k_next + 1].set(INF)

        return (k_next, v, z), None

    v_init = jnp.zeros(n, dtype=jnp.int32)
    z_init = jnp.full(n + 1, INF, dtype=DTYPE).at[0].set(-INF)
    
    # Run the scan
    (k_final, v_final, z_final), _ = jax.lax.scan(
        compute_envelope, (0, v_init, z_init), jnp.arange(1, n)
    )

    z_indices = jnp.arange(n + 1)
    z_final = jnp.where(z_indices > k_final + 1, INF, z_final)
    # -----------------------------

    # Query
    q_coords = jnp.arange(n, dtype=DTYPE)
    k_indices = jnp.searchsorted(z_final, q_coords, side='right') - 1
    
    v_k = v_final[k_indices]
    dist_1d = (q_coords - v_k)**2 + f[v_k]
    
    return dist_1d

@jax.jit
def distance_transform_2d(image):
    """
    Full 2D Euclidean Distance Transform in JAX.
    image: Boolean/Binary array (True for seed points).
    """
    dist = jnp.where(image, DTYPE(0.0), INF)
    # Apply to rows (vmap scales the 1D function across the 2D grid)
    dist = jax.vmap(edt_1d_jax)(dist)
    # Apply to columns (transpose, vmap, transpose)
    dist = jax.vmap(edt_1d_jax)(dist.T).T
    
    return jnp.sqrt(dist)