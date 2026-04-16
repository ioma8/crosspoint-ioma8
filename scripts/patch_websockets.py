"""
PlatformIO pre-build script: patch WebSockets for Arduino ESP32 3.x.

links2004/WebSockets 2.7.3 calls NetworkClient::flush(), which is deprecated
by the current ESP32 Arduino core in favor of clear(). The call is only used to
discard receive data before stopping a disconnected client, so clear() preserves
the intended behavior and removes the build warning.
"""

Import("env")
import os


def patch_websockets(env):
    libdeps_dir = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")
    if not os.path.isdir(libdeps_dir):
        return

    for env_dir in os.listdir(libdeps_dir):
        client_cpp = os.path.join(libdeps_dir, env_dir, "WebSockets", "src", "WebSocketsClient.cpp")
        if os.path.isfile(client_cpp):
            _apply_networkclient_clear_patch(client_cpp)


def _apply_networkclient_clear_patch(filepath):
    marker = "// CrossPoint patch: NetworkClient::flush() is deprecated on ESP32 Arduino 3.x"
    with open(filepath, "r") as f:
        content = f.read()

    if marker in content:
        return

    old = """\
#if (WEBSOCKETS_NETWORK_TYPE != NETWORK_ESP8266_ASYNC)
            client->tcp->flush();
#endif
            client->tcp->stop();"""

    new = """\
#if (WEBSOCKETS_NETWORK_TYPE != NETWORK_ESP8266_ASYNC)
            """ + marker + """
            client->tcp->clear();
#endif
            client->tcp->stop();"""

    if old not in content:
        print("WARNING: WebSockets NetworkClient clear patch target not found in %s — library may have been updated" %
              filepath)
        return

    content = content.replace(old, new, 1)
    with open(filepath, "w") as f:
        f.write(content)
    print("Patched WebSockets: replace deprecated NetworkClient::flush() with clear(): %s" % filepath)


patch_websockets(env)
