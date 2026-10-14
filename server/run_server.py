"""Find an installed Python on Windows and run the local service in this folder."""
import os
import http.client
from pathlib import Path
import subprocess
import sys
import webbrowser
import time
import json
import socket

root = Path(__file__).resolve().parent
RUNTIME_FILES = ('server.py','acquisition.py','catalog.py','game_catalog.py','operations.py','log_observer.py','dashboard.html','tools.html','testing.html')
_spawned = {}

def probe(folder=root, port=18870):
    """ready/foreign/down: never start or stop an unpaired service on this port."""
    # Windows may drop connection attempts to a closed port instead of returning RST.
    # An exclusive bind distinguishes a genuinely free port from that timeout.
    with socket.socket() as availability:
        if hasattr(socket, 'SO_EXCLUSIVEADDRUSE'):
            availability.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        try:
            availability.bind(('127.0.0.1',port))
            return 'down'
        except OSError:
            pass
    try:
        token = (folder/'server-state'/'api-token.txt').read_text(encoding='ascii').strip()
    except OSError:
        token = ''
    connection = http.client.HTTPConnection('127.0.0.1', port, timeout=2)
    try:
        connection.request('GET', '/api/v1/state', headers={'Authorization':'Bearer '+token})
        reply = connection.getresponse()
        payload = reply.read()
        if reply.status == 200 and json.loads(payload).get('schema_version') == 2:
            return 'ready'
        return 'foreign'
    except ConnectionRefusedError:
        return 'down'
    except (OSError, http.client.HTTPException, ValueError):
        return 'foreign'  # a timeout or unexpected reply is not evidence of a free port
    finally:
        connection.close()

def control(action, folder=root, port=18870, *, log_paths=None):
    state = probe(folder, port)
    if state == 'foreign':
        print('Порт занят другим или недоступным сервисом. Его процессы не изменены.')
        return 1
    if action == '--ensure' and state == 'ready':
        print('Локальный сервер уже работает.')
        return 0
    if action == '--stop' and state == 'down':
        print('Локальный сервер уже остановлен.')
        return 0
    if action == '--stop':
        token = (folder/'server-state'/'api-token.txt').read_text(encoding='ascii').strip()
        connection = http.client.HTTPConnection('127.0.0.1', port, timeout=2)
        try:
            connection.request('POST', '/api/v1/control/shutdown', json.dumps({'confirm':True}),
                {'Authorization':'Bearer '+token,'Content-Type':'application/json'})
            reply = connection.getresponse(); reply.read()
            if reply.status != 200:
                print('Сервер не поддерживает штатную остановку или отклонил запрос.')
                return 1
        finally:
            connection.close()
    else:
        state_dir = folder/'server-state'; state_dir.mkdir(exist_ok=True)
        stamp = time.time_ns()
        with (state_dir/f'autostart-{stamp}.stdout.log').open('ab') as out, (state_dir/f'autostart-{stamp}.stderr.log').open('ab') as err:
            command=[sys.executable,'-X','utf8',str(folder/'server.py'),'--port',str(port)]
            if log_paths is not None:
                command += ['--game-log',str(log_paths[0]),'--mod-log',str(log_paths[1])]
            _spawned[(str(folder),port)] = subprocess.Popen(command,
                cwd=folder, stdin=subprocess.DEVNULL, stdout=out, stderr=err,
                creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    expected = 'down' if action == '--stop' else 'ready'
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if probe(folder,port) == expected:
            if action == '--stop':
                child = _spawned.pop((str(folder),port),None)
                if child is not None:
                    child.wait(timeout=3)
            print('Локальный сервер остановлен. Данные сохранены.' if action == '--stop' else f'Локальный сервер запущен: http://127.0.0.1:{port}/')
            return 0
        time.sleep(.1)
    print('Сервер не подтвердил завершение операции. Проверьте журналы в server-state.')
    return 1

if __name__ == '__main__':
    if len(sys.argv) == 2 and sys.argv[1] in ('--ensure','--stop'):
        raise SystemExit(control(sys.argv[1]))
    if '--open' in sys.argv and len(sys.argv) == 2:
        try:
            token = (root/'server-state'/'api-token.txt').read_text(encoding='ascii').strip()
            connection = http.client.HTTPConnection('127.0.0.1', 18870, timeout=2)
            connection.request('GET', '/api/v1/state', headers={'Authorization':'Bearer '+token})
            reply = connection.getresponse()
            matched = reply.status == 200
            reply.read()
            connection.close()
            if matched:
                webbrowser.open('http://127.0.0.1:18870')
                print('Сервер уже запущен. Открыта страница управления.')
                raise SystemExit(0)
        except (OSError, http.client.HTTPException):
            pass
    raise SystemExit(subprocess.call([sys.executable, str(root / 'server.py'), *sys.argv[1:]], cwd=root))
