import os, json, sqlite3, secrets, time, base64
from pathlib import Path
from datetime import datetime, timezone
from contextlib import contextmanager
from typing import Any
import httpx
from fastapi import FastAPI, HTTPException, Header, Request, UploadFile, File
from fastapi.responses import FileResponse
from pydantic import BaseModel, Field
from dotenv import load_dotenv
import secrets
from typing import Optional


BASE = Path(__file__).resolve().parent
load_dotenv(BASE / '.env')
DB = BASE / 'smart_house.sqlite3'
DEVICE_TOKEN = os.getenv('DEVICE_TOKEN', '')
ADMIN_TOKEN = os.getenv('ADMIN_TOKEN', '')
GEMINI_KEY = os.getenv('GEMINI_API_KEY', '')
MODEL = os.getenv('GEMINI_MODEL', 'gemini-2.5-flash')
if not DEVICE_TOKEN or not ADMIN_TOKEN or DEVICE_TOKEN == ADMIN_TOKEN:
    raise RuntimeError('Set different DEVICE_TOKEN and ADMIN_TOKEN in server/.env')
app = FastAPI(title='Smart House AI 2.0', version='0.2.0')

@contextmanager
def dbconn():
    con = sqlite3.connect(DB)
    con.row_factory = sqlite3.Row
    try:
        yield con
        con.commit()
    finally:
        con.close()

def init_db():
    with dbconn() as con:
        con.executescript('''
        CREATE TABLE IF NOT EXISTS telemetry (id INTEGER PRIMARY KEY, at TEXT NOT NULL, data TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS events (id INTEGER PRIMARY KEY, at TEXT NOT NULL, kind TEXT NOT NULL, data TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS commands (id INTEGER PRIMARY KEY, at TEXT NOT NULL, device TEXT NOT NULL, action TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'PENDING', executed_at TEXT);
        ''')
init_db()

def now(): return datetime.now(timezone.utc).isoformat()
def valid(provided: Optional[str], expected: str) -> bool:
    return bool(provided) and secrets.compare_digest(provided, expected)

def require_device(token):
    if not valid(token, DEVICE_TOKEN): raise HTTPException(401, 'Device authentication failed')
def require_admin(token):
    if not valid(token, ADMIN_TOKEN): raise HTTPException(401, 'Admin authentication failed')

# Restrict to supported actuators; door locks and hazardous actuators are intentionally excluded.
ACTIONS = {'light': {'on','off'}, 'night_light': {'on','off'}, 'fan': {'on','off'}, 'curtain': {'open','close'}}
class CommandIn(BaseModel):
    device: str
    action: str
class AckIn(BaseModel):
    id: int
    status: str = 'EXECUTED'
class EventIn(BaseModel):
    type: str = Field(min_length=1, max_length=64)
    detail: dict[str,Any] = Field(default_factory=dict)
class AskIn(BaseModel):
    message: str = Field(min_length=1, max_length=500)

def record_event(kind: str, details: dict):
    # Never persist photographs, audio, biometric templates, or raw badge IDs.
    with dbconn() as con:
        con.execute('INSERT INTO events(at,kind,data) VALUES(?,?,?)',
                    (now(), kind, json.dumps({'type': kind, 'detail': details}, ensure_ascii=False)))

def recent_home_state():
    with dbconn() as con:
        row=con.execute('SELECT data FROM telemetry ORDER BY id DESC LIMIT 1').fetchone()
    return json.loads(row['data']) if row else {'message':'No telemetry yet'}

async def gemini_generate(parts, *, system_text, use_tools=False):
    if not GEMINI_KEY:
        raise HTTPException(503, 'Set GEMINI_API_KEY in server/.env')
    body={'systemInstruction':{'parts':[{'text':system_text}]},
          'contents':[{'role':'user','parts':parts}], 'generationConfig':{'temperature':0.1}}
    if use_tools:
        body['tools']=[{'functionDeclarations':[
            {'name':'get_home_status','description':'Read current smart-home state','parameters':{'type':'OBJECT','properties':{}}},
            {'name':'set_device','description':'Queue a light, fan or curtain prototype action, never unlock a door.',
             'parameters':{'type':'OBJECT','properties':{'device':{'type':'STRING','enum':list(ACTIONS)},
                                                      'action':{'type':'STRING','enum':['on','off','open','close']}},
                           'required':['device','action']}}
        ]}]
    try:
        async with httpx.AsyncClient(timeout=45) as client:
            r=await client.post(f'https://generativelanguage.googleapis.com/v1beta/models/{MODEL}:generateContent',
                                params={'key':GEMINI_KEY},json=body)
        r.raise_for_status()
        candidates=r.json().get('candidates') or []
        return candidates[0].get('content',{}).get('parts',[]) if candidates else []
    except (httpx.HTTPError, ValueError) as exc:
        raise HTTPException(502,'Gemini request failed; check API key/model/network') from exc

async def interpret_request(parts, *, voice=False):
    instruction=('Bạn là trợ lý cho nhà thông minh thử nghiệm. Hiểu tiếng Việt. '
                 'Khi có yêu cầu điều khiển đèn/quạt/rèm, dùng công cụ set_device. '
                 'Không điều khiển khóa cửa, không xem AI là cơ chế xác thực danh tính. '
                 'Chỉ xác nhận đã xếp lệnh vào hàng đợi, không tự khẳng định thiết bị đã thực thi. '
                 'Nếu đây là âm thanh, hãy hiểu câu nói và phản hồi ngắn bằng tiếng Việt.')
    result=await gemini_generate(parts,system_text=instruction,use_tools=True)
    calls=[]; texts=[]
    for part in result:
        if part.get('text'):texts.append(part['text'])
        f=part.get('functionCall')
        if not f:continue
        args=f.get('args') or {}
        if f.get('name')=='get_home_status':
            calls.append({'tool':'get_home_status','result':recent_home_state()})
        elif f.get('name')=='set_device':
            try:calls.append({'tool':'set_device','result':queue_cmd(str(args.get('device','')),str(args.get('action','')))})
            except HTTPException:calls.append({'tool':'set_device','error':'Action not allowed'})
    return {'reply':' '.join(texts).strip() or ('Đã xếp lệnh; chờ thiết bị xác nhận.' if calls else 'Không nhận diện được yêu cầu.'),
            'tool_results':calls,'source':'voice' if voice else 'chat'}

def queue_cmd(device: str, action: str):
    if action not in ACTIONS.get(device, set()):
        raise HTTPException(400, 'Unsupported device/action')
    with dbconn() as con:
        cur=con.execute('INSERT INTO commands(at,device,action,status) VALUES(?,?,?,?)', (now(),device,action,'PENDING'))
        return {'id':cur.lastrowid,'device':device,'action':action,'status':'PENDING'}

@app.get('/')
def index(): return FileResponse(BASE/'static'/'index.html')
@app.get('/health')
def health(): return {'ok':True}
@app.post('/device/telemetry')
def telemetry(payload: dict[str,Any], x_device_token: str | None = Header(default=None)):
    require_device(x_device_token)
    if len(json.dumps(payload)) > 8192: raise HTTPException(413, 'Payload too large')
    with dbconn() as con: con.execute('INSERT INTO telemetry(at,data) VALUES(?,?)', (now(),json.dumps(payload)))
    return {'ok':True}
@app.post('/device/event')
def event(payload:EventIn,x_device_token: str | None = Header(default=None)):
    require_device(x_device_token)
    with dbconn() as con: con.execute('INSERT INTO events(at,kind,data) VALUES(?,?,?)', (now(),payload.type,payload.model_dump_json()))
    return {'ok':True}
@app.get('/device/commands')
def pending(x_device_token: str | None = Header(default=None)):
    require_device(x_device_token)
    with dbconn() as con:
        rows=con.execute("SELECT id,device,action FROM commands WHERE status='PENDING' ORDER BY id LIMIT 5").fetchall()
    return {'commands':[dict(x) for x in rows]}
@app.post('/device/ack')
def ack(payload:AckIn,x_device_token: str | None = Header(default=None)):
    require_device(x_device_token)
    if payload.status not in ('EXECUTED','FAILED'): raise HTTPException(400,'Bad status')
    with dbconn() as con:
        cur=con.execute("UPDATE commands SET status=?,executed_at=? WHERE id=? AND status='PENDING'", (payload.status,now(),payload.id))
        if cur.rowcount == 0: raise HTTPException(404, 'No pending command with that id')
    return {'ok':True}
@app.get('/api/status')
def status(x_admin_token: str | None = Header(default=None)):
    require_admin(x_admin_token)
    with dbconn() as con:
        r=con.execute('SELECT at,data FROM telemetry ORDER BY id DESC LIMIT 1').fetchone()
        pending_count=con.execute("SELECT COUNT(*) FROM commands WHERE status='PENDING'").fetchone()[0]
    return {'latest': {'at':r['at'],**json.loads(r['data'])} if r else None,'pending_count':pending_count}
@app.get('/api/events')
def events(x_admin_token: str | None = Header(default=None)):
    require_admin(x_admin_token)
    with dbconn() as con:
        rows=con.execute('SELECT id,at,kind,data FROM events ORDER BY id DESC LIMIT 50').fetchall()
    return {'events':[dict(x) for x in rows]}
@app.get('/api/commands')
def commands(x_admin_token: str | None = Header(default=None)):
    require_admin(x_admin_token)
    with dbconn() as con:
        rows=con.execute('SELECT * FROM commands ORDER BY id DESC LIMIT 40').fetchall()
    return {'commands':[dict(x) for x in rows]}
@app.post('/api/command')
def command(payload:CommandIn,x_admin_token: str | None = Header(default=None)):
    require_admin(x_admin_token)
    return queue_cmd(payload.device,payload.action)
@app.post('/api/ask')
async def ask(payload: AskIn, x_admin_token: str | None = Header(default=None)):
    require_admin(x_admin_token)
    return await interpret_request([{'text':payload.message}])

@app.post('/device/access')
def access_event(payload: EventIn, x_device_token: str | None = Header(default=None)):
    require_device(x_device_token)
    if payload.type not in ('rfid_seen','fingerprint_seen','fingerprint_unknown','rfid_unknown'):
        raise HTTPException(400,'Unsupported access event')
    # Telemetry only: does not authorize unlock. Never accept image or biometric template here.
    allowed={'matched','reader','error'}
    detail={k:v for k,v in payload.detail.items() if k in allowed and isinstance(v,(bool,str,int))}
    record_event(payload.type,detail)
    return {'ok':True,'door_unlock_authorized':False}

@app.post('/device/voice')
async def device_voice(request:Request,x_device_token: str | None = Header(default=None)):
    require_device(x_device_token)
    if request.headers.get('content-type','').split(';')[0].strip() not in ('audio/wav','audio/x-wav'):
        raise HTTPException(415,'Expected audio/wav')
    # Hard limit on received bytes. Never save the audio to disk.
    data=await request.body()
    if len(data)<44 or len(data)>192000:
        raise HTTPException(413,'WAV must be 44..192000 bytes (max about 6 seconds mono PCM 16kHz)')
    if data[:4]!=b'RIFF' or data[8:12]!=b'WAVE':
        raise HTTPException(400,'Invalid WAV header')
    return await interpret_request([{'text':'Nghe âm thanh tiếng Việt này; nếu là lệnh điều khiển, gọi công cụ thích hợp.'},
                                    {'inlineData':{'mimeType':'audio/wav','data':base64.b64encode(data).decode('ascii')}}],voice=True)

@app.post('/device/face')
async def device_face(request:Request,x_device_token: str | None = Header(default=None)):
    require_device(x_device_token)
    if request.headers.get('content-type','').split(';')[0].strip()!='image/jpeg':
        raise HTTPException(415,'Expected image/jpeg')
    data=await request.body()
    if len(data)<100 or len(data)>300000:
        raise HTTPException(413,'JPEG exceeds 300 KB limit')
    if not data.startswith(b'\xff\xd8') or not data.endswith(b'\xff\xd9'):
        raise HTTPException(400,'Invalid JPEG framing')
    # Vision is advisory; no identity verification and no physical unlocking.
    result=await gemini_generate([{'text':'Mô tả ngắn bằng tiếng Việt: trong ảnh có người đứng gần cửa không? Không đoán danh tính hoặc tên người.'},
                                  {'inlineData':{'mimeType':'image/jpeg','data':base64.b64encode(data).decode('ascii')}}],
                                 system_text='Chỉ mô tả đối tượng hiện diện trong ảnh. Không xác thực danh tính, không cấp quyền mở cửa.')
    message=' '.join(p.get('text','') for p in result).strip()
    record_event('camera_frame_analyzed',{'analyzed':True})
    return {'analysis':message,'identity_verified':False,'unlock_authorized':False}
