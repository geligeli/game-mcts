import jax
import jax.numpy as jnp
import ascii_rendering.edt as edt

@jax.jit
def chamfer_distance(mask1, mask2):
    """
    Chamfer distance for large masks (1k x 1k) using Distance Transforms.
    """
    # Distance from every point to nearest point in mask 1
    dt1 = edt.distance_transform_2d(mask1)
    # Distance from every point to nearest point in mask 2
    dt2 = edt.distance_transform_2d(mask2)

    # Average distance of points in M1 to M2
    term1 = jnp.sum(jnp.where(mask1, dt2, 0.0)) / (jnp.sum(mask1.astype(jnp.float32)) + 1e-6)

    # Average distance of points in M2 to M1
    term2 = jnp.sum(jnp.where(mask2, dt1, 0.0)) / (jnp.sum(mask2.astype(jnp.float32)) + 1e-6)
    
    return term1 + term2


@jax.jit
def chamfer_distance2(dt1, mask1, mask2):
    """
    Chamfer distance for large masks (1k x 1k) using Distance Transforms.
    """
    # Distance from every point to nearest point in mask 2
    dt2 = edt.distance_transform_2d(mask2)
    # Average distance of points in M1 to M2
    term1 = jnp.sum(jnp.where(mask1, dt2, 0.0)) / (jnp.sum(mask1.astype(jnp.float32)) + 1e-6)
    # Average distance of points in M2 to M1
    term2 = jnp.sum(jnp.where(mask2, dt1, 0.0)) / (jnp.sum(mask2.astype(jnp.float32)) + 1e-6)
    
    return term1 + term2

@jax.jit
def chamfer_distance3(dt1, mask1, dt2, mask2):
    """
    Chamfer distance for large masks (1k x 1k) using Distance Transforms.
    """
    # Average distance of points in M1 to M2
    term1 = jnp.sum(jnp.where(mask1, dt2, 0.0)) / (jnp.sum(mask1.astype(jnp.float32)) + 1e-6)
    # Average distance of points in M2 to M1
    term2 = jnp.sum(jnp.where(mask2, dt1, 0.0)) / (jnp.sum(mask2.astype(jnp.float32)) + 1e-6)
    
    return term1 + term2