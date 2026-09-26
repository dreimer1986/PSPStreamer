import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class PlaybackRecoveryTests(unittest.TestCase):
    def test_playback_reuses_known_ip_without_entering_dns(self):
        source = (ROOT / "psp-client/main.c").read_text()
        resolver = source[source.index("static int resolve_server_address("):source.index("static int prepare_server(")]
        harness = r'''
#include <assert.h>
struct in_addr {unsigned int s_addr;};
static struct in_addr cached_server_address={123};
static int have_cached_server_address=1,queries;
static const char *server_host="test";
static int inet_aton(const char *host,struct in_addr *a){(void)host;(void)a;return 0;}
static int sceNetResolverCreate(int *r,void *b,int n){(void)b;(void)n;*r=1;return 0;}
static void sceNetResolverInit(void){}
static int sceNetResolverStartNtoA(int r,const char *h,struct in_addr *a,int t,int n){(void)r;(void)h;(void)a;(void)t;(void)n;queries++;return -1;}
static void sceNetResolverDelete(int r){(void)r;}
''' + resolver + r'''
int main(void){struct in_addr a={0};assert(resolve_server_address(&a)==0 && a.s_addr==123 && !queries);
have_cached_server_address=0;assert(resolve_server_address(&a)<0 && queries==1);}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "dns.c"
            path.write_text(harness)
            binary = str(Path(directory) / "dns")
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(path), "-o", binary], check=True)
            subprocess.run([binary], check=True, timeout=3)

    def test_preparation_http_budgets(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = str(Path(directory) / "preparation")
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", "-I", str(ROOT / "psp-client"),
                            str(ROOT / "tests/preparation_http_harness.c"), "-o", binary], check=True)
            subprocess.run([binary], check=True, timeout=5)

    def test_diagnostic_retention(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = str(Path(directory) / "history")
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined",
                            "-I", str(ROOT / "tests/psp_history_stubs"),
                            "-I", str(ROOT / "psp-client"),
                            str(ROOT / "tests/diagnostic_history_harness.c"),
                            "-o", binary], check=True)
            subprocess.run([binary], check=True, timeout=5)

    def test_transport_and_recovery_state(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = str(Path(directory) / "recovery")
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", "-I", str(ROOT / "psp-client"),
                            str(ROOT / "tests/playback_recovery_harness.c"),
                            "-o", binary], check=True)
            subprocess.run([binary], check=True, timeout=5)

    def test_recovery_is_scoped_and_joins_before_retry(self):
        source = (ROOT / "psp-client/main.c").read_text()
        audio = source[source.index("static int audio_thread("):source.index('#include "music_ui.h"')]
        self.assertNotIn("connection_recv(", audio)
        self.assertIn("180000000ULL", audio)  # Keep cold-start preparation budget.
        self.assertIn("audio_state=-28", audio)
        self.assertNotIn("gethostbyname(server_host)", source)
        wrappers = source[source.index("static int comfort_play_audio("):source.index("/* This is intentionally a narrow parser")]
        self.assertIn("offline_active || radio_is_live(id) || !music_network_failed", wrappers)
        self.assertIn("if(!decoder && (result!=-1320 || !timed_network_failed))break;", wrappers)
        self.assertIn("if(offline_active || seek_requested || video_file_direction)break;", wrappers)
        self.assertIn("stream_start_seconds=playback_recovery_position(playback_position_ms)", wrappers)


if __name__ == "__main__":
    unittest.main()
