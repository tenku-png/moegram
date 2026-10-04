from base_plugin import BasePlugin

__id__ = "hello_world"
__name__ = "Hello World"
__version__ = "1.0.0"


class HelloWorldPlugin(BasePlugin):
    def on_plugin_load(self):
        self.logger.info("Hello from MoeGram: on_plugin_load()")

    def on_plugin_unload(self):
        self.logger.info("Goodbye from MoeGram: on_plugin_unload()")
