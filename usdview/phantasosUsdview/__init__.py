"""usdview plugin: Phantasos > Parameters... opens a window to add, change
and remove the PhantasosAPI parameters of the selected prims (editor.py).

Installed into <USDEXTRA>/lib/python/phantasosUsdview, with its plugInfo.json
in <USDEXTRA>/plugin/usd/phantasosUsdview/resources; setup_usd_env puts both
on the paths usdview searches.
"""

from pxr import Tf
from pxr.Usdviewq.plugin import PluginContainer


def _open_editor(usdviewApi):
    # Imported here, so usdview's start-up does not import Qt code for it.
    from .editor import open_editor
    open_editor(usdviewApi)


class PhantasosPluginContainer(PluginContainer):
    def registerPlugins(self, plugRegistry, usdviewApi):
        self._editor = plugRegistry.registerCommandPlugin(
            "PhantasosPluginContainer.parameters", "Parameters...", _open_editor,
            "Add, change and remove the Phantasos parameters of the selected prims")

    def configureView(self, plugRegistry, plugUIBuilder):
        plugUIBuilder.findOrCreateMenu("Phantasos").addItem(self._editor)


Tf.Type.Define(PhantasosPluginContainer)
