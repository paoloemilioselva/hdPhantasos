"""The Phantasos parameter editor: a window beside usdview that shows the
PhantasosAPI parameters of the selected prims, with each one's
documentation, and adds, changes and removes them.

- It follows usdview's selection (untick 'Follow selection' to keep editing
  the same prims while selecting others). With several prims selected, an
  edit applies to all of them; a value they do not share shows as mixed.
- Each parameter's box is ticked when the prims author it themselves. A
  value they inherit (constant primvars inherit down the namespace) or the
  schema's fallback shows greyed, with where it comes from; editing it, or
  ticking the box, authors it on the selected prims. Unticking removes it.
- Edits go to the layer chosen at the top: the root layer (saved with
  'Save') or the session layer (gone when usdview closes). The delegate
  sees an edit as soon as it is made.
"""

from pxr import Gf, Sdf, Tf, Usd
from pxr.Usdviewq.qt import QtCore, QtGui, QtWidgets

from . import parameters as params

MUTED = "color: #8a8a8a;"
DOC = "color: #9a9a9a; font-size: 11px;"


class _TextEdit(QtWidgets.QPlainTextEdit):
    """A multi-line field that commits when it loses focus or on Ctrl+Enter,
    not on every key: each commit makes the delegate describe the frame
    again, and a new shot starts a new Matrix-Game stream."""

    committed = QtCore.Signal()

    def focusOutEvent(self, event):
        super().focusOutEvent(event)
        self.committed.emit()

    def keyPressEvent(self, event):
        if event.key() in (QtCore.Qt.Key_Return, QtCore.Qt.Key_Enter) and \
                event.modifiers() & QtCore.Qt.ControlModifier:
            self.committed.emit()
            return
        super().keyPressEvent(event)


class _Row:
    """One parameter: its tick box, origin, editor and documentation."""

    def __init__(self, editor, parameter, layout):
        self.editor, self.parameter = editor, parameter
        self.reading = None
        self.shown = None  # the value the widget shows, to commit only real changes

        self.check = QtWidgets.QCheckBox(parameter.display_name)
        font = self.check.font()
        font.setBold(True)
        self.check.setFont(font)
        self.check.setToolTip(f"{parameter.attribute} ({parameter.type_name})\n"
                              "Ticked: the selected prims author it. Untick to remove it.")
        self.check.clicked.connect(self._on_check)
        self.origin = QtWidgets.QLabel()
        self.origin.setStyleSheet(MUTED)
        # A long path must not widen the window: it gives way, and the
        # tooltip has it whole.
        self.origin.setSizePolicy(QtWidgets.QSizePolicy.Ignored, QtWidgets.QSizePolicy.Preferred)
        self.go = QtWidgets.QPushButton("Select source")
        self.go.setFlat(True)
        self.go.setStyleSheet("text-decoration: underline;" + MUTED)
        self.go.setCursor(QtCore.Qt.PointingHandCursor)
        self.go.clicked.connect(self._on_go)
        header = QtWidgets.QHBoxLayout()
        header.addWidget(self.check)
        header.addWidget(self.origin, 1)
        header.addWidget(self.go)
        layout.addLayout(header)

        self.widget = self._make_widget()
        self.widget.setToolTip(parameter.doc)
        layout.addWidget(self.widget)

        doc = QtWidgets.QLabel(parameter.doc)
        doc.setWordWrap(True)
        doc.setStyleSheet(DOC)
        doc.setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse)
        layout.addWidget(doc)
        layout.addSpacing(8)

    # --- The value widget, by type -------------------------------------------

    def _make_widget(self):
        kind = self.parameter.kind
        if kind == "bool":
            self.box = QtWidgets.QCheckBox("Yes")
            self.box.clicked.connect(lambda: self._commit(self.box.isChecked()))
            return self.box
        if kind == "token":
            self.combo = QtWidgets.QComboBox()
            self.combo.addItems(self.parameter.allowed or [])
            self.combo.activated.connect(lambda _: self._commit(self.combo.currentText()))
            return self.combo
        if kind == "text":
            self.text = _TextEdit()
            self.text.setFixedHeight(64)
            self.text.committed.connect(lambda: self._commit(self.text.toPlainText()))
            return self.text
        if kind == "asset":
            holder = QtWidgets.QWidget()
            line = QtWidgets.QHBoxLayout(holder)
            line.setContentsMargins(0, 0, 0, 0)
            self.line = QtWidgets.QLineEdit()
            self.line.editingFinished.connect(lambda: self._commit(self.line.text()))
            browse = QtWidgets.QPushButton("Browse...")
            browse.clicked.connect(self._on_browse)
            line.addWidget(self.line, 1)
            line.addWidget(browse)
            return holder
        if kind == "color":
            holder = QtWidgets.QWidget()
            line = QtWidgets.QHBoxLayout(holder)
            line.setContentsMargins(0, 0, 0, 0)
            self.swatch = QtWidgets.QPushButton()
            self.swatch.setFixedSize(48, 22)
            self.swatch.clicked.connect(self._on_color)
            self.hex = QtWidgets.QLabel()
            self.hex.setStyleSheet(MUTED)
            line.addWidget(self.swatch)
            line.addWidget(self.hex, 1)
            return holder
        self.line = QtWidgets.QLineEdit()
        self.line.editingFinished.connect(lambda: self._commit(self.line.text()))
        return self.line

    def show(self, reading):
        self.reading = reading
        kind, value, mixed = self.parameter.kind, reading.value, reading.mixed
        greyed = reading.authored < reading.total or reading.total == 0
        if reading.total and reading.authored == reading.total:
            self.check.setCheckState(QtCore.Qt.Checked)
        elif reading.authored:
            self.check.setCheckState(QtCore.Qt.PartiallyChecked)
        else:
            self.check.setCheckState(QtCore.Qt.Unchecked)
        self.check.setEnabled(reading.total > 0)
        self.widget.setEnabled(reading.total > 0)

        if reading.total == 0:
            origin = ""
        elif reading.origin == "here":
            origin = "set here" if reading.total == 1 else f"set on all {reading.total}"
        elif reading.origin == "inherited":
            origin = f"inherited from {reading.source}"
        elif reading.origin == "fallback":
            origin = "not set: the fallback"
        else:
            origin = f"set on {reading.authored} of {reading.total}"
        if mixed:
            origin += ", several values"
        self.origin.setText(origin)
        self.origin.setToolTip(origin)
        self.go.setVisible(reading.origin == "inherited")

        style = "" if not greyed else "color: #8a8a8a;"
        self.shown = None if mixed else params._comparable(value)
        if kind == "bool":
            self.box.setTristate(mixed)
            self.box.setCheckState(QtCore.Qt.PartiallyChecked if mixed else
                                   QtCore.Qt.Checked if value else QtCore.Qt.Unchecked)
        elif kind == "token":
            text = "" if mixed or value is None else str(value)
            index = self.combo.findText(text)
            if index < 0:
                self.combo.addItem(text)
                index = self.combo.findText(text)
            self.combo.setCurrentIndex(index)
            self.combo.setStyleSheet(style)
        elif kind == "text":
            self.text.setPlainText("" if mixed or value is None else str(value))
            self.text.setPlaceholderText("several values" if mixed else "")
            self.text.setStyleSheet(style)
        elif kind == "asset":
            path = value.path if isinstance(value, Sdf.AssetPath) else (value or "")
            self.line.setText("" if mixed else path)
            self.line.setPlaceholderText("several values" if mixed else "an image file")
            self.line.setStyleSheet(style)
            if isinstance(value, Sdf.AssetPath) and value.resolvedPath:
                self.line.setToolTip(f"Resolves to {value.resolvedPath}\n\n{self.parameter.doc}")
        elif kind == "color":
            if mixed or value is None:
                self.swatch.setStyleSheet("")
                self.hex.setText("several values" if mixed else "derived from the subject key")
            else:
                rgb = [max(0, min(255, round(c * 255))) for c in value]
                self.swatch.setStyleSheet(f"background-color: rgb({rgb[0]}, {rgb[1]}, {rgb[2]});")
                self.hex.setText("#%02x%02x%02x  (%.3f, %.3f, %.3f)" % (*rgb, *value))
        else:
            self.line.setText("" if mixed or value is None else str(value))
            self.line.setPlaceholderText("several values" if mixed else "")
            self.line.setStyleSheet(style)

    # --- Actions ---------------------------------------------------------------

    def _commit(self, value):
        if self.editor.updating or not self.reading or not self.reading.total:
            return
        converted = params._comparable(params.convert(self.parameter, value))
        if converted == self.shown and self.reading.authored == self.reading.total:
            return  # nothing changed
        if self.parameter.kind in ("text", "string", "asset") and converted == "" and self.shown is None:
            return  # a mixed field left empty: not an edit
        self.editor.apply(f"Set {self.parameter.display_name}",
                          lambda prims: params.set_value(prims, self.parameter, value))

    def _on_check(self, checked):
        if self.editor.updating or not self.reading:
            return
        if checked:
            value = self.reading.value
            if value is None:
                value = self.parameter.fallback
            if value is None:
                value = {"bool": False, "color": (0.5, 0.5, 0.5)}.get(self.parameter.kind, "")
            self.editor.apply(f"Add {self.parameter.display_name}",
                              lambda prims: params.set_value(prims, self.parameter, value))
        else:
            def remove(prims):
                left = params.remove(prims, self.parameter)
                if left:
                    self.editor.say(f"{self.parameter.display_name} is still authored on "
                                    f"{len(left)} prim(s) in a layer the edit target cannot change.")
            self.editor.apply(f"Remove {self.parameter.display_name}", remove)

    def _on_go(self):
        if self.reading and self.reading.source:
            self.editor.select(self.reading.source)

    def _on_browse(self):
        path, _ = QtWidgets.QFileDialog.getOpenFileName(
            self.editor, f"{self.parameter.display_name}", self.line.text(),
            "Images (*.png *.jpg *.jpeg *.exr *.tif *.tiff *.webp);;All files (*)")
        if path:
            self.line.setText(path)
            self._commit(path)

    def _on_color(self):
        value = self.reading.value if self.reading and not self.reading.mixed else None
        initial = QtGui.QColor.fromRgbF(*value) if value is not None else QtGui.QColor(128, 128, 128)
        colour = QtWidgets.QColorDialog.getColor(initial, self.editor, self.parameter.display_name)
        if colour.isValid():
            self._commit((colour.redF(), colour.greenF(), colour.blueF()))


class PhantasosEditor(QtWidgets.QWidget):
    def __init__(self, usdviewApi):
        super().__init__(usdviewApi.qMainWindow, QtCore.Qt.Tool)
        self.api = usdviewApi
        self.data = usdviewApi.dataModel
        self.updating = False
        self.prims = []
        self.rows = []
        self.notice = None
        self.pending = QtCore.QTimer(self)
        self.pending.setSingleShot(True)
        self.pending.setInterval(0)
        self.pending.timeout.connect(self.refresh)
        self.setAttribute(QtCore.Qt.WA_DeleteOnClose)
        self.setWindowTitle("Phantasos parameters")
        self.resize(460, 760)

        layout = QtWidgets.QVBoxLayout(self)
        self.selection = QtWidgets.QLabel()
        self.selection.setWordWrap(True)
        self.selection.setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse)
        self.follow = QtWidgets.QCheckBox("Follow selection")
        self.follow.setChecked(True)
        self.follow.toggled.connect(lambda on: on and self._take_selection())
        top = QtWidgets.QHBoxLayout()
        top.addWidget(self.selection, 1)
        top.addWidget(self.follow)
        layout.addLayout(top)

        target = QtWidgets.QHBoxLayout()
        target.addWidget(QtWidgets.QLabel("Edit in"))
        self.layer = QtWidgets.QComboBox()
        self.layer.addItems(["Root layer (saved with Save)", "Session layer (not saved)"])
        self.layer.currentIndexChanged.connect(self._on_layer)
        self.save = QtWidgets.QPushButton("Save")
        self.save.setToolTip("Save the root layer to its file")
        self.save.clicked.connect(self._on_save)
        target.addWidget(self.layer, 1)
        target.addWidget(self.save)
        layout.addLayout(target)

        self.schema = QtWidgets.QLabel()
        self.schema.setWordWrap(True)
        self.schema.setStyleSheet(DOC)
        layout.addWidget(self.schema)

        scroll = QtWidgets.QScrollArea()
        scroll.setWidgetResizable(True)
        # The documentation wraps to the window's width instead.
        scroll.setHorizontalScrollBarPolicy(QtCore.Qt.ScrollBarAlwaysOff)
        body = QtWidgets.QWidget()
        self.body = QtWidgets.QVBoxLayout(body)
        scroll.setWidget(body)
        layout.addWidget(scroll, 1)

        bottom = QtWidgets.QHBoxLayout()
        self.clear = QtWidgets.QPushButton("Remove all")
        self.clear.setToolTip("Remove every Phantasos parameter, and PhantasosAPI, from the selected prims")
        self.clear.clicked.connect(self._on_remove_all)
        self.status = QtWidgets.QLabel()
        self.status.setStyleSheet(MUTED)
        self.status.setWordWrap(True)
        bottom.addWidget(self.status, 1)
        bottom.addWidget(self.clear)
        layout.addLayout(bottom)

        self._build_rows()
        self.data.selection.signalPrimSelectionChanged.connect(self._on_selection)
        self.data.signalStageReplaced.connect(self._on_stage)
        self._on_stage()

    # --- Building ----------------------------------------------------------------

    def _build_rows(self):
        every = params.parameters()
        self.every = every
        if not every:
            self.schema.setText("PhantasosAPI is not registered: phantasosSchema is not on "
                                "PXR_PLUGINPATH_NAME. Launch usdview through launch_phantasos.")
            return
        self.schema.setText(params.schema_doc())
        boxes, layouts = {}, {}
        for parameter in every:
            if parameter.group not in boxes:
                boxes[parameter.group] = QtWidgets.QGroupBox(parameter.group)
                layouts[parameter.group] = QtWidgets.QVBoxLayout(boxes[parameter.group])
            self.rows.append(_Row(self, parameter, layouts[parameter.group]))
        order = {name: i for i, name in enumerate(params.GROUPS)}
        for name in sorted(boxes, key=lambda g: order.get(g, len(order))):
            self.body.addWidget(boxes[name])
        self.body.addStretch(1)

    # --- Stage and selection ---------------------------------------------------------

    def _on_stage(self):
        if self.notice:
            self.notice.Revoke()
            self.notice = None
        stage = self.data.stage
        if stage:
            self.notice = Tf.Notice.Register(Usd.Notice.ObjectsChanged, self._on_changed, stage)
            self.layer.blockSignals(True)
            self.layer.setCurrentIndex(1 if stage.GetEditTarget().GetLayer() == stage.GetSessionLayer() else 0)
            self.layer.blockSignals(False)
        self.prims = []
        self._take_selection()

    def _on_selection(self, *_):
        if self.follow.isChecked():
            self._take_selection()

    def _take_selection(self):
        # usdview selects the pseudo-root when nothing else is: not editable.
        self.prims = [p for p in self.data.selection.getPrims()
                      if p and p.IsValid() and not p.IsPseudoRoot()]
        self.refresh()

    def _on_changed(self, notice, sender):
        # Any change may be to an ancestor a value is inherited from: refresh,
        # once, after usdview has handled the same change.
        if self.prims and not self.pending.isActive():
            self.pending.start()

    def refresh(self):
        self.prims = [p for p in self.prims if p.IsValid()]
        paths = [str(p.GetPath()) for p in self.prims]
        if not paths:
            self.selection.setText("Nothing selected: select prims in the viewport or the prim browser.")
        elif len(paths) == 1:
            prim = self.prims[0]
            applied = "PhantasosAPI applied" if params.has_schema(prim) else "no PhantasosAPI yet"
            self.selection.setText(f"<b>{paths[0]}</b> &nbsp; <span style='{MUTED}'>{prim.GetTypeName()}, {applied}</span>")
        else:
            more = f" and {len(paths) - 3} more" if len(paths) > 3 else ""
            self.selection.setText(f"<b>{len(paths)} prims</b>: " + ", ".join(paths[:3]) + more)
        self.updating = True
        try:
            for row in self.rows:
                row.show(params.read(self.prims, row.parameter))
        finally:
            self.updating = False
        self.clear.setEnabled(bool(self.prims))
        stage = self.data.stage
        dirty = bool(stage) and stage.GetRootLayer().dirty
        self.save.setText("Save *" if dirty else "Save")

    def select(self, path):
        self.data.selection.setPrimPath(path)

    # --- Editing ---------------------------------------------------------------------

    def say(self, message):
        self.status.setText(message)

    def apply(self, what, edit):
        if not self.prims:
            return
        stage = self.data.stage
        layer = stage.GetEditTarget().GetLayer()
        try:
            edit(self.prims)
            where = "session layer" if layer == stage.GetSessionLayer() else layer.GetDisplayName()
            count = len(self.prims)
            self.say(f"{what} on {count} prim{'s' if count != 1 else ''}, in {where}.")
        except Exception as error:  # noqa: BLE001 - shown, not raised into usdview
            self.say(f"{what} failed: {error}")
        self.refresh()

    def _on_layer(self, index):
        stage = self.data.stage
        if stage:
            stage.SetEditTarget(stage.GetSessionLayer() if index == 1 else stage.GetRootLayer())

    def _on_save(self):
        stage = self.data.stage
        if not stage:
            return
        layer = stage.GetRootLayer()
        if layer.anonymous:
            self.say("The root layer is anonymous: nothing to save it to.")
            return
        if layer.Save():
            self.say(f"Saved {layer.realPath}.")
        else:
            self.say(f"Could not save {layer.realPath}.")
        self.refresh()

    def _on_remove_all(self):
        count = len(self.prims)
        answer = QtWidgets.QMessageBox.question(
            self, "Remove all", f"Remove every Phantasos parameter, and PhantasosAPI, from "
                                f"{count} prim{'s' if count != 1 else ''}?")
        if answer != QtWidgets.QMessageBox.Yes:
            return

        def remove(prims):
            left = params.remove_all(prims, self.every)
            if left:
                self.say(f"Some parameters are still authored on {len(left)} prim(s) in a layer "
                         "the edit target cannot change.")
        self.apply("Removed all", remove)

    def closeEvent(self, event):
        if self.notice:
            self.notice.Revoke()
            self.notice = None
        try:
            self.data.selection.signalPrimSelectionChanged.disconnect(self._on_selection)
            self.data.signalStageReplaced.disconnect(self._on_stage)
        except (RuntimeError, TypeError):
            pass
        super().closeEvent(event)


def open_editor(usdviewApi):
    window = getattr(usdviewApi.qMainWindow, "_phantasosEditor", None)
    try:
        alive = window is not None and window.isVisible()
    except RuntimeError:  # closed: Qt deleted it
        alive = False
    if not alive:
        window = PhantasosEditor(usdviewApi)
        usdviewApi.qMainWindow._phantasosEditor = window
    window.show()
    window.raise_()
    window.activateWindow()
