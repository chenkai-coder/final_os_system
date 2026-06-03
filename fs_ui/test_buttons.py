import sys
from PyQt5.QtWidgets import QApplication, QAction
from ui.main_window import MainWindow

app = QApplication(sys.argv)
win = MainWindow()

def hook(cmd):
    print(f"HOOK: {repr(cmd)}")

win.command_sent.connect(hook)

for action in win.findChildren(QAction):
    if action.text() in ["Statfs", "Super", "Free Group", "Cache", "FSCK"]:
        print(f"Triggering {action.text()}")
        action.trigger()
