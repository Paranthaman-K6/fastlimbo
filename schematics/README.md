# schematics/ — drop-in spawn platforms for limbo-c++
#
# What goes here: Sponge `.schem` v2 files (gzip NBT). The server pastes the
# configured file at spawn (centered on block 8,·,8, top at y=320).
#
# Default: spawn.schem (8x3x8 stone floor + glass corner pillars).
# Enabled via config: schematic_path=schematics/spawn.schem
#
# Use your own (WorldEdit):
#   1. //pos1, //pos2 (or //wand), //schem save <name>
#   2. cp ~/.minecraft/.../<name>.schem ./schematics/
#   3. schematic_path=schematics/<name>.schem  + restart
#
# Limits: v2 only, ≤256 per side, ≤1M blocks. Renders on 1.18+ clients
# (older versions see void). Bad file = void fallback + [world] log line.
