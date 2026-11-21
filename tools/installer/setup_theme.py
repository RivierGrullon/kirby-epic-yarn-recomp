# SPDX-License-Identifier: GPL-3.0-only
"""Qt Widgets used by Setup, in the native launcher's linen and stitched-patch style."""
from PyQt6.QtCore import QDir, QPointF, QRectF, QSize, Qt
from PyQt6.QtGui import QColor, QFont, QFontDatabase, QPainter, QPainterPath, QPalette, QPen, QPixmap
from PyQt6.QtWidgets import (QComboBox, QFrame, QGridLayout, QHBoxLayout, QLabel, QPushButton,
                             QSizePolicy, QVBoxLayout, QWidget)

from .setup_model import PHASES


def configure_application(app, project):
    app.setStyle('Fusion')
    font_file = project/'launcher/assets/launcher-ui.ttf'
    families = ['Segoe UI', 'Noto Sans', 'DejaVu Sans']
    if font_file.is_file():
        font_id = QFontDatabase.addApplicationFont(str(font_file))
        if font_id >= 0:
            families.extend(QFontDatabase.applicationFontFamilies(font_id))
    font = QFont()
    font.setFamilies(families)
    font.setPixelSize(15)
    app.setFont(font)
    colors = QPalette()
    for role, value in {
        QPalette.ColorRole.Window: '#f8f4e9', QPalette.ColorRole.WindowText: '#35463b',
        QPalette.ColorRole.Base: '#fffdf6', QPalette.ColorRole.Text: '#35463b',
        QPalette.ColorRole.Button: '#f5efdf', QPalette.ColorRole.ButtonText: '#35463b',
        QPalette.ColorRole.Highlight: '#b45c7b', QPalette.ColorRole.HighlightedText: '#ffffff',
        QPalette.ColorRole.PlaceholderText: '#717a70',
    }.items():
        colors.setColor(role, QColor(value))
    app.setPalette(colors)
    theme = project/'launcher/qt/theme'
    QDir.setSearchPaths('yarnTheme', [str(theme)])
    app.setStyleSheet("""
        QLabel { background: transparent; }
        QLabel#brand { font-family: Georgia, serif; font-size: 24px; font-weight: bold; color: #30483f; }
        QLabel#title { font-size: 38px; font-weight: bold; color: #314c40; }
        QLabel#section { font-size: 19px; font-weight: bold; }
        QLabel#stage { font-weight: bold; }
        QLabel#stage[status="failed"] { color: #903d53; }
        QLabel#stage[status="ready"] { color: #35644b; }
        QLabel#secondary { color: #5e6e61; font-size: 13px; }
        QFrame#card { background: #fffdf6; border: 1px solid #d9d5c8; border-radius: 16px; }
        QLineEdit, QPlainTextEdit { background: #faf9f0; color: #35463b; border: 1px solid #c8c7b7;
            border-radius: 8px; padding: 7px 10px; selection-background-color: #b45c7b; }
        QLineEdit:focus, QPlainTextEdit:focus { border: 2px solid #ad6a82; padding: 6px 9px; }
        QLineEdit:disabled { color: #70786d; background: #f1efe5; }
        QCheckBox { spacing: 8px; }
        QCheckBox:focus { outline: 1px solid #ad6a82; }
        QProgressBar { background: #e6e8db; border: 0; border-radius: 5px; }
        QProgressBar::chunk { background: #ba6e8b; border-radius: 5px; }
        QScrollArea, QWidget#paper, QWidget#viewport { background: transparent; border: 0; }
        QScrollBar:vertical { background: transparent; width: 9px; }
        QScrollBar::handle:vertical { background: #c7ccba; min-height: 24px; border-radius: 4px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QToolTip { background: #fff9ea; color: #38463d; border: 1px solid #c3baa2; padding: 4px; }
        QComboBox { background: #fffcf1; color: #405347; border: 1px dashed #bdb59f;
            border-radius: 9px; padding: 5px 12px; min-height: 22px; }
        QComboBox:focus { border: 2px solid #ad6a82; padding: 4px 11px; }
        LanguagePicker::drop-down { border: 0; width: 24px; }
        LanguagePicker::down-arrow { width: 0; height: 0; }
        QComboBox QAbstractItemView { background: #fffaf0; selection-background-color: #efd1d9;
            selection-color: #423e39; padding: 3px; }
    """ + (theme/'dialogs.qss').read_text(encoding='utf-8'))


def label(text, name=''):
    widget = QLabel(text)
    widget.setObjectName(name)
    widget.setTextFormat(Qt.TextFormat.PlainText)
    widget.setWordWrap(True)
    return widget


def stitch(painter, rect, color, radius):
    pen = QPen(QColor(color), 1.2, Qt.PenStyle.DashLine, Qt.PenCapStyle.RoundCap)
    pen.setDashPattern([3, 4])
    painter.setPen(pen)
    painter.setBrush(Qt.BrushStyle.NoBrush)
    painter.drawRoundedRect(rect, radius, radius)


def button_patch(fill, size):
    pix = QPixmap(size*2, size*2)
    pix.setDevicePixelRatio(2)
    pix.fill(Qt.GlobalColor.transparent)
    p = QPainter(pix)
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    color = QColor(fill)
    center = QPointF(size/2, size/2)
    radius = size*.44
    p.setBrush(color)
    p.setPen(QPen(color.darker(125), 1))
    p.drawEllipse(center, radius, radius)
    p.setBrush(Qt.BrushStyle.NoBrush)
    p.drawEllipse(center, radius*.78, radius*.78)
    p.setPen(QPen(QColor('#fff8eb'), max(1.4, size*.065), Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
    hole = radius*.26
    for sign in (-1, 1):
        p.drawLine(center+QPointF(-hole, sign*hole), center+QPointF(hole, -sign*hole))
    p.end()
    return pix


class Linen(QWidget):
    def paintEvent(self, event):
        p = QPainter(self)
        p.fillRect(self.rect(), QColor('#f8f4e9'))
        p.setPen(QColor(152, 118, 76, 15))
        for x in range(0, self.width(), 7):
            p.drawLine(x, 0, x, self.height())
        for y in range(0, self.height(), 7):
            p.drawLine(0, y, self.width(), y)


class Banner(QWidget):
    def __init__(self, project):
        super().__init__()
        self.art = QPixmap(str(project/'imgs/Kirbys_Epic_Yarn_Concept-Art.png'))
        if self.art.isNull():
            self.art = QPixmap(str(project/'packaging/linux/yarn-mascot-icon.png'))
        self.setFixedHeight(164)
        row = QHBoxLayout(self)
        row.setContentsMargins(26, 12, 18, 14)
        text = QVBoxLayout()
        text.setSpacing(2)
        text.addStretch()
        self.heading = label('Game setup', 'title')
        self.intro = label('Install from your own USA Wii copy (RK5E01 Rev 0).')
        text.addWidget(self.heading)
        text.addWidget(self.intro)
        text.addStretch()
        row.addLayout(text, 2)
        row.addStretch(1)

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHints(QPainter.RenderHint.Antialiasing | QPainter.RenderHint.SmoothPixmapTransform)
        rect = QRectF(self.rect()).adjusted(1, 1, -1, -3)
        path = QPainterPath()
        path.addRoundedRect(rect, 19, 19)
        p.fillPath(path.translated(0, 2), QColor('#c0d1b9'))
        p.fillPath(path, QColor('#deeee0'))
        p.setClipPath(path)
        p.setPen(QPen(QColor(92, 137, 104, 22), 1.3))
        for x in range(-self.height(), self.width()+self.height(), 32):
            p.drawLine(x, 0, x+self.height(), self.height())
            p.drawLine(x, self.height(), x+self.height(), 0)
        if not self.art.isNull():
            box = QRectF(self.width()*.64, 5, self.width()*.35-12, self.height()-11)
            size = self.art.size().scaled(box.size().toSize(), Qt.AspectRatioMode.KeepAspectRatio)
            target = QRectF(0, 0, size.width(), size.height())
            target.moveCenter(box.center())
            p.drawPixmap(target, self.art, QRectF(self.art.rect()))
        stitch(p, rect.adjusted(7, 7, -7, -7), '#9ab8a1', 13)


class FabricButton(QPushButton):
    def __init__(self, text, primary=False):
        super().__init__(text)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Fixed)
        self.set_primary(primary)

    def set_primary(self, primary):
        self.primary = primary
        font = self.font()
        font.setBold(primary)
        self.setFont(font)
        self.updateGeometry()
        self.update()

    def sizeHint(self):
        return QSize(self.fontMetrics().horizontalAdvance(self.text())+(38 if self.primary else 30),
                     46 if self.primary else 37)

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        fill, edge, seam, ink = (('#bf607f', '#a75470', '#f2c1d0', '#fffaf0') if self.primary else
                                 ('#f5efdf', '#cac2aa', '#c5bca7', '#3d5143'))
        if not self.isEnabled():
            fill, edge, seam, ink = (('#eed7df', '#d6b9c3', '#faf0f3', '#785d67') if self.primary else
                                     ('#edebe0', '#d4d0c4', '#d8d1c0', '#757b71'))
        elif self.isDown() or self.isChecked():
            fill = '#a34d68' if self.primary else '#e3ebdc'
        elif self.underMouse():
            fill = '#b55575' if self.primary else '#f6e5e6'
        radius = 13 if self.primary else 10
        rect = QRectF(self.rect()).adjusted(1, 3 if self.isDown() else 1, -1, -4)
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QColor(edge))
        p.drawRoundedRect(rect.translated(0, 2), radius, radius)
        p.setPen(QPen(QColor('#8e4866' if self.hasFocus() else edge), 2 if self.hasFocus() else 1))
        p.setBrush(QColor(fill))
        p.drawRoundedRect(rect, radius, radius)
        stitch(p, rect.adjusted(4, 4, -4, -4), seam, radius-3)
        p.setPen(QColor(ink))
        p.drawText(rect, Qt.AlignmentFlag.AlignCenter, self.text())


class LanguagePicker(QComboBox):
    def paintEvent(self, event):
        super().paintEvent(event)
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        center = QPointF(self.width()-16, self.height()/2)
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QColor('#dcead9'))
        p.drawEllipse(center, 9, 9)
        p.setPen(QPen(QColor('#5b7160'), 1.7, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        p.drawLine(center+QPointF(-4, -1.5), center+QPointF(0, 2.5))
        p.drawLine(center+QPointF(0, 2.5), center+QPointF(4, -1.5))


class Card(QFrame):
    def __init__(self, title, color, buddy=None):
        super().__init__()
        self.setObjectName('card')
        self.body = QVBoxLayout(self)
        self.body.setContentsMargins(17, 13, 17, 14)
        self.body.setSpacing(8)
        heading = QHBoxLayout()
        patch = QLabel()
        patch.setPixmap(button_patch(color, 23))
        heading.addWidget(patch)
        self.heading = label(title, 'section')
        if buddy:
            self.heading.setBuddy(buddy)
        heading.addWidget(self.heading, 1)
        self.body.addLayout(heading)


class PhaseList(QWidget):
    STATES = {
        'pending': ('Waiting', '#bdbdaf'), 'running': ('In progress', '#c87e97'),
        'done': ('Done', '#83ac90'), 'failed': ('Failed', '#c96e67'),
        'cancelled': ('Cancelled', '#cbad78'),
    }

    def __init__(self):
        super().__init__()
        self.rows = []
        grid = QGridLayout(self)
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setSpacing(5)
        grid.setColumnStretch(1, 1)
        for i, phase in enumerate(PHASES):
            marker, name, state = QLabel(), label(phase), label('', 'secondary')
            grid.addWidget(marker, i, 0)
            grid.addWidget(name, i, 1)
            grid.addWidget(state, i, 2, Qt.AlignmentFlag.AlignRight)
            self.rows.append((marker, name, state))
        self.states = ('pending',)*len(PHASES)
        self.set_language(str)

    def set_language(self, translate):
        self.translate = translate
        self._refresh()

    def set_states(self, states):
        states = tuple(states)
        if states == self.states:
            return
        self.states = states
        self._refresh()

    def _refresh(self):
        for phase, key, (marker, name, state) in zip(PHASES, self.states, self.rows):
            caption, color = self.STATES[key]
            caption = self.translate(caption)
            name.setText(self.translate(phase))
            pix = button_patch(color, 16)
            if key != 'running':
                pix.fill(Qt.GlobalColor.transparent)
                p = QPainter(pix)
                p.setRenderHint(QPainter.RenderHint.Antialiasing)
                p.setBrush(QColor('#fffdf6') if key == 'pending' else QColor(color))
                p.setPen(QPen(QColor(color), 1.4))
                p.drawEllipse(QRectF(2, 2, 12, 12))
                p.setPen(QPen(QColor('#fffdf6'), 1.5, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
                if key == 'done':
                    p.drawLine(5, 8, 7, 10)
                    p.drawLine(7, 10, 11, 6)
                elif key == 'failed':
                    p.drawLine(6, 6, 10, 10)
                    p.drawLine(10, 6, 6, 10)
                elif key == 'cancelled':
                    p.drawLine(5, 8, 11, 8)
                p.end()
            marker.setPixmap(pix)
            name.setAccessibleName(f'{name.text()}: {caption}')
            state.setText('' if key == 'pending' else caption)
            marker.setToolTip(caption)
