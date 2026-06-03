import sys
import time
from PyQt5.QtCore import QCoreApplication
from core.fs_process import FSProcess

app = QCoreApplication(sys.argv)
proc = FSProcess()

def on_line(line):
    print(f"RECV: {repr(line)}")

proc.line_received.connect(on_line)
proc.start()

time.sleep(1)
print("SENDING statfs")
proc.send_command("statfs\n")

time.sleep(2)
proc.terminate()
