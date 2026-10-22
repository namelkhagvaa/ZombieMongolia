"""Вэб хувилбарыг дотоод сүлжээнд (утсаар туршихад) үйлчилнэ; /api/ хүсэлтийг жинхэнэ данс, төлбөрийн сервер рүү дамжуулна.

    python tools/serve_web.py [port] [api]
    утаснаас: http://<компьютерийн IP>:<port>/?api=http://<компьютерийн IP>:<port>/api/

Тоглоомын API сервер (zombie-api.apps.mn) зөвхөн play-zombie.apps.mn хаягаас ирсэн хөтчийн хүсэлтийг (CORS)
зөвшөөрдөг. Энэ сервер API-г нэг хаягаас дамжуулдаг тул туршилтад нэвтрэх, төлбөр хэвийн ажиллана.
"""
import http.server, os, socketserver, sys, urllib.error, urllib.request

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
API = sys.argv[2] if len(sys.argv) > 2 else 'https://zombie-api.apps.mn/api/'
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build_web', 'web')


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def proxy(self):
        url = API + self.path[len('/api/'):]
        length = int(self.headers.get('Content-Length') or 0)
        body = self.rfile.read(length) if length else None
        headers = {k: v for k, v in self.headers.items() if k.lower() in ('content-type', 'authorization', 'accept')}
        headers['User-Agent'] = 'ZombieMongolia-devproxy/1.0'
        request = urllib.request.Request(url, data=body, method=self.command, headers=headers)
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                status, data, kind = response.status, response.read(), response.headers.get('Content-Type', 'application/json')
        except urllib.error.HTTPError as error:
            status, data, kind = error.code, error.read(), error.headers.get('Content-Type', 'application/json')
        except Exception as error:  # сервер хүрэхгүй
            status, data, kind = 502, ('{"message": "proxy: %s"}' % error).encode(), 'application/json'
        self.send_response(status)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path.startswith('/api/'):
            return self.proxy()
        return super().do_GET()

    def do_POST(self):
        if self.path.startswith('/api/'):
            return self.proxy()
        self.send_error(405)

    do_PUT = do_PATCH = do_DELETE = do_POST

    def end_headers(self):
        # Шинэ build-ийг утас кэшээс хуучныг аваачихгүй
        if not self.path.startswith('/api/'):
            self.send_header('Cache-Control', 'no-cache')
        super().end_headers()


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


if __name__ == '__main__':
    print('serving %s on port %d, /api/ -> %s' % (os.path.abspath(ROOT), PORT, API), flush=True)
    Server(('0.0.0.0', PORT), Handler).serve_forever()
