import json
import http.client
import subprocess
import tempfile
import threading
import unittest
from pathlib import Path

from psp_streamer.server import AppServer, Library, MediaItem, calibration_command, ffmpeg_command, parse_srt_cues

ROOT = Path(__file__).resolve().parents[1]


class FlvIntegrationTests(unittest.TestCase):
    def test_subtitle_millisecond_units(self):
        cues = parse_srt_cues("1\n00:00:01,125 --> 00:00:02,750\nHello\n", 1000)
        self.assertEqual(cues, [[1125, 2750, "Hello"]])

    def test_delayed_short_audio_spans_video_timeline(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, result = root / "offset.mkv", root / "result.flv"
            subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                            "color=size=480x272:rate=20:duration=12", "-itsoffset", "8",
                            "-f", "lavfi", "-i", "sine=sample_rate=44100:duration=1",
                            "-c:v", "libx264", "-c:a", "pcm_s16le", str(source)], check=True)
            cmd = ffmpeg_command(source, 0, "flv")
            cmd.remove("-re")
            burst = cmd.index("-readrate_initial_burst")
            del cmd[burst:burst + 2]
            result.write_bytes(subprocess.check_output(cmd))
            packets = json.loads(subprocess.check_output([
                "ffprobe", "-v", "error", "-show_packets", "-show_entries",
                "packet=codec_type,pts_time", "-of", "json", str(result)]))["packets"]
            audio = [float(p["pts_time"]) for p in packets if p["codec_type"] == "audio"]
            video = [float(p["pts_time"]) for p in packets if p["codec_type"] == "video"]
            self.assertLess(audio[0], .1)
            self.assertLess(audio[-1], video[-1])
            self.assertLess(max(b - a for a, b in zip(audio, audio[1:])), .028)

    def test_http_flv_endpoint_and_video_without_audio(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "silent.mp4"
            subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                            "color=size=480x272:rate=24000/1001:duration=1", "-c:v", "libx264",
                            str(source)], check=True)
            library = Library([root])
            server = AppServer(("127.0.0.1", 0), library)
            thread = threading.Thread(target=server.serve_forever)
            thread.start()
            connection = http.client.HTTPConnection(*server.server_address, timeout=10)
            try:
                token = library.encode(MediaItem(0, source.name))
                connection.request("GET", f"/api/transcode/{token}?container=flv&start=0.2")
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                self.assertEqual(response.getheader("Content-Type"), "video/x-flv")
                content = response.read()
                self.assertEqual(content[:4], b"FLV\x01")
                self.assertEqual(content[4], 1)  # video present, audio absent
                result = root / "response.flv"
                result.write_bytes(content)
                subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(result),
                                "-f", "null", "-"], check=True)
            finally:
                connection.close()
                server.shutdown()
                thread.join()
                server.server_close()

    def test_container_pts_match_ffprobe_and_payloads_decode(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            parser = work / "parser"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                            str(ROOT / "tests/flv_parser.c"), "-o", str(parser)], check=True)
            for tv, duration in ((False, 2), (True, 2), (True, 300)):
                movie = work / "test.flv"
                command = calibration_command(duration, "flv", tv)
                command.remove("-re")
                movie.write_bytes(subprocess.check_output(command))
                output = subprocess.check_output(
                    [str(parser), str(movie), str(work / "video.h264"), str(work / "audio.mp3")],
                    text=True)
                actual = [(kind, int(pts)) for kind, pts in
                          (line.split() for line in output.splitlines())]
                packets = json.loads(subprocess.check_output([
                    "ffprobe", "-v", "error", "-show_packets", "-show_entries",
                    "packet=codec_type,pts_time", "-of", "json", str(movie)]))["packets"]
                expected = [(p["codec_type"], round(float(p["pts_time"]) * 1000)) for p in packets]
                self.assertEqual(actual, expected)
                self.assertEqual(sum(kind == "video" for kind, _ in actual), duration * 20)
                for name in ("video.h264", "audio.mp3"):
                    subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(work / name),
                                    "-f", "null", "-"], check=True)
                # A real input transcode, including input seek, uses the same
                # mux and retains inter-stream timestamps in one process.
                cmd = ffmpeg_command(movie, 0, "flv", start_seconds=duration - 1.5, tv_output=tv)
                cmd.remove("-re")
                burst = cmd.index("-readrate_initial_burst")
                del cmd[burst:burst + 2]
                seeked = work / "seek.flv"
                seeked.write_bytes(subprocess.check_output(cmd))
                subprocess.run([str(parser), str(seeked), str(work / "video.h264"),
                                str(work / "audio.mp3")], check=True, stdout=subprocess.DEVNULL)


if __name__ == "__main__":
    unittest.main()
