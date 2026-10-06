# MangoHud Next

MangoHud Next is a server/client rewrite of MangoHud.

## Usage

The server and clients may be started independently and in any order.

Start the server:
```sh
mangohud-server
```

Run an application through the launcher for OpenGL or mixed Vulkan/OpenGL use:

```sh
mangohud-next glxgears
```

For Steam launch options, use:

```text
mangohud-next %command%
```

Runtime metric snapshots can be requested from the server as JSON:

- `mangohud-next get_system` returns the current server-side system metrics, including GPU polling state when GPU metrics are present.
- `mangohud-next get_clients` returns the current connected client metrics and process ids.

## Configuration

Configuration is documented in [server/README.md](server/README.md).

The config file is loaded from `$XDG_CONFIG_HOME/MangoHud/MangoHud.yml`, or `~/.config/MangoHud/MangoHud.yml` when `XDG_CONFIG_HOME` is not set.

### Blacklist

To exclude an application from the HUD, add its executable name to `options.blacklist`:

```yaml
options:
  blacklist:
    - example-game
```

These names extend the built-in blacklist, which includes `gamescope`.
Excluded applications forward graphics calls without starting a HUD client.

## Current Status

MangoHud Next is under active development. While already usable, you may encounter bugs or unsupported configurations as development continues.
