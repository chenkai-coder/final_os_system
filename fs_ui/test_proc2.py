import subprocess
from config import FS_CORE_PATH

p = subprocess.Popen([FS_CORE_PATH], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
print(repr(p.stdout.readline())) # read prompt
p.stdin.write("statfs\n")
p.stdin.flush()
print(repr(p.stdout.readline()))
p.stdin.write("exit\n")
p.stdin.flush()
