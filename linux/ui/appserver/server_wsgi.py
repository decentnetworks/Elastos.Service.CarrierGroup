#!/usr/bin/env python
# -*- coding: utf-8 -*-
# by vellhe 2017/7/9
from flask import Flask
import configparser
from pathlib import Path

def create_app(chatrobot):
    app = Flask(__name__)

    conf = configparser.ConfigParser()
    config_path = Path(__file__).resolve().with_name("chatrobot_config.ini")
    conf.read(config_path)
    data_dir = conf.get("chatrobot", "data_dir") # 获取指定section 的option值
    ip = conf.get("chatrobot", "socket_ip")
    port = conf.getint("chatrobot", "socket_port")
    Path(data_dir).mkdir(parents=True, exist_ok=True)
    chatrobot.start(ip, port, data_dir)
    return app
