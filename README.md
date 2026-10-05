# doom-carbon-2
This is a port of The Ultimate DOOM for Elegoo Centauri Carbon 2.

**Requires you to have root on your Elegoo Centauri Carbon 2!**

# Before we start
> This project is not affiliated with, endorsed by or supported by ELEGOO.
> I do not take responsibilities if you brick or break your 3D Printer.

# How to build

### Prerequisites
On your host PC (Linux or WSL Ubuntu), install the ARM cross-compiler:
```bash
sudo apt update
sudo apt install -y gcc-arm-linux-gnueabihf build-essential

```

### Build Binary

1. Put The Ultimate DOOM's `DOOM.WAD` into the `doomgeneric` folder.

2. Compile the binary statically using `arm-linux-gnueabihf-gcc`:

```bash
arm-linux-gnueabihf-gcc -O3 -static -w -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
    -o doom \
    doomgeneric.c doomgeneric_fb_touch.c \
    am_map.c doomdef.c doomstat.c dstrings.c d_event.c d_items.c d_iwad.c \
    d_loop.c d_main.c d_mode.c d_net.c f_finale.c f_wipe.c g_game.c hu_lib.c \
    hu_stuff.c info.c i_cdmus.c i_endoom.c i_joystick.c i_scale.c i_sound.c \
    i_system.c i_timer.c memio.c m_argv.c m_bbox.c m_cheat.c m_config.c \
    m_controls.c m_fixed.c m_menu.c m_misc.c m_random.c p_ceilng.c p_doors.c \
    p_enemy.c p_floor.c p_inter.c p_lights.c p_map.c p_maputl.c p_mobj.c \
    p_plats.c p_pspr.c p_saveg.c p_setup.c p_sight.c p_spec.c p_switch.c \
    p_telept.c p_tick.c p_user.c r_bsp.c r_data.c r_draw.c r_main.c r_plane.c \
    r_segs.c r_sky.c r_things.c sha1.c sounds.c statdump.c st_lib.c st_stuff.c \
    s_sound.c tables.c v_video.c wi_stuff.c w_checksum.c w_file.c w_main.c \
    w_wad.c z_zone.c w_file_stdc.c dummy.c -lm

```

# Installation & Setup

1. Copy the compiled `doom` executable and your `DOOM.WAD` to the printer:

```bash
scp doom DOOM.WAD root@<PRINTER_IP>:/opt/bin/

```

2. SSH into your printer:

```bash
ssh root@<PRINTER_IP>

```

3. Kill the printer's stock UI to release `/dev/fb0`:

```sh
killall -9 ec-eeb001-gui 2>/dev/null

```

4. Make the binary executable and launch the game:

```sh
chmod +x /opt/bin/doom
cd /opt/bin
./doom -iwad DOOM.WAD

```

# Controls

Touchscreen overlay controls are mapped to the on-screen display:

| Button | Action |
| --- | --- |
| **ESC** | Open / Close Menu |
| **ENT** | Select / Confirm Menu Options |
| **FWD** | Move Forward |
| **BCK** | Move Backward |
| **SL** | Strafe Left |
| **SR** | Strafe Right |
| **TL** | Turn Left |
| **TR** | Turn Right |
| **FIRE** | Shoot (Ctrl) |
| **USE** | Open doors / Activate switches (Spacebar) |

> **Note**: To restore the printer's default touch interface after quitting, simply reboot the machine via `reboot` or restart the GUI daemon.

# Credits

[ozkl](https://github.com/ozkl/) - [doomgeneric](https://github.com/ozkl/doomgeneric/)
