import http.client
import json
from pathlib import Path
import shutil
import socket
import tempfile
import unittest
from unittest.mock import patch
import run_server


class ServerControlTests(unittest.TestCase):
    def test_foreign_service_is_never_started_or_stopped(self):
        with patch.object(run_server, 'probe', return_value='foreign'), patch.object(run_server.subprocess, 'Popen') as start:
            self.assertEqual(run_server.control('--ensure'), 1)
            self.assertEqual(run_server.control('--stop'), 1)
            start.assert_not_called()

    def test_start_reuse_stop_restart_preserves_data(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)/'server'; folder.mkdir()
            for name in run_server.RUNTIME_FILES:
                shutil.copy2(Path(__file__).parent/name, folder/name)
            with socket.socket() as sock:
                sock.bind(('127.0.0.1',0)); port = sock.getsockname()[1]
            try:
                self.assertEqual(run_server.control('--ensure',folder,port),0)
                self.assertEqual(run_server.control('--ensure',folder,port),0)
                self.assertEqual(len(list((folder/'server-state').glob('autostart-*.stdout.log'))),1)
                token = (folder/'server-state'/'api-token.txt').read_text().strip()
                def request(path,body,auth=token):
                    conn=http.client.HTTPConnection('127.0.0.1',port,timeout=3)
                    conn.request('POST',path,json.dumps(body),{'Authorization':'Bearer '+auth,'Content-Type':'application/json','Idempotency-Key':'control-test'})
                    response=conn.getresponse(); response.read(); code=response.status; conn.close(); return code
                self.assertEqual(request('/api/v1/control/shutdown',{'confirm':True},'wrong'),401)
                self.assertEqual(request('/api/v1/control/shutdown',{}),400)
                self.assertEqual(run_server.probe(folder,port),'ready')
                self.assertEqual(request('/api/v1/profile',{'name':'Тестовый пилот'}),200)
                self.assertEqual(run_server.control('--stop',folder,port),0)
                self.assertEqual(run_server.control('--stop',folder,port),0)
                self.assertEqual(run_server.control('--ensure',folder,port),0)
                self.assertEqual((folder/'server-state'/'api-token.txt').read_text().strip(),token)
                conn=http.client.HTTPConnection('127.0.0.1',port,timeout=3)
                conn.request('GET','/api/v1/state',headers={'Authorization':'Bearer '+token})
                response=conn.getresponse(); state=json.loads(response.read()); conn.close()
                self.assertEqual(state['profile']['name'],'Тестовый пилот')
            finally:
                run_server.control('--stop',folder,port)


if __name__ == '__main__':
    unittest.main()
