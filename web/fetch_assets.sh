#!/usr/bin/env bash
# Downloads the web app's third-party files into web/static/ (git ignores
# them): three.js (MIT) and the CC0 models, sounds and particle sprites listed
# in web/CREDITS.md. Archives are cached in web/.cache/, so a rerun is local.
#
#   bash web/fetch_assets.sh
set -euo pipefail

WEB=$(cd "$(dirname "$0")" && pwd)
CACHE=$WEB/.cache/zips
STATIC=$WEB/static
mkdir -p "$CACHE"

fetch() {  # <url> <cache name>
  [[ -s $CACHE/$2 ]] || curl -sSfL --retry 3 -o "$CACHE/$2" "$1"
}

fetch https://registry.npmjs.org/three/-/three-0.170.0.tgz three.tgz
fetch https://codeload.github.com/KayKit-Game-Assets/KayKit-Character-Pack-Adventures-1.0/zip/refs/heads/main adventurers.zip
fetch https://raw.githubusercontent.com/KayKit-Game-Assets/KayKit-Character-Pack-Skeletons-1.0/main/addons/kaykit_character_pack_skeletons/Characters/gltf/Skeleton_Warrior.glb Skeleton_Warrior.glb
fetch https://codeload.github.com/KayKit-Game-Assets/KayKit-Character-Pack-Skeletons-1.0/zip/refs/heads/main skeletons.zip
fetch https://codeload.github.com/KayKit-Game-Assets/KayKit-Medieval-Hexagon-Pack-1.0/zip/refs/heads/main hexagon.zip
fetch https://kenney.nl/media/pages/assets/smoke-particles/23249a0d35-1677695171/kenney_smoke-particles.zip smoke.zip
fetch https://kenney.nl/media/pages/assets/particle-pack/f8fe0f8cb8-1677578741/kenney_particle-pack.zip particles.zip
fetch https://kenney.nl/media/pages/assets/casino-audio/2472606a04-1721639069/kenney_casino-audio.zip casino.zip
fetch https://kenney.nl/media/pages/assets/ui-audio/490d233f68-1677590494/kenney_ui-audio.zip ui.zip
fetch https://kenney.nl/media/pages/assets/impact-sounds/87b4ddecda-1677589768/kenney_impact-sounds.zip impact.zip
fetch https://kenney.nl/media/pages/assets/rpg-audio/8e99002d76-1677590336/kenney_rpg-audio.zip rpg.zip
fetch https://kenney.nl/media/pages/assets/music-jingles/f37e530b9e-1677590399/kenney_music-jingles.zip jingles.zip
fetch https://opengameart.org/sites/default/files/sword_clash_-_starninjas_0.zip swordclash.zip
fetch https://opengameart.org/sites/default/files/sword_-_starninjas_1.zip swords.zip
fetch https://opengameart.org/sites/default/files/25-CC0-bang-sfx.zip bangs.zip
fetch https://opengameart.org/sites/default/files/horde_war_drums_by_william_hector.wav drums.wav

rm -rf "$STATIC/vendor" "$STATIC/assets"
mkdir -p "$STATIC/vendor" "$STATIC/assets"/{models,audio,fx}

# three.js: the core and the addons the pages import, keeping the addons'
# relative imports intact.
tar -xzf "$CACHE/three.tgz" -C "$STATIC/vendor" \
  package/build/three.module.js package/LICENSE \
  package/examples/jsm/loaders/GLTFLoader.js \
  package/examples/jsm/loaders/SVGLoader.js \
  package/examples/jsm/controls/OrbitControls.js \
  package/examples/jsm/utils/SkeletonUtils.js \
  package/examples/jsm/utils/BufferGeometryUtils.js \
  package/examples/jsm/postprocessing \
  package/examples/jsm/shaders
mv "$STATIC/vendor/package" "$STATIC/vendor/three"

M=$STATIC/assets/models
unzip -p "$CACHE/adventurers.zip" '*/Characters/gltf/Knight.glb' > "$M/knight.glb"
unzip -p "$CACHE/adventurers.zip" '*/Characters/gltf/Barbarian.glb' > "$M/barbarian.glb"
cp "$CACHE/Skeleton_Warrior.glb" "$M/skeleton.glb"
unzip -qjo "$CACHE/skeletons.zip" \
  '*/Assets/gltf/Skeleton_Blade.*' '*/Assets/gltf/Skeleton_Shield_Large_A.*' \
  '*/Assets/gltf/skeleton_texture.png' -d "$M"
# The hexagon pack's pieces share one texture, hexagons_medieval.png.
HEX='KayKit-Medieval-Hexagon-Pack-1.0-main/addons/kaykit_medieval_hexagon_pack/Assets/gltf'
for piece in buildings/red/building_castle_red buildings/green/building_castle_green \
  buildings/yellow/building_castle_yellow \
  buildings/red/building_tower_A_red buildings/green/building_tower_A_green \
  buildings/yellow/building_tower_A_yellow buildings/neutral/projectile_catapult \
  decoration/props/flag_red decoration/props/flag_green decoration/props/flag_yellow \
  decoration/nature/trees_A_medium decoration/nature/trees_B_small; do
  unzip -qjo "$CACHE/hexagon.zip" "$HEX/$piece.gltf" "$HEX/$piece.bin" -d "$M"
done
unzip -qjo "$CACHE/hexagon.zip" "$HEX/buildings/red/hexagons_medieval.png" -d "$M"

A=$STATIC/assets/audio
unzip -qjo "$CACHE/casino.zip" '*dice-shake-1.ogg' '*dice-throw-[123].ogg' -d "$A"
unzip -qjo "$CACHE/ui.zip" '*click1.ogg' '*click3.ogg' '*rollover2.ogg' '*switch7.ogg' -d "$A"
unzip -qjo "$CACHE/impact.zip" '*impactMetal_heavy_00[0-2].ogg' \
  '*impactPunch_heavy_00[0-2].ogg' '*footstep_grass_00[0-3].ogg' -d "$A"
unzip -qjo "$CACHE/rpg.zip" '*drawKnife1.ogg' '*chop.ogg' -d "$A"
unzip -qjo "$CACHE/jingles.zip" '*jingles_HIT00.ogg' '*jingles_HIT05.ogg' \
  '*jingles_PIZZI01.ogg' '*jingles_STEEL00.ogg' -d "$A"
unzip -qjo "$CACHE/swordclash.zip" '*sword_clash.[1-6].ogg' -d "$A"
unzip -qjo "$CACHE/swords.zip" '*sword.[1-4].ogg' -d "$A"
unzip -qjo "$CACHE/bangs.zip" '*cannon_0[1-3].ogg' '*bang_0[1-4].ogg' -d "$A"
cp "$CACHE/drums.wav" "$A/drums.wav"
# Dots are not for file names that end up in URLs.
for f in "$A"/sword_clash.*.ogg "$A"/sword.*.ogg; do
  mv "$f" "${f%.*.ogg}_${f##*[a-z].}"
done
# An .mp3 beside each .ogg, for browsers without Vorbis (iOS Safari). ffmpeg
# comes from imageio-ffmpeg's static build, so the host needs none.
FFMPEG=$(uv run -q --with imageio-ffmpeg python -c \
  'import imageio_ffmpeg; print(imageio_ffmpeg.get_ffmpeg_exe())')
for f in "$A"/*.ogg; do
  "$FFMPEG" -loglevel error -y -i "$f" -codec:a libmp3lame -q:a 5 "${f%.ogg}.mp3"
done

F=$STATIC/assets/fx
unzip -qjo "$CACHE/particles.zip" 'PNG (Transparent)/spark_0[1-4].png' \
  'PNG (Transparent)/smoke_0[1-4].png' 'PNG (Transparent)/flare_01.png' \
  'PNG (Transparent)/circle_05.png' 'PNG (Transparent)/dirt_01.png' \
  'PNG (Transparent)/scorch_01.png' -d "$F"
# The 9-frame explosion as one 3x3 flipbook, so a burst is one sprite.
unzip -qjo "$CACHE/smoke.zip" 'PNG/Explosion/explosion0[0-8].png' -d "$CACHE/explosion"
python3 - "$CACHE/explosion" "$F/explosion_atlas.png" <<'EOF'
import sys, pathlib
from PIL import Image
frames = sorted(pathlib.Path(sys.argv[1]).glob("explosion0*.png"))
tiles = [Image.open(f).convert("RGBA").resize((256, 256)) for f in frames]
atlas = Image.new("RGBA", (768, 768))
for i, tile in enumerate(tiles):
    atlas.paste(tile, ((i % 3) * 256, (i // 3) * 256))
atlas.save(sys.argv[2])
EOF

cp "$WEB/../problem/risk_map.svg" "$STATIC/assets/risk_map.svg"
echo "assets in $STATIC: $(du -sh "$STATIC/assets" | cut -f1), three.js in vendor/"
