#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""百度短语音识别 API 自测:token 获取 + WAV 转写(验证 Key 和接口格式)
用法:先导出环境变量 BAIDU_API_KEY / BAIDU_SECRET_KEY,再运行本脚本。"""
import base64
import os
import subprocess
import sys
import tempfile

import requests

API_KEY = os.environ.get("BAIDU_API_KEY", "")
SECRET_KEY = os.environ.get("BAIDU_SECRET_KEY", "")


def get_token():
    r = requests.get(
        "https://openapi.baidu.com/oauth/2.0/token",
        params={"grant_type": "client_credentials", "client_id": API_KEY, "client_secret": SECRET_KEY},
        timeout=15,
    )
    print("token HTTP", r.status_code)
    data = r.json()
    if "access_token" not in data:
        print("  body:", data)
        return None
    print("  token ok:", data["access_token"][:12] + "...")
    return data["access_token"]


def make_test_wav(path):
    ps = (
        "Add-Type -AssemblyName System.Speech; "
        "$s = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
        "$s.SetOutputToWaveFile('" + path.replace("\\", "/") + "'); "
        "$s.Speak('现在几点了,今天天气怎么样'); "
        "$s.Dispose()"
    )
    subprocess.run(["powershell.exe", "-NoProfile", "-Command", ps], check=True, capture_output=True)


def asr(token, wav_path):
    with open(wav_path, "rb") as f:
        audio = f.read()
    body = {
        "format": "wav",
        "rate": 16000,
        "dev_pid": 1537,  # 1537 标准普通话(15372 加强版需单独开通权限)
        "channel": 1,
        "cuid": "wio-terminal-test",
        "token": token,
        "len": len(audio),
        "speech": base64.b64encode(audio).decode(),
    }
    r = requests.post("https://vop.baidu.com/server_api", json=body, timeout=30)
    print("asr HTTP", r.status_code)
    data = r.json()
    print("  err_no:", data.get("err_no"), data.get("err_msg"))
    print("  result:", data.get("result"))
    return data.get("err_no") == 0


if __name__ == "__main__":
    if not API_KEY or not SECRET_KEY:
        sys.exit("缺少 BAIDU_API_KEY / BAIDU_SECRET_KEY 环境变量")
    token = get_token()
    if not token:
        sys.exit(1)
    wav = os.path.join(tempfile.gettempdir(), "baidu_test.wav")
    make_test_wav(wav)
    ok = asr(token, wav)
    print("== result ==", "OK" if ok else "FAIL")
    sys.exit(0 if ok else 1)
