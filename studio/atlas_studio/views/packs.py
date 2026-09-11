"""Task pack management view; emits intent without loading native modules."""

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QDialog,
    QFileDialog,
    QHBoxLayout,
    QPushButton,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
)


class PackManagerView(QDialog):
    """Display exact identities, capabilities and explicit trust actions as plain text."""

    import_requested = Signal(str)
    trust_requested = Signal(str, bool)
    remove_requested = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Task Packs")
        self.resize(1000, 500)
        layout = QVBoxLayout(self)
        self.tree = QTreeWidget()
        self.tree.setHeaderLabels(
            ["Pack / task", "Version", "SHA-256 digest", "Availability / trust", "Capabilities"]
        )
        layout.addWidget(self.tree)
        actions = QHBoxLayout()
        for title, callback in (
            ("Import directory", self._import),
            ("Trust digest", lambda: self.trust_requested.emit(self._digest(), True)),
            ("Revoke trust", lambda: self.trust_requested.emit(self._digest(), False)),
            ("Remove", lambda: self.remove_requested.emit(self._digest())),
        ):
            button = QPushButton(title)
            button.clicked.connect(callback)
            actions.addWidget(button)
        layout.addLayout(actions)

    def _digest(self):
        item = self.tree.currentItem()
        if item and item.parent():
            item = item.parent()
        return item.text(2) if item else ""

    def _import(self):
        path = QFileDialog.getExistingDirectory(self, "Import task-pack directory")
        if path:
            self.import_requested.emit(path)

    def render(self, packs, statuses, errors):
        self.tree.clear()
        for digest, pack in packs.items():
            item = QTreeWidgetItem(
                [pack["pack_id"], pack["version"], digest, statuses[digest], pack.get("description", "")]
            )
            self.tree.addTopLevelItem(item)
            for task in pack["tasks"]:
                QTreeWidgetItem(
                    item,
                    [
                        task.get("name") or task["task_id"],
                        "",
                        "",
                        task["resource"],
                        ("Slicing; " if task.get("supports_slicing") else "") + task.get("description", ""),
                    ],
                )
        for error in errors:
            self.tree.addTopLevelItem(QTreeWidgetItem(["Damaged pack", "", error.split(":", 1)[0], error]))
