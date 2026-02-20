#!/usr/bin/env python
# -*- coding: utf-8 -*-
# by vellhe 2017/7/9
from flask import Flask, abort, request, jsonify,make_response
from flask_cors import CORS
import argparse
from pathlib import Path
import sqlite3
import chatrobot_restful_api as chatrobot

RUNTIME_DATA_DIR = ""

def start(manager_ip, manager_port, data_dir):
    global RUNTIME_DATA_DIR
    Path(data_dir).mkdir(parents=True, exist_ok=True)
    RUNTIME_DATA_DIR = str(Path(data_dir).resolve())
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

def _read_agent_table(group_id):
    db_path = Path(RUNTIME_DATA_DIR) / ("carrierService" + str(group_id)) / "chatrobot.db"
    if not db_path.exists():
        return None, "group db not found"
    conn = sqlite3.connect(str(db_path))
    try:
        cur = conn.cursor()
        cur.execute("select UserId, Address from agent_table order by id asc")
        rows = cur.fetchall()
        data = []
        for row in rows:
            data.append({"userid": row[0], "address": row[1]})
        return data, ""
    finally:
        conn.close()

@app.route('/agent/list', methods=['GET'])
def list_agents():
    group_id = request.args.get('group_id')
    if not group_id:
        return jsonify({'code':1, 'error':'missing group_id'}), 400
    data, err = _read_agent_table(group_id)
    if data is None:
        return jsonify({'code':2, 'error':err}), 404
    return jsonify({'code':0, 'group_id':str(group_id), 'data':data})

@app.route('/agent/add', methods=['GET', 'POST'])
def add_agent():
    group_id = request.values.get('group_id')
    address = request.values.get('address')
    if not group_id or not address:
        return jsonify({'code':1, 'error':'missing group_id or address'}), 400
    ret = chatrobot.addAgent(group_id, address)
    if ret != 0:
        return jsonify({'code':2, 'error':'add agent command failed (service may not be ready)'}), 500
    data, _ = _read_agent_table(group_id)
    return jsonify({'code':0, 'group_id':str(group_id), 'address':address, 'data':data if data is not None else []})

@app.route('/agent/remove', methods=['GET', 'POST'])
def remove_agent():
    group_id = request.values.get('group_id')
    user_id = request.values.get('userid')
    if not group_id or not user_id:
        return jsonify({'code':1, 'error':'missing group_id or userid'}), 400
    ret = chatrobot.removeAgent(group_id, user_id)
    if ret != 0:
        return jsonify({'code':2, 'error':'remove agent command failed (service may not be ready)'}), 500
    data, _ = _read_agent_table(group_id)
    return jsonify({'code':0, 'group_id':str(group_id), 'userid':user_id, 'data':data if data is not None else []})

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
