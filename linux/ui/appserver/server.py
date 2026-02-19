#!/usr/bin/env python
# -*- coding: utf-8 -*-
# by vellhe 2017/7/9
from flask import Flask, abort, request, jsonify,make_response
from flask_cors import CORS
import argparse
from pathlib import Path
import chatrobot_restful_api as chatrobot

def start(manager_ip, manager_port, data_dir):
    Path(data_dir).mkdir(parents=True, exist_ok=True)
    print(data_dir)
    chatrobot.start(manager_ip, manager_port, data_dir)
app = Flask(__name__)
CORS(app, resource={r"/*":{"orgins":"*"}}, supports_credentials=True)
@app.route("/")
def hello():
    return "<h1 style='color:blue'>Hello There OK!</h1>"

@app.route('/create', methods=['GET'])
def create():
    chatrobot.createGroup();
    print("create");
    return jsonify({'result':"success"});

@app.route('/groups', methods=['GET'])
def list_groups():
    data = chatrobot.list();
    print("groups:", data);
    return jsonify({'code':0, 'data':data});

parser = argparse.ArgumentParser()
parser.add_argument('--ip', type=str, default="127.0.0.1")
parser.add_argument('--port', type=int, default=5000)
parser.add_argument('--manager_ip', type=str, default="127.0.0.1")
parser.add_argument('--manager_port', type=int, default=3333)
default_data_path = str((Path(__file__).resolve().parent.parent / "runtime_data").resolve())
parser.add_argument('--data_path', type=str, default=default_data_path)
args = parser.parse_args();

if __name__ == "__main__":
    # 将host设置为0.0.0.0，则外网用户也可以访问到这个服务
    start(args.manager_ip, args.manager_port, args.data_path)
    print("start in************************************8")
    app.run(host=args.ip, port=args.port)

