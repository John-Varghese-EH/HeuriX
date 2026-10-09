import urllib.request
import threading
import time

def read_stream():
    req = urllib.request.Request("http://127.0.0.1:50051/telemetry")
    with urllib.request.urlopen(req) as response:
        for line in response:
            print(line.decode('utf-8').strip())

t = threading.Thread(target=read_stream)
t.daemon = True
t.start()
time.sleep(1)
import os
os.system("touch ./canary/test.txt")
time.sleep(2)
