"""Focused flight rules and score persistence; no preset collection tests."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class CaveGameTests(unittest.TestCase):
    def test_spawns_in_generated_tunnel(self):
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/"spawn.c";binary=Path(tmp)/"spawn"
            source.write_text('''#include "cave_visual.c"
#include <assert.h>
#include <stdio.h>
int main(void) {
    unsigned seeds[]={1,0x45319a7,123456};int total=0;
    for(int seed=0;seed<3;seed++) {
        CaveScene *s=cave_create_seed(seeds[seed]);assert(s);
        unsigned char bands[12]={0};int spawned=0,attempts=0,drones=0,turrets=0,shields=0;
        for(int f=0;f<240;f++) {
            cave_prepare(s,bands,0,1000000ULL+f*50000ULL);
            if(f>20 && f%10==0) {
                s->combat.health=0;cave_combat_spawn(s);attempts++;
                if(s->combat.health>0){spawned++;if(s->combat.drone)drones++;else turrets++;}
                s->combat.shield_active=0;s->combat.shield_timer=0;s->combat.shield_retry=0;
                float player[3]={0,0,s->motion.travel+2};cave_shield_step(s,.01f,player);
                shields+=s->combat.shield_active;
            }
        }
        printf("Generated tunnel seed %u: %d/%d spawn sites usable\\n",seeds[seed],spawned,attempts);
        printf("Drones=%d turrets=%d shields=%d\\n",drones,turrets,shields);
        assert(drones>0 && turrets>0 && shields>0);total+=spawned;cave_destroy(s);
    }
    assert(total>0);
}''')
            subprocess.run(["cc","-std=c11","-O2","-Wall","-Wextra","-Werror",
                "-fsanitize=undefined","-fno-sanitize-recover=all","-I",str(ROOT/"psp-client"),
                str(source),str(ROOT/"psp-client/cave_paths.c"),str(ROOT/"psp-client/milkdrop_signal.c"),
                "-lm","-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=10)

    def test_combat(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "combat"
            subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(ROOT / "psp-client"),
                            str(ROOT / "tests/cave_combat_harness.c"), "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=5)

    def test_rules_and_scores(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "flight"
            subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(ROOT / "psp-client"),
                            str(ROOT / "tests/cave_game_harness.c"), "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary), str(Path(tmp)/"a.dat"), str(Path(tmp)/"b.dat")], check=True, timeout=5)
