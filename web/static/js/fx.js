// Particles and flourishes for the 3D scene: explosion flipbooks, sparks,
// smoke, dust rings, and floating numbers. Everything is a sprite, pooled per
// texture, and ages with Fx.update(dt).

import * as THREE from 'three';

const FX = new URL('../assets/fx/', import.meta.url);

export class Fx {
  constructor(scene) {
    this.scene = scene;
    this.live = [];
    const loader = new THREE.TextureLoader();
    const tex = (name) => {
      const t = loader.load(new URL(name, FX).href);
      t.colorSpace = THREE.SRGBColorSpace;
      return t;
    };
    this.textures = {
      explosion: tex('explosion_atlas.png'),
      spark: [1, 2, 3, 4].map((i) => tex(`spark_0${i}.png`)),
      smoke: [1, 2, 3, 4].map((i) => tex(`smoke_0${i}.png`)),
      flare: tex('flare_01.png'),
      ring: tex('circle_05.png'),
      dirt: tex('dirt_01.png'),
    };
  }

  sprite(texture, {color = 0xffffff, size = 1, additive = false, opacity = 1} = {}) {
    const material = new THREE.SpriteMaterial({
      map: texture, color, transparent: true, opacity, depthWrite: false,
      blending: additive ? THREE.AdditiveBlending : THREE.NormalBlending,
    });
    const sprite = new THREE.Sprite(material);
    sprite.scale.setScalar(size);
    this.scene.add(sprite);
    return sprite;
  }

  // |tick(k, dt)| with k in [0, 1] over |life| seconds.
  spawn(object, life, tick) {
    this.live.push({object, life, age: 0, tick});
  }

  update(dt) {
    for (let i = this.live.length - 1; i >= 0; --i) {
      const p = this.live[i];
      p.age += dt;
      const k = Math.min(1, p.age / p.life);
      p.tick(k, dt);
      if (k >= 1) {
        this.scene.remove(p.object);
        p.object.material.map = null;
        p.object.material.dispose();
        // Sprites share one geometry; a ring has its own.
        if (!p.object.isSprite) p.object.geometry.dispose();
        this.live.splice(i, 1);
      }
    }
  }

  // The 3x3 explosion flipbook, plus sparks and smoke, at |at|.
  explosion(at, scale = 1) {
    const texture = this.textures.explosion.clone();
    texture.repeat.set(1 / 3, 1 / 3);
    texture.needsUpdate = true;
    const boom = this.sprite(texture, {size: 2.6 * scale, additive: true});
    boom.position.copy(at).add(new THREE.Vector3(0, 0.9 * scale, 0));
    boom.material.rotation = Math.random() * Math.PI * 2;
    this.spawn(boom, 0.5, (k) => {
      const frame = Math.min(8, Math.floor(k * 9));
      texture.offset.set((frame % 3) / 3, 1 - (Math.floor(frame / 3) + 1) / 3);
      boom.scale.setScalar(2.6 * scale * (0.8 + k * 0.6));
    });
    this.sparks(at, 14, scale);
    this.smoke(at, 3, scale);
    const flash = this.sprite(this.textures.flare, {color: 0xffd08a, size: 4 * scale, additive: true});
    flash.position.copy(at).add(new THREE.Vector3(0, 0.6, 0));
    this.spawn(flash, 0.25, (k) => { flash.material.opacity = 1 - k; });
  }

  sparks(at, count, scale = 1, color = 0xffc46b) {
    for (let i = 0; i < count; ++i) {
      const spark = this.sprite(this.textures.spark[i % 4], {color, size: 0.35 * scale, additive: true});
      spark.position.copy(at).add(new THREE.Vector3(0, 0.6, 0));
      const v = new THREE.Vector3((Math.random() - 0.5) * 9, 3 + Math.random() * 6, (Math.random() - 0.5) * 9).multiplyScalar(scale);
      this.spawn(spark, 0.45 + Math.random() * 0.35, (k, dt) => {
        v.y -= 18 * dt;
        spark.position.addScaledVector(v, dt);
        spark.material.opacity = 1 - k;
      });
    }
  }

  smoke(at, count, scale = 1, color = 0x8a8278) {
    for (let i = 0; i < count; ++i) {
      const puff = this.sprite(this.textures.smoke[i % 4], {color, size: 1.2 * scale, opacity: 0.5});
      puff.position.copy(at).add(new THREE.Vector3((Math.random() - 0.5) * 0.8, 0.5, (Math.random() - 0.5) * 0.8));
      puff.material.rotation = Math.random() * 6;
      this.spawn(puff, 0.9 + Math.random() * 0.5, (k, dt) => {
        puff.position.y += 1.2 * dt;
        puff.scale.setScalar(scale * (1.2 + k * 1.8));
        puff.material.opacity = 0.5 * (1 - k);
      });
    }
  }

  // A flat ring spreading from a landing, in |color|.
  dust(at, color = 0xd8c9a6, scale = 1) {
    const material = new THREE.MeshBasicMaterial({
      map: this.textures.ring, color, transparent: true, depthWrite: false, opacity: 0.8,
    });
    const ring = new THREE.Mesh(new THREE.PlaneGeometry(1, 1), material);
    ring.rotation.x = -Math.PI / 2;
    ring.position.copy(at).add(new THREE.Vector3(0, 0.05, 0));
    this.scene.add(ring);
    this.spawn(ring, 0.45, (k) => {
      ring.scale.setScalar(scale * (0.5 + k * 2.6));
      material.opacity = 0.8 * (1 - k);
    });
  }

  // Confetti-ish burst in a team's colour, for a conquest.
  burst(at, color) {
    this.sparks(at, 22, 1.1, color);
    const flare = this.sprite(this.textures.flare, {color, size: 6, additive: true});
    flare.position.copy(at).add(new THREE.Vector3(0, 1.5, 0));
    this.spawn(flare, 0.6, (k) => {
      flare.material.opacity = 1 - k;
      flare.scale.setScalar(6 + k * 4);
    });
  }

  // "+3" / "-2" rising from |at|.
  text(at, label, color) {
    const canvas = document.createElement('canvas');
    canvas.width = 256;
    canvas.height = 128;
    const g = canvas.getContext('2d');
    g.font = 'bold 84px Palatino, Georgia, serif';
    g.textAlign = 'center';
    g.textBaseline = 'middle';
    g.lineWidth = 12;
    g.strokeStyle = 'rgba(0,0,0,0.75)';
    g.strokeText(label, 128, 64);
    g.fillStyle = color;
    g.fillText(label, 128, 64);
    const texture = new THREE.CanvasTexture(canvas);
    texture.colorSpace = THREE.SRGBColorSpace;
    const sprite = this.sprite(texture, {size: 1});
    sprite.material.depthTest = false;
    sprite.renderOrder = 20;
    sprite.scale.set(2.2, 1.1, 1);
    sprite.position.copy(at).add(new THREE.Vector3(0, 2.6, 0));
    const y = sprite.position.y;
    this.spawn(sprite, 1.1, (k) => {
      sprite.position.y = y + k * 1.6;
      sprite.material.opacity = k < 0.7 ? 1 : 1 - (k - 0.7) / 0.3;
      if (k >= 1) texture.dispose();
    });
  }
}
