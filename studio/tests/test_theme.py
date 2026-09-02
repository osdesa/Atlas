from PySide6.QtGui import QColor

from atlas_studio.views.theme import DEFAULT_DIAGRAM_PALETTE, load_stylesheet


def test_packaged_stylesheet_and_diagram_palette_are_valid() -> None:
    stylesheet = load_stylesheet()
    assert "QPushButton:hover" in stylesheet
    assert "QProgressBar::chunk" in stylesheet

    for value in DEFAULT_DIAGRAM_PALETTE.__dict__.values():
        assert QColor(value).isValid()
