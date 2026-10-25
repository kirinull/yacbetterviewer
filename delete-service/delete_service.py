"""
Pineapple 删除服务
==================

用途：给 Android 客户端提供删除漫画文件的能力。
YACReader 服务端本身没有任何 DELETE 接口，只能由这个服务代劳。

安全边界（四层）：
  1. 只监听指定地址（默认仅局域网可达），不暴露公网
  2. 请求必须带 X-Delete-Token，token 在 config.json 里
  3. 文件路径必须落在库根目录内（realpath 校验，防 ../ 穿越）
  4. 不直接删除：移动到 <库根>/.deleted/ 下保留，
     文件名加时间戳，需要时可手工恢复

用法：
    python delete_service.py            # 前台运行，可见日志
    python delete_service.py --check    # 只做自检，不启动

接口：
    POST /delete
      Header: X-Delete-Token: <token>
      Body:   {"comic_id": 1234}
    返回：
      {"ok": true,  "moved_to": "...", "name": "..."}
      {"ok": false, "error": "..."}
"""

import argparse
import json
import os
import shutil
import sqlite3
import sys
import time
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(HERE, 'config.json')

DEFAULT_CONFIG = {
    # 监听地址：0.0.0.0 表示局域网内所有网卡可达
    "listen_host": "0.0.0.0",
    "listen_port": 8894,
    # 必须与客户端一致
    "token": "change-me-please",
    # 漫画库根目录
    "library_root": "F:/manga",
    # YACReader 的库数据库
    "ydb_path": "F:/manga/.yacreaderlibrary/library.ydb",
    # YACReader 服务地址，删完触发重扫
    "yacreader_base": "http://192.168.10.89:8080",
    "library_id": 1,
    # 移到这个子目录（相对 library_root），保留可恢复
    "trash_dir_name": ".deleted",
    # 触发重扫（false 则只删文件不改索引）
    "trigger_rescan": True,
}


def load_config():
    if not os.path.exists(CONFIG_PATH):
        with open(CONFIG_PATH, 'w', encoding='utf-8') as f:
            json.dump(DEFAULT_CONFIG, f, ensure_ascii=False, indent=2)
        print(f'已生成默认配置: {CONFIG_PATH}')
        print('请先把 token 改成一个只有你知道的字符串，再重新启动。')
        sys.exit(0)
    with open(CONFIG_PATH, encoding='utf-8') as f:
        cfg = dict(DEFAULT_CONFIG)
        cfg.update(json.load(f))
    return cfg


def log(msg):
    print(f'[{time.strftime("%H:%M:%S")}] {msg}', flush=True)


class Handler(BaseHTTPRequestHandler):
    server_version = 'PineappleDelete/1.0'

    def log_message(self, *args):
        pass

    def _json(self, code, obj):
        body = json.dumps(obj, ensure_ascii=False).encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.startswith('/ping'):
            self._json(200, {'ok': True, 'service': 'pineapple-delete'})
        else:
            self._json(404, {'ok': False, 'error': 'not found'})

    def do_POST(self):
        if not self.path.startswith('/delete'):
            self._json(404, {'ok': False, 'error': 'not found'})
            return

        cfg = self.server.cfg

        token = self.headers.get('X-Delete-Token', '')
        if token != cfg['token']:
            log(f'拒绝：token 不匹配 (来自 {self.client_address[0]})')
            self._json(403, {'ok': False, 'error': 'token 无效'})
            return

        length = int(self.headers.get('Content-Length') or 0)
        try:
            payload = json.loads(self.rfile.read(length).decode('utf-8') or '{}')
        except Exception as e:
            self._json(400, {'ok': False, 'error': f'请求体解析失败: {e}'})
            return

        comic_id = payload.get('comic_id')
        if not isinstance(comic_id, int):
            self._json(400, {'ok': False, 'error': 'comic_id 必须是整数'})
            return

        ok, msg, detail = delete_comic(cfg, comic_id)
        if ok:
            log(f'已删除 comic {comic_id}: {detail.get("name")}')
            self._json(200, {'ok': True, **detail})
        else:
            log(f'删除 comic {comic_id} 失败: {msg}')
            self._json(400, {'ok': False, 'error': msg})


def lookup_filename(cfg, comic_id):
    """从 YACReader 库数据库查 comic_id 对应的文件名。"""
    db = cfg['ydb_path']
    if not os.path.exists(db):
        return None, f'库数据库不存在: {db}'
    try:
        conn = sqlite3.connect(f'file:{db}?mode=ro', uri=True)
        row = conn.execute('select fileName from comic where id = ?', (comic_id,)).fetchone()
        conn.close()
    except Exception as e:
        return None, f'读库失败: {e}'
    if not row:
        return None, f'库里没有 comic_id={comic_id}'
    return row[0], None


def delete_comic(cfg, comic_id):
    """
    返回 (ok, error_message, detail_dict)
    """
    name, err = lookup_filename(cfg, comic_id)
    if err:
        return False, err, {}

    root = os.path.realpath(cfg['library_root'])
    src = os.path.realpath(os.path.join(root, name))

    # 核心安全检查：真实路径必须还在库根之内
    if not (src == root or src.startswith(root + os.sep)):
        return False, '路径越界，拒绝操作', {}
    if not os.path.isfile(src):
        return False, f'文件不存在: {os.path.basename(src)}', {}

    trash = os.path.join(root, cfg['trash_dir_name'])
    os.makedirs(trash, exist_ok=True)

    base, ext = os.path.splitext(os.path.basename(src))
    stamp = time.strftime('%Y%m%d-%H%M%S')
    dst = os.path.join(trash, f'{base}.{stamp}{ext}')

    # 同名则加序号
    n = 1
    while os.path.exists(dst):
        dst = os.path.join(trash, f'{base}.{stamp}_{n}{ext}')
        n += 1

    try:
        shutil.move(src, dst)
    except Exception as e:
        return False, f'移动失败: {e}', {}

    detail = {'name': os.path.basename(src), 'moved_to': dst}

    if cfg.get('trigger_rescan'):
        try:
            url = f"{cfg['yacreader_base']}/v2/library/{cfg['library_id']}/update"
            req = urllib.request.Request(url, data=b'', method='POST')
            with urllib.request.urlopen(req, timeout=15) as r:
                log(f'已触发重扫: {r.status}')
            detail['rescanned'] = True
        except Exception as e:
            log(f'触发重扫失败（不影响文件已移走）: {e}')
            detail['rescanned'] = False

    return True, None, detail


def self_check(cfg):
    print('=== 自检 ===')
    print(f"监听      : {cfg['listen_host']}:{cfg['listen_port']}")
    print(f"库根目录  : {cfg['library_root']}  存在={os.path.isdir(cfg['library_root'])}")
    print(f"库数据库  : {cfg['ydb_path']}  存在={os.path.exists(cfg['ydb_path'])}")
    print(f"YACReader : {cfg['yacreader_base']}")
    print(f"token     : {'已设置' if cfg['token'] != 'change-me-please' else '⚠ 还是默认值，必须改'}")
    root = os.path.realpath(cfg['library_root'])
    for probe in ('../../Windows/System32/drivers/etc/hosts', '..\\..\\secret.txt'):
        p = os.path.realpath(os.path.join(root, probe))
        inside = p == root or p.startswith(root + os.sep)
        print(f"穿越测试 {probe!r} -> 在库内={inside} (应为 False)")
    return cfg['token'] != 'change-me-please'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true', help='只自检，不启动')
    args = ap.parse_args()

    cfg = load_config()

    if args.check:
        ok = self_check(cfg)
        sys.exit(0 if ok else 1)

    if cfg['token'] == 'change-me-please':
        print('token 还是默认值，请先改 config.json 再启动。')
        sys.exit(1)

    srv = ThreadingHTTPServer((cfg['listen_host'], cfg['listen_port']), Handler)
    srv.cfg = cfg
    log(f"删除服务已启动: http://{cfg['listen_host']}:{cfg['listen_port']}")
    log(f"库根目录: {cfg['library_root']}")
    log('关闭此窗口即停止服务')
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        log('已停止')


if __name__ == '__main__':
    main()
