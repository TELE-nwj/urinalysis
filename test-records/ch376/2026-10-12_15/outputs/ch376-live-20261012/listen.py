import argparse, codecs, datetime, json, time
from pathlib import Path
import serial

parser=argparse.ArgumentParser()
parser.add_argument('--port',default='COM21')
parser.add_argument('--seconds',type=int,default=180)
args=parser.parse_args()
folder=Path(__file__).resolve().parent
stamp=datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
path=folder/f'{args.port}-{stamp}.log'
status=folder/'latest-status.json'
connection=serial.Serial(port=None,baudrate=115200,bytesize=serial.EIGHTBITS,parity=serial.PARITY_NONE,stopbits=serial.STOPBITS_ONE,timeout=0.2,xonxoff=False,rtscts=False,dsrdtr=False)
connection.dtr=False
connection.rts=False
connection.port=args.port
received=0
decoder=codecs.getincrementaldecoder('utf-8')(errors='replace')
pending=''
def state(value):
 status.write_text(json.dumps({'status':value,'port':args.port,'file':str(path),'bytes':received,'window_seconds':args.seconds},ensure_ascii=False),encoding='utf-8')
try:
 connection.open()
 state('listening')
 print(f'LISTENING {args.port} 115200 8N1; receive only; DTR/RTS disabled; {args.seconds}s window; log={path}',flush=True)
 started=time.monotonic()
 with path.open('wb') as output:
  while time.monotonic()-started<args.seconds:
   data=connection.read(connection.in_waiting or 1)
   if not data:continue
   received+=len(data);output.write(data);output.flush()
   pending+=decoder.decode(data)
   while '\n' in pending:
    line,pending=pending.split('\n',1)
    # Raw log retains every byte; console focuses on storage, state and errors.
    if any(word in line for word in ('S1:', 'CH376', 'storage:', 'monitor:', 'main:', 'W (', 'E (','abort','watchdog','panic')) or received<1600:
     print(line.rstrip('\r'),flush=True)
 state('finished')
 print(f'FINISHED bytes={received}; log={path}',flush=True)
except Exception as error:
 state('error')
 print(f'LISTENER_ERROR {type(error).__name__}: {error}',flush=True)
 raise
finally:
 if connection.is_open:connection.close()
