# orbital-face

A watchface showing a small orrery of the solar system — the Sun and the
eight planets at their real heliocentric positions today (Paul Schlyter's
low-precision method), tinted by time of day. Shake the watch to nudge the
planets with a damped spring physics response. Written in C using the
Pebble SDK.

## Building & running

```sh
pebble build                          # build for all targetPlatforms
pebble install --emulator emery       # install on the emery emulator
pebble install --phone <ip>
```

## Target platforms

All geometry is derived from the real screen bounds at load time, so this
runs on any display size/shape (`aplite`, `basalt`, `chalk`, `diorite`,
`emery`, `flint`, `gabbro` — see `targetPlatforms` in `package.json`).

## Project layout

```
src/c/           C source for the watchface
package.json     Project metadata (UUID, platforms, resources, message keys)
wscript          Build rules — usually no need to edit
```

## Documentation

Full SDK docs, tutorials, and API reference: <https://developer.repebble.com>
