"""Worker ownership for bounded pack inspection and filesystem operations."""

from PySide6.QtCore import QObject, QThread, Signal, Slot


class PackJob(QObject):
    """Execute one manager operation off the GUI thread and report an owned error string."""

    finished = Signal(str)

    def __init__(self, operation):
        super().__init__()
        self.operation = operation

    @Slot()
    def run(self):
        try:
            self.operation()
        except Exception as error:
            self.finished.emit(str(error))
        else:
            self.finished.emit("")


class PackJobs(QObject):
    """Serialize manager work and retain Qt objects until the worker has exited."""

    finished = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.thread = None
        self.worker = None
        self.error = ""

    def start(self, operation):
        if self.thread is not None:
            raise RuntimeError("A pack operation is already active")
        self.thread = QThread(self)
        self.worker = PackJob(operation)
        self.worker.moveToThread(self.thread)
        self.thread.started.connect(self.worker.run)
        self.worker.finished.connect(self._done)
        self.worker.finished.connect(self.thread.quit)
        self.worker.finished.connect(self.worker.deleteLater)
        self.thread.finished.connect(self._stopped)
        self.thread.start()

    @Slot(str)
    def _done(self, error):
        self.error = error

    @Slot()
    def _stopped(self):
        # finished precedes deferred QObject destruction and thread-local cleanup.
        # Join before releasing the Python worker wrapper or permitting another job.
        self.thread.wait()
        self.thread.deleteLater()
        self.thread = None
        self.worker = None
        self.finished.emit(self.error)
