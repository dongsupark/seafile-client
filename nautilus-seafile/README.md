## Nautilus Extension For Seafile


## Prerequirement

- libsearpc
- ccnet
- seafile

## Installation
- `sudo apt-get install cmake libnautilus-extension-dev`
- `./build.sh`
- copy `libnautilus-seafile.so` to `/usr/lib/nautilus/extensions-3.0/` or `/usr/lib/nautilus/extensions-2.0/` (up to your system)
- `make install` (or `ninja install`)
- `nautilus -q`
