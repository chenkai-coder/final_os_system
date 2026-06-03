import sys
from PyQt5.QtWidgets import QApplication, QMainWindow, QAction
from PyQt5.QtCore import pyqtSignal

class Test(QMainWindow):
    command_sent = pyqtSignal(str)
    def __init__(self):
        super().__init__()
        action = QAction("test", self)
        action.triggered.connect(lambda: self._send("statfs\n"))
        action.trigger()

    def _send(self, cmd):
        print(f"SENT: {repr(cmd)}")

app = QApplication(sys.argv)
t = Test()
