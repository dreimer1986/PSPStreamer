import json
import socket
from pathlib import Path
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.request import Request, urlopen
from unittest.mock import patch
from xml.sax.saxutils import escape

from psp_streamer.dlna import Dlna, http_url, xml, encode, decode
from psp_streamer.catalogue import browse, folder_media
from psp_streamer.server import AppServer, Library


class DlnaTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        owner=self
        class Handler(BaseHTTPRequestHandler):
            def log_message(self,*args):pass
            def do_GET(self):
                if self.path=='/root.xml':
                    body=b'<root xmlns="urn:schemas-upnp-org:device-1-0"><device><friendlyName>Test NAS</friendlyName><UDN>uuid:test</UDN><serviceList><service><serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType><controlURL>/control</controlURL></service></serviceList></device></root>'
                else:
                    owner.ranges.append(self.headers.get('Range'))
                    self.send_response(206);self.send_header('Content-Length','4');self.send_header('Content-Range','bytes 2-5/10');self.end_headers();self.wfile.write(b'2345');return
                self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
            def do_POST(self):
                body=self.rfile.read(int(self.headers['Content-Length']))
                owner.soap.append(body)
                doc=xml(body);oid=doc.findtext('.//ObjectID');metadata=doc.findtext('.//BrowseFlag')=='BrowseMetadata'
                if metadata and oid=='0':
                    nodes='<container id="0" parentID="-1"><dc:title>Root</dc:title></container>'
                else:
                    ids=[oid] if metadata else ['a','b']
                    nodes=''.join(f'<item id="{key}" parentID="0"><dc:title>Track {key}</dc:title><res protocolInfo="http-get:*:audio/mpeg:DLNA.ORG_CI=1">{owner.url}/converted.mp3</res><res protocolInfo="http-get:*:audio/mpeg:DLNA.ORG_CI=0">{owner.url}/original.mp3</res></item>' for key in ids)
                didl='<DIDL-Lite xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/" xmlns:dc="http://purl.org/dc/elements/1.1/">'+nodes+'</DIDL-Lite>'
                response=('<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body><BrowseResponse><Result>'+escape(didl)+'</Result><NumberReturned>2</NumberReturned><TotalMatches>2</TotalMatches></BrowseResponse></s:Body></s:Envelope>').encode()
                self.send_response(200);self.send_header('Content-Length',str(len(response)));self.end_headers();self.wfile.write(response)
        self.http=ThreadingHTTPServer(('127.0.0.1',0),Handler)
        self.worker=threading.Thread(target=self.http.serve_forever);self.worker.start()
        self.addCleanup(self.http.server_close);self.addCleanup(self.worker.join);self.addCleanup(self.http.shutdown)
        self.url=f'http://127.0.0.1:{self.http.server_port}'
        self.ranges=[];self.soap=[]
        self.dlna=Dlna(self.temp.name);self.addCleanup(self.dlna.close)

    def test_description_browse_original_range_next_and_persistence(self):
        result=self.dlna.add(self.url+'/root.xml');self.assertTrue(result['enabled'])
        key=result['devices'][0]['id']
        listing=self.dlna.browse(0,self.dlna.browse(0,':dlna:')['folders'][0]['path'])
        first=listing['videos'][0]['id'];second=listing['videos'][1]['id']
        source=self.dlna.source(first)
        self.assertEqual(self.dlna.bridge.resolve(source.url.split(str(self.dlna.bridge.server_port),1)[1])[0],self.url+'/original.mp3')
        with urlopen(Request(source.url,headers={'Range':'bytes=2-5'})) as response:
            self.assertEqual(response.status,206);self.assertEqual(response.read(),b'2345')
        self.assertEqual(self.ranges,['bytes=2-5'])
        self.assertEqual(self.dlna.next_media(first)['id'],second)
        self.assertEqual(self.dlna.next_media(second),{})
        self.assertEqual(self.dlna.next_media(second,previous=True)['id'],first)
        self.assertEqual(self.dlna.next_media(first,shuffle=True)['id'],second)
        self.dlna.bridge.report_pause(first,True)
        self.assertTrue(self.dlna.bridge.paused(self.url+'/original.mp3'))
        restored=Dlna(self.temp.name);self.assertEqual(restored.public(),result)
        self.assertEqual((Path(self.temp.name)/'dlna.json').stat().st_mode&0o777,0o600)
        self.dlna.configure({'remove':key})
        with self.assertRaises(ValueError):self.dlna.source(first)

    def test_network_and_xml_bounds(self):
        for url in ('file:///etc/passwd','http://user:pass@host','http://host/\nsecret','http://host/#fragment'):
            with self.assertRaises(ValueError):http_url(url)
        with self.assertRaises(ValueError):http_url('http://other/file',self.url)
        with self.assertRaises(ValueError):xml(b'<!DOCTYPE root [<!ENTITY a "b">]><root/>')
        for value in ('a/b & c','ä','0',''):
            self.assertEqual(decode(encode(value)),value)
        with self.assertRaises(ValueError):decode('!')

    def test_ssdp_discovery_is_bounded_and_registers_description(self):
        with patch('psp_streamer.dlna.socket') as network:
            network.timeout=socket.timeout
            sock=network.socket.return_value.__enter__.return_value
            sock.recvfrom.side_effect=[(f'HTTP/1.1 200 OK\r\nLOCATION: {self.url}/root.xml\r\n\r\n'.encode(),('127.0.0.1',1900)),socket.timeout()]
            result=self.dlna.discover()
            self.assertEqual(result['discovered'],1)
            self.assertEqual(result['devices'][0]['name'],'Test NAS')
            self.assertIn(b'M-SEARCH',sock.sendto.call_args.args[0])

    def test_authenticated_api_and_source_navigation(self):
        with patch.dict('os.environ',{'PSP_STREAMER_SETTINGS_DIR':self.temp.name,'PSP_STREAMER_PASSWORD':'',
                'PSP_STREAMER_TLS_CERT':'','PSP_STREAMER_TLS_KEY':''}), AppServer(('127.0.0.1',0),Library([])) as server:
            thread=threading.Thread(target=server.serve_forever);thread.start()
            try:
                base=f'http://127.0.0.1:{server.server_port}'
                with urlopen(Request(base+'/api/dlna/add',data=json.dumps({'url':self.url+'/root.xml'}).encode(),headers={'Content-Type':'application/json'})) as reply:
                    self.assertTrue(json.load(reply)['enabled'])
                self.assertIn(':dlna:',[r['path'] for r in browse(server)['folders']])
                path=browse(server,path=':dlna:')['folders'][0]['path']
                self.assertEqual(len(folder_media(server,0,path)),2)
                server.dlna.configure({'enabled':False})
                self.assertNotIn(':dlna:',[r['path'] for r in browse(server)['folders']])
            finally:server.shutdown();thread.join()
