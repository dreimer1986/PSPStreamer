import base64
import http.client
import json
import os
import subprocess
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import patch
from psp_streamer.server import AppServer, Library

ROOT=Path(__file__).resolve().parents[1]

class AuthTests(unittest.TestCase):
    def test_all_routes_require_password_and_commands_reject_csrf(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ,{"PSP_STREAMER_PASSWORD":"test-ä:secret"}):
            with AppServer(("127.0.0.1",0),Library([Path(temp)])) as server:
                thread=threading.Thread(target=server.serve_forever); thread.start()
                client=http.client.HTTPConnection(*server.server_address,timeout=3)
                try:
                    for path in ("/","/api/health","/api/library","/api/metadata/x","/api/subtitles/x","/api/transcode/x","/api/remote/next"):
                        for auth in ("","Basic !!!","Basic "+base64.b64encode(b"psp:wrong").decode()):
                            client.request("GET",path,headers={"Authorization":auth})
                            response=client.getresponse()
                            self.assertEqual(response.status,401)
                            self.assertIn("Basic",response.getheader("WWW-Authenticate"))
                            self.assertEqual(response.read(),b"")
                    client.request("POST","/api/remote/command",'{"action":"stop"}',{"Content-Type":"application/json"})
                    response=client.getresponse(); self.assertEqual(response.status,401);response.read()
                    self.assertEqual(server.remote_sequence,0)
                    headers={"Authorization":"Basic "+base64.b64encode("psp:test-ä:secret".encode()).decode(),"Content-Type":"application/json"}
                    client.request("GET","/api/health",headers=headers)
                    response=client.getresponse();self.assertEqual(response.status,200)
                    self.assertIn("no-store",response.getheader("Cache-Control"));response.read()
                    client.request("POST","/api/remote/command",'{"action":"pause"}',headers)
                    response=client.getresponse();self.assertEqual(response.status,200)
                    self.assertEqual(json.loads(response.read())["seq"],1)
                    for extra in ({"Content-Type":"text/plain"},{"Origin":"https://evil.invalid"}):
                        client.request("POST","/api/remote/command",'{"action":"stop"}',{**headers,**extra})
                        response=client.getresponse();self.assertEqual(response.status,403);response.read()
                    self.assertEqual(server.remote_sequence,1)
                finally:
                    client.close();server.shutdown();thread.join()

    def test_psp_header_encoding_and_all_request_paths(self):
        with tempfile.TemporaryDirectory() as temp:
            source=Path(temp)/"auth.c";binary=Path(temp)/"auth"
            source.write_text('#include <stdio.h>\n#include <string.h>\n#include "server_auth.h"\n'
                              'int main(int argc,char **argv){if(argc!=2)return 1;snprintf(server_password,sizeof(server_password),"%s",argv[1]);server_auth_update();fputs(server_auth_header,stdout);return 0;}')
            subprocess.run(["cc","-Wall","-Wextra","-Werror","-I",str(ROOT/"psp-client"),str(source),"-o",str(binary)],check=True)
            for password in ("","a","ab","abc","ä:pass","x"*128):
                actual=subprocess.check_output([str(binary),password])
                expected=b"Authorization: Basic "+base64.b64encode(("psp:"+password).encode())+b"\r\n" if password else b""
                self.assertEqual(actual,expected)
        main=(ROOT/"psp-client/main.c").read_text()
        for line in main.splitlines():
            if 'GET ' in line and 'HTTP/1.0' in line:
                self.assertIn('Connection: close\\r\\n%s\\r\\n',line)
        self.assertIn('server_password=%s',main)
