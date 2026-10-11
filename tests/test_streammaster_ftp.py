import ftplib
import io
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[1]

class FTPTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory()
        cls.exe=Path(cls.build.name)/'ftp-host'
        subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                        '-DSM_TCP_SERVER_HOST=1','-I',str(ROOT/'streammaster'),
                        str(ROOT/'tests/ftp_host.c'),str(ROOT/'streammaster/main/tcp_server.c'),
                        '-o',str(cls.exe)],check=True)

    @classmethod
    def tearDownClass(cls):cls.build.cleanup()

    def setUp(self):
        self.root=tempfile.TemporaryDirectory();self.path=Path(self.root.name)
        cfg=self.path/'SEPLUGINS/PSPStreamFTP';cfg.mkdir(parents=True)
        with socket.socket() as sock:
            sock.bind(('127.0.0.1',0));self.port=sock.getsockname()[1]
        (cfg/'PSPStreamFTP.ini').write_text(f'enabled=1\nuser=test\npassword=secret\nport={self.port}\nidle_seconds=30\n')
        self.process=subprocess.Popen([str(self.exe),self.root.name],stderr=subprocess.PIPE)
        self.ftp=ftplib.FTP();self.ftp.encoding='utf-8'
        for _ in range(100):
            try:self.ftp.connect('127.0.0.1',self.port,timeout=4);break
            except OSError:time.sleep(.03)
        else:self.fail('FTP server did not start')

    def tearDown(self):
        self.ftp.close();self.process.terminate()
        _,err=self.process.communicate(timeout=5)
        self.root.cleanup()
        self.assertNotIn(b'runtime error:',err)

    def test_authentication_and_jail(self):
        with self.assertRaises(ftplib.error_perm):self.ftp.nlst()
        with self.assertRaises(ftplib.error_perm):self.ftp.login('test','wrong')
        self.ftp.login('test','secret')
        self.ftp.cwd('../../../../');self.assertEqual(self.ftp.pwd(),'/')
        for path in ('flash0:/','ms0:/','/a/../../flash1:/','..\\flash0:'):
            with self.assertRaises(ftplib.error_perm):self.ftp.cwd(path)
        with self.assertRaises(ftplib.error_perm):self.ftp.delete('/')
        with self.assertRaises(ftplib.error_perm):self.ftp.sendcmd('PORT 127,0,0,1,1,1')

    def test_roundtrip_overwrite_resume_rename(self):
        self.ftp.login('test','secret')
        payload=bytes(range(256))*350
        self.ftp.mkd('ÄÖÜ Test');self.ftp.cwd('ÄÖÜ Test')
        self.ftp.storbinary('STOR Datei.bin',io.BytesIO(b'old'))
        self.ftp.storbinary('STOR Datei.bin',io.BytesIO(payload))
        self.assertEqual((self.path/'ÄÖÜ Test/Datei.bin').read_bytes(),payload)
        result=io.BytesIO();self.ftp.retrbinary('RETR Datei.bin',result.write)
        self.assertEqual(result.getvalue(),payload)
        result=io.BytesIO();self.ftp.retrbinary('RETR Datei.bin',result.write,rest=123)
        self.assertEqual(result.getvalue(),payload[123:])
        self.assertEqual(self.ftp.size('Datei.bin'),len(payload))
        self.assertEqual(self.ftp.nlst(),['Datei.bin'])
        self.assertEqual(dict(self.ftp.mlsd())['Datei.bin']['type'],'file')
        self.ftp.rename('Datei.bin','new.bin');self.ftp.delete('new.bin')
        self.assertEqual(list((self.path/'ÄÖÜ Test').iterdir()),[])
        self.ftp.cwd('/');self.ftp.rmd('ÄÖÜ Test')

    def test_epsv_and_abort_preserves_old_file(self):
        self.ftp.login('test','secret')
        target=self.path/'existing';target.write_bytes(b'keep me')
        reply=self.ftp.sendcmd('EPSV');port=int(reply.split('|||')[1].split('|')[0])
        data=socket.create_connection(('127.0.0.1',port),timeout=3)
        self.ftp.putcmd('STOR existing');self.assertTrue(self.ftp.getresp().startswith('150'))
        data.sendall(b'incomplete')
        self.ftp.putcmd('ABOR')
        with self.assertRaises(ftplib.error_temp):self.ftp.getresp()
        self.assertTrue(self.ftp.getresp().startswith('226'));data.close()
        self.assertEqual(target.read_bytes(),b'keep me')
        self.assertEqual(list(self.path.glob('existing.smftp-*')),[])

    def test_upload_reset_preserves_old_file(self):
        self.ftp.login('test','secret');target=self.path/'existing';target.write_bytes(b'keep me')
        data=self.ftp.transfercmd('STOR existing');data.sendall(b'partial')
        data.setsockopt(socket.SOL_SOCKET,socket.SO_LINGER,struct.pack('ii',1,0));data.close()
        with self.assertRaises(ftplib.error_temp):self.ftp.voidresp()
        self.assertEqual(target.read_bytes(),b'keep me')

if __name__=='__main__':unittest.main()
